#ifndef __SHMRINGBUFFERPAYLOAD_HH__
#define __SHMRINGBUFFERPAYLOAD_HH__

#include <assert.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h> /* For O_CREAT, O_RDWR */
#include <pthread.h>
#include <string>
#include <sys/mman.h> /* shared memory and mmap() */
#include <sys/mman.h>
#include <sys/stat.h> /* S_IRWXU */
#include <sys/types.h>
#include <unistd.h>

using std::string;

//
// Shared-memory based Ring buffer.
//
// T must be POD type
#define EVENT_BUFFER_SHM "/shm_ring_buffer"
class ShmRingBufferPayload {
public:
  ShmRingBufferPayload(size_t cap = 2048, bool master = false,
                       const char *path = EVENT_BUFFER_SHM)
      : _hdr(NULL), _lock(NULL), _v(NULL), _shm_path(path), _shm_size(0),
        _master(master) {
    init(cap, master, path);
  }
  ~ShmRingBufferPayload() {
    if (_hdr)
      munmap((void *)_hdr, _shm_size);
    _hdr = NULL;
    _lock = NULL;
    _v = NULL;
    if (_master)
      shm_unlink(_shm_path.c_str());
  }

  size_t capacity() const;
  size_t begin() const;
  size_t end() const;
  size_t count() const;

  void clear();                           // clear buffer
  bool push_back(const char *, uint32_t); // insert new event
  bool pop_front(char *);
  string unparse() const; // dump contents in the buffer to a string

private:
  // Mutex, Condition and ReadWriteLock must be POD type to use shared memory
  class Mutex {
  public:
    pthread_mutex_t _mutex;
    pthread_mutexattr_t _attr;

    void init(bool pshared = false) {
      pthread_mutexattr_init(&_attr);
      if (pshared)
        pthread_mutexattr_setpshared(&_attr, PTHREAD_PROCESS_SHARED);
      else
        pthread_mutexattr_setpshared(&_attr, PTHREAD_PROCESS_PRIVATE);
      pthread_mutex_init(&_mutex, &_attr);
    }

    int lock() { return pthread_mutex_lock(&_mutex); }
    int trylock() { return pthread_mutex_trylock(&_mutex); }
    int unlock() { return pthread_mutex_unlock(&_mutex); }
  };

  class Condition {
  public:
    pthread_cond_t _cond;
    pthread_condattr_t _attr;

    void init(bool pshared = false) {
      pthread_condattr_init(&_attr);
      if (pshared)
        pthread_condattr_setpshared(&_attr, PTHREAD_PROCESS_SHARED);
      else
        pthread_condattr_setpshared(&_attr, PTHREAD_PROCESS_PRIVATE);

      pthread_cond_init(&_cond, &_attr);
    }

    int wait(Mutex &m) { return pthread_cond_wait(&_cond, &m._mutex); }
    int timedwait(const struct timespec &ts, Mutex &m) {
      return pthread_cond_timedwait(&_cond, &m._mutex, &ts);
    }
    int signal() { return pthread_cond_signal(&_cond); }
    int broadcast() { return pthread_cond_broadcast(&_cond); }
  };

  // Multiple-writer, multiple-reader lock (write-preferring)
  class ReadWriteLock {
  public:
    void init(bool pshared = false) {
      _nread = _nread_waiters = 0;
      _nwrite = _nwrite_waiters = 0;
      _mtx.init(pshared);
      _rcond.init(pshared);
      _wcond.init(pshared);
    }

    void read_lock() {
      _mtx.lock();
      if (_nwrite || _nwrite_waiters) {
        _nread_waiters++;
        do
          _rcond.wait(_mtx);
        while (_nwrite || _nwrite_waiters);
        _nread_waiters--;
      }
      _nread++;
      _mtx.unlock();
    }

    void read_unlock() {
      _mtx.lock();
      _nread--;
      if (_nwrite_waiters)
        _wcond.broadcast();
      _mtx.unlock();
    }

    void write_lock() {
      _mtx.lock();
      if (_nread || _nwrite) {
        _nwrite_waiters++;
        do
          _wcond.wait(_mtx);
        while (_nread || _nwrite);
        _nwrite_waiters--;
      }
      _nwrite++;
      _mtx.unlock();
    }

    void write_unlock() {
      _mtx.lock();
      _nwrite--;
      if (_nwrite_waiters)
        _wcond.broadcast();
      else if (_nread_waiters)
        _rcond.broadcast();
      _mtx.unlock();
    }

  private:
    Mutex _mtx;
    Condition _rcond;
    Condition _wcond;
    uint32_t _nread, _nread_waiters;
    uint32_t _nwrite, _nwrite_waiters;
  };

  typedef struct _ShmHeader {
    size_t _capacity; // max number of logs
    int _begin;       // start index of the circular buffer
    int _end;         // end index of the circular buffer
    int _cnt;         // number of entries in the circular buffer
  } ShmHeader;

  ShmHeader *_hdr;
  ReadWriteLock *_lock;
  char *_v; // pointer to the head of event buffer
  string _shm_path;
  size_t _shm_size; // size(bytes) of shared memory
  bool _master;

  bool init(size_t cap, bool master, const char *path);
};

inline size_t ShmRingBufferPayload::capacity() const {
  assert(_hdr != NULL);

  size_t cap = 0;
  _lock->read_lock();
  cap = _hdr->_capacity;
  _lock->read_unlock();
  return cap;
}

inline size_t ShmRingBufferPayload::begin() const {
  assert(_hdr != NULL);

  size_t idx = 0;
  _lock->read_lock();
  idx = _hdr->_begin;
  _lock->read_unlock();
  return idx;
}
inline size_t ShmRingBufferPayload::end() const {
  assert(_hdr != NULL);

  size_t idx = 0;
  _lock->read_lock();
  idx = _hdr->_end;
  _lock->read_unlock();
  return idx;
}

inline size_t ShmRingBufferPayload::count() const {
  assert(_hdr != NULL);

  size_t idx = 0;
  _lock->read_lock();
  idx = _hdr->_cnt;
  _lock->read_unlock();
  return idx;
}

inline bool ShmRingBufferPayload::init(size_t cap, bool master,
                                       const char *path) {
  assert(path != NULL);
  int shm_fd{0};
  // Only master can open shm and master process must be started before any
  // slave process
  if (master) {
    shm_fd =
        shm_open(path, O_CREAT | O_RDWR, S_IRWXU | S_IRWXG); // TODO: O_TRUNC?
  } else {
    shm_fd = shm_open(path, O_RDWR, S_IRWXU | S_IRWXG); // TODO: O_TRUNC?
  }
  if (shm_fd < 0) {
    perror("shm_open failed, exiting..");
    exit(1);
  }

  _shm_size = sizeof(ShmHeader) + sizeof(ReadWriteLock) + cap;
  if (master && (ftruncate(shm_fd, _shm_size) < 0)) {
    perror("ftruncate failed, exiting...");
    shm_unlink(path);
    exit(1);
  }

  void *pbuf = NULL; /* shared memory adddress */
  pbuf = mmap(NULL, _shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
  if (pbuf == (void *)-1) {
    perror("mmap failed, exiting...");
    if (master) {
      shm_unlink(path);
    }
    exit(1);
  }

  _hdr = reinterpret_cast<ShmHeader *>(pbuf);
  assert(_hdr != NULL);
  _lock = reinterpret_cast<ReadWriteLock *>((char *)_hdr + sizeof(ShmHeader));
  assert(_lock != NULL);
  _v = (char *)_lock + sizeof(ReadWriteLock);
  assert(_v != NULL);

  if (master) {
    _hdr->_capacity = cap;
    _hdr->_begin = _hdr->_end = _hdr->_cnt = 0;
    _lock->init(true);
  }

  return true;
}

inline void ShmRingBufferPayload::clear() {
  if (!_hdr || !_lock)
    return;

  _lock->write_lock();
  _hdr->_begin = _hdr->_end = 0;
  // TODO: memset the shared memory?
  _lock->write_unlock();
}

inline bool ShmRingBufferPayload::push_back(const char *begin, uint32_t len) {
  assert(_hdr != NULL);
  assert(_v != NULL);

  _lock->write_lock();
  // check space left

  uint32_t _bytes_left{0};
  uint32_t _rel_end = _hdr->_end;

  if (_hdr->_begin != _hdr->_end) { // not empty
    uint32_t tmp{0};
    // memcpy(&tmp, _v + _hdr->_end, 1); // len of last payload
    // memcpy((char *)&tmp + 1, _v + ((_hdr->_end + 1) % _hdr->_capacity), 1);
    // memcpy((char *)&tmp + 2, _v + ((_hdr->_end + 2) % _hdr->_capacity), 1);
    // memcpy((char *)&tmp + 3, _v + ((_hdr->_end + 3) % _hdr->_capacity), 1);

    for (int i = 0; i < 4; ++i) { // deserialize count byte-wise
      memcpy((char *)&tmp + i, _v + ((_hdr->_end + i) % _hdr->_capacity), 1);
    }

    _rel_end = (_hdr->_end + sizeof(uint32_t) + tmp) %
               _hdr->_capacity; // end of last payload
    _bytes_left =
        (_hdr->_begin >= _rel_end) // if _hdr->begin == _rel_end, then buffer is
                                   // full(possible, but not likely)
            ? _hdr->_begin - _rel_end
            : _hdr->_begin + _hdr->_capacity - _rel_end; // bytes left
  } else {                                               // ring buffer empty
    _bytes_left = _hdr->_capacity - sizeof(uint32_t);
  }

  if (_bytes_left < len + sizeof(uint32_t)) {
    return false; // not enough space to hold payload
  }

  // copy len
  for (int i = 0; i < 4; ++i) {
    memcpy(_v + (_rel_end + i) % _hdr->_capacity, (char *)&len + i, 1);
  }
  // copy payload
  for (int i = 0; i < len; ++i) {
    memcpy(_v + (_rel_end + sizeof(uint32_t) + i) % _hdr->_capacity, begin + i,
           1);
  }

  _hdr->_cnt++; // increment count of entries in the buffer
  // move end index
  _hdr->_end = (_hdr->_end + sizeof(uint32_t) + len) % _hdr->_capacity;
  _lock->write_unlock();
}

inline bool ShmRingBufferPayload::pop_front(char *dst) {
  assert(_hdr != NULL);
  assert(_v != NULL);
  bool success = false;
  _lock->write_lock();
  if (_hdr->_begin != _hdr->_end) {
    uint32_t tmp{0};
    for (int i = 0; i < 4; ++i) { // deserialize count byte-wise
      memcpy((char *)&tmp + i, _v + ((_hdr->_begin + i) % _hdr->_capacity), 1);
    }
    // copy data
    for (int i = 0; i < tmp; ++i) {
      memcpy(dst + i,
             _v + (_hdr->_begin + sizeof(uint32_t) + i) % _hdr->_capacity, 1);
    }
    _hdr->_begin = (_hdr->_begin + sizeof(uint32_t) + tmp) % _hdr->_capacity;
    _hdr->_cnt--; // removing consumed entities
    success = true;
  }
  _lock->write_unlock();
  return success;
}

inline string ShmRingBufferPayload::unparse() const {
  assert(_hdr != NULL);
  assert(_v != NULL);

  string ret;
  _lock->read_lock();
  if (_hdr->_begin == _hdr->_end) {
    _lock->read_unlock();
    return string();
  }
  _lock->read_unlock();
  return ret;
}

#endif
