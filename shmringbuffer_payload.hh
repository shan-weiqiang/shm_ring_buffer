/*
* Copyright (c) [2025] [shan weiqiang].
* https://github.com/shan-weiqiang/shm_ring_buffer
*
* The code in this file includes modifications and additions to the original
* work which is licensed under the MIT License. See below for the original
* copyright notice.
*
* ----------------------------------------------------------------------------
* MIT License

* Copyright (c) 2016 Bo Yang
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
* ----------------------------------------------------------------------------
*
* Modifications and additional functionality were added by [shan weiqiang].
* These modifications are licensed under the terms of the MIT License
*/

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
// Shared-memory based Ring buffer for variable-length bytes stream.
//
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
  bool pop_front(char *, uint32_t);
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

  if (_hdr->_begin != _hdr->_end) {
    _bytes_left = (_hdr->_begin > _hdr->_end)
                      ? _hdr->_begin - _hdr->_end
                      : _hdr->_begin + _hdr->_capacity - _hdr->_end;
    // _hdr->_begin == _hdr->_end has two implications: empty or full
  } else if (_hdr->_cnt == 0) {
    _bytes_left = _hdr->_capacity - sizeof(uint32_t);
  } else {
    _bytes_left = 0;
  }

  if (_bytes_left < len + sizeof(uint32_t)) {
    _lock->write_unlock();
    return false; // not enough space to hold payload
  }

  // copy len info
  for (int i = 0; i < 4; ++i) {
    memcpy(_v + (_hdr->_end + i) % _hdr->_capacity, (char *)&len + i, 1);
  }
  _hdr->_end = (_hdr->_end + sizeof(uint32_t)) % _hdr->_capacity;
  // copy payload
  if ((_hdr->_begin > _hdr->_end) ||
      (_hdr->_capacity - _hdr->_end >=
       len)) { // one copy is enough(len can be 0)
    memcpy(_v + _hdr->_end, begin, len);
  } else { // need two copies
    uint32_t _fir_copy = _hdr->_capacity - _hdr->_end;
    memcpy(_v + _hdr->_end, begin, _fir_copy);
    memcpy(_v, begin + _fir_copy, len - _fir_copy);
  }
  _hdr->_cnt++; // increment count of entries in the buffer
  _hdr->_end = (_hdr->_end + len) % _hdr->_capacity; // move end index
  _lock->write_unlock();
  return true;
}
/**
 * IMPORTANT:Caller need to know previously about the max length of all
 * payloads. Since multiple readers and writers are suppotted, we cannot give
 * lenght of next payload to the caller. Also we cannot give a max length of all
 * stored payloads from within. User need to record max length according to
 * push_back(..) calls and assure that legal_len is enough to hold the
 * about-to-popped payload.
 */
inline bool ShmRingBufferPayload::pop_front(char *dst, uint32_t legal_len) {
  assert(_hdr != NULL);
  assert(_v != NULL);
  bool success = false;
  _lock->write_lock();
  if (_hdr->_cnt != 0) {
    uint32_t len{0};
    for (int i = 0; i < 4; ++i) { // deserialize count byte-wise
      memcpy((char *)&len + i, _v + ((_hdr->_begin + i) % _hdr->_capacity), 1);
    }
    if (legal_len < len) {
      _lock->write_unlock();
      return false; // not enough space to hold payload
    }
    _hdr->_begin = (_hdr->_begin + sizeof(uint32_t)) % _hdr->_capacity;
    // copy data
    if (_hdr->_end > _hdr->_begin || _hdr->_capacity - _hdr->_begin >= len) {
      memcpy(dst, _v + _hdr->_begin, len); // one copy(len can be 0)
    } else {                               // two copies
      uint32_t _fir_copy = _hdr->_capacity - _hdr->_begin;
      memcpy(dst, _v + _hdr->_begin, _fir_copy);
      memcpy(dst + _fir_copy, _v, len - _fir_copy);
    }
    _hdr->_begin = (_hdr->_begin + len) % _hdr->_capacity;
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
