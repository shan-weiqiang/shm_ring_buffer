/*
* Copyright (c) [2025] [shan weiqiang].
* https://github.com/shan-weiqiang/shm_ring_buffer
*
* The code in this file includes modifications and additions to the original
* work which is licensed under the MIT License. See below for the original
* copyright notice. C++ 17 and above required.
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
#include <memory>
#include <optional>
#include <pthread.h>
#include <string>
#include <sys/mman.h> /* shared memory and mmap() */
#include <sys/mman.h>
#include <sys/stat.h> /* S_IRWXU */
#include <sys/types.h>
#include <unistd.h>
#include <utility>

using std::string;

//
// Shared-memory based Ring buffer for variable-length bytes stream.
//
#define EVENT_BUFFER_SHM "/shm_ring_buffer"
class ShmRingBufferPayload {
public:
  ShmRingBufferPayload(
      size_t cap = 2048,
      bool master = false,
      const char* path = EVENT_BUFFER_SHM)
      : _hdr(NULL), _lock(NULL), _v(NULL), _shm_path(path), _shm_size(0),
        _master(master) {
    init(cap, master, path);
  }
  ~ShmRingBufferPayload() {
    if (_hdr)
      munmap((void*)_hdr, _shm_size);
    _hdr = NULL;
    _lock = NULL;
    _v = NULL;
    if (_master)
      shm_unlink(_shm_path.c_str());
  }

  ShmRingBufferPayload(ShmRingBufferPayload&&) = delete;
  ShmRingBufferPayload(const ShmRingBufferPayload&) = delete;

  size_t capacity() const;
  size_t begin() const;
  size_t end() const;
  size_t count() const;

  void clear();                        // clear buffer
  bool push_back(const char*, size_t); // insert new event
  std::optional<std::pair<std::unique_ptr<char[]>, size_t>> pop_front();
  std::optional<std::pair<std::unique_ptr<char[]>, size_t>> peek_front();
  // for success,does not remove element

private:
  std::optional<std::pair<std::unique_ptr<char[]>, size_t>> handle_copy();

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

    int wait(Mutex& m) { return pthread_cond_wait(&_cond, &m._mutex); }
    int timedwait(const struct timespec& ts, Mutex& m) {
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
    size_t _nread, _nread_waiters;
    size_t _nwrite, _nwrite_waiters;
  };

  typedef struct _ShmHeader {
    size_t _capacity; // max number of logs
    size_t _begin;    // start index of the circular buffer
    size_t _end;      // end index of the circular buffer
    size_t _cnt;      // number of entries in the circular buffer
  } ShmHeader;

  ShmHeader* _hdr;
  ReadWriteLock* _lock;
  char* _v; // pointer to the head of event buffer
  string _shm_path;
  size_t _shm_size; // size(bytes) of shared memory
  bool _master;

  bool init(size_t cap, bool master, const char* path);
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

inline bool
ShmRingBufferPayload::init(size_t cap, bool master, const char* path) {
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

  void* pbuf = NULL; /* shared memory adddress */
  pbuf = mmap(NULL, _shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
  if (pbuf == (void*)-1) {
    perror("mmap failed, exiting...");
    if (master) {
      shm_unlink(path);
    }
    exit(1);
  }

  _hdr = reinterpret_cast<ShmHeader*>(pbuf);
  assert(_hdr != NULL);
  _lock = reinterpret_cast<ReadWriteLock*>((char*)_hdr + sizeof(ShmHeader));
  assert(_lock != NULL);
  _v = (char*)_lock + sizeof(ReadWriteLock);
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
  _hdr->_cnt = 0;
  // TODO: memset the shared memory?
  _lock->write_unlock();
}

inline bool ShmRingBufferPayload::push_back(const char* begin, size_t len) {
  assert(_hdr != NULL);
  assert(_v != NULL);

  _lock->write_lock();
  // check space left
  size_t _bytes_left{0};

  if (_hdr->_begin != _hdr->_end) {
    _bytes_left = (_hdr->_begin > _hdr->_end)
                      ? _hdr->_begin - _hdr->_end
                      : _hdr->_begin + _hdr->_capacity - _hdr->_end;
    // _hdr->_begin == _hdr->_end has two implications: empty or full
  } else if (_hdr->_cnt == 0) {
    _bytes_left = _hdr->_capacity - sizeof(size_t);
  } else {
    _bytes_left = 0;
  }

  if (_bytes_left < len + sizeof(size_t)) {
    _lock->write_unlock();
    return false; // not enough space to hold payload
  }

  // copy len info
  for (int i = 0; i < 4; ++i) {
    memcpy(_v + (_hdr->_end + i) % _hdr->_capacity, (char*)&len + i, 1);
  }
  _hdr->_end = (_hdr->_end + sizeof(size_t)) % _hdr->_capacity;
  // copy payload
  if ((_hdr->_begin > _hdr->_end) ||
      (_hdr->_capacity - _hdr->_end >=
       len)) { // one copy is enough(len can be 0)
    memcpy(_v + _hdr->_end, begin, len);
  } else { // need two copies
    size_t _fir_copy = _hdr->_capacity - _hdr->_end;
    memcpy(_v + _hdr->_end, begin, _fir_copy);
    memcpy(_v, begin + _fir_copy, len - _fir_copy);
  }
  _hdr->_cnt++; // increment count of entries in the buffer
  _hdr->_end = (_hdr->_end + len) % _hdr->_capacity; // move end index
  _lock->write_unlock();
  return true;
}

inline std::optional<std::pair<std::unique_ptr<char[]>, size_t>>
ShmRingBufferPayload::handle_copy() {
  if (_hdr->_cnt != 0) {
    size_t len{0};
    for (int i = 0; i < 4; ++i) { // deserialize count byte-wise
      memcpy((char*)&len + i, _v + ((_hdr->_begin + i) % _hdr->_capacity), 1);
    }
    auto _p_data = (_hdr->_begin + sizeof(size_t)) % _hdr->_capacity;
    if (len == 0) {
      return std::make_pair(nullptr, len);
    } else {
      // allocate memory
      auto storage = std::make_unique<char[]>(len);
      // copy data
      if (_hdr->_end > _p_data || _hdr->_capacity - _p_data >= len) {
        memcpy(storage.get(), _v + _p_data, len); // one copy(len can be 0)
      } else {                                    // two copies
        size_t _fir_copy = _hdr->_capacity - _p_data;
        memcpy(storage.get(), _v + _p_data, _fir_copy);
        memcpy(storage.get() + _fir_copy, _v, len - _fir_copy);
      }
      return std::make_pair(std::move(storage), len);
    }
  } else {
    return std::nullopt;
  }
}

inline std::optional<std::pair<std::unique_ptr<char[]>, size_t>>
ShmRingBufferPayload::pop_front() {
  assert(_hdr != NULL);
  assert(_v != NULL);
  _lock->write_lock();
  auto ret = handle_copy();
  if (ret) {
    _hdr->_begin =
        (_hdr->_begin + sizeof(size_t) + ret.value().second) % _hdr->_capacity;
    _hdr->_cnt--; // removing consumed entities
  }

  _lock->write_unlock();
  return ret;
}

inline std::optional<std::pair<std::unique_ptr<char[]>, size_t>>
ShmRingBufferPayload::peek_front() {
  assert(_hdr != NULL);
  assert(_v != NULL);
  _lock->write_lock();
  auto ret = handle_copy();
  _lock->write_unlock();
  return ret;
}

#endif
