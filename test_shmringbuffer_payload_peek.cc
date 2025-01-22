#include "shmringbuffer_payload.hh"
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <sched.h>
#include <sys/wait.h>
#include <unistd.h>

constexpr char var1[] = "china shangahi";
constexpr char var2[] = "japan tokyo";
constexpr char var3[] =
    "korea seoul is a great city and everybody is going there";

int main() {
  /* initialize random seed: */
  srand(time(NULL));

  const int CAPACITY = 100;
  pid_t pid1 = fork();
  pid_t wpid;
  if (pid1 == 0) {
    // child process must start after master process
    usleep(5000);
    ShmRingBufferPayload buffer(CAPACITY, false);
    int start = 1000;
    int cnt{0};
    for (int i = start; i < start + 10 * CAPACITY; ++i) {
      if (buffer.push_back(var1, sizeof(var1))) {
        cnt++;
        std::cout << "child: insert " << i << ", index " << buffer.end()
                  << "; count: " << buffer.count()
                  << "; length: " << sizeof(var1) << std::endl; // FIXME
      } else {
        std::cout << "buffer is full" << std::endl;
      }

      usleep(rand() % 1000 + 500);
    }
    std::cout << ">>>>>>>>>>> child pushed: " << cnt << std::endl;
    exit(0);
  } else if (pid1 > 0) {
    // parent process
    std::cout << "parent" << std::endl;
    ShmRingBufferPayload buffer(CAPACITY, true);
    char buf[100];
    int cnt{0};
    auto p = buffer.peek_front(buf, 100);
    // WARN: possible exit before pop all buffers
    while (true) {
      std::cout << "peek front: " << buf << std::endl;
      std::memset(buf, 0, sizeof(buf));
      usleep(rand() % 900 + 500);
      p = buffer.peek_front(buf, 100);
      std::cout << "current cnt: " << buffer.count() << std::endl;
    }
  } else {
    // fork failed
    std::cout << "fork() failed." << std::endl;
    return 1;
  }

  return 0;
}
