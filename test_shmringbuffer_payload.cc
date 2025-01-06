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

  const int CAPACITY = 1000;
  pid_t pid1 = fork();
  pid_t wpid;
  if (pid1 == 0) {
    // child process must start after master process
    usleep(5000);
    ShmRingBufferPayload buffer(CAPACITY, false);
    int start = 1000;
    int cnt{0};
    for (int i = start; i < start + 10 * CAPACITY; ++i) {
      if (i % 2 == 0) {
        if (buffer.push_back(var1, sizeof(var1)))
          cnt++;
        std::cout << "child: insert " << i << ", index " << buffer.end()
                  << "; count: " << buffer.count()
                  << "; length: " << sizeof(var1) << std::endl; // FIXME

      } else if (i % 3 == 0) {
        if (buffer.push_back(var2, sizeof(var2)))
          cnt++;
        std::cout << "child: insert " << i << ", index " << buffer.end()
                  << "; count: " << buffer.count()
                  << "; length: " << sizeof(var2) << std::endl; // FIXME

      } else if (i % 5 == 0) {
        if (buffer.push_back(var3, sizeof(var3)))
          cnt++;
        std::cout << "child: insert " << i << ", index " << buffer.end()
                  << "; count: " << buffer.count()
                  << "; length: " << sizeof(var3) << std::endl; // FIXME
      }

      usleep(rand() % 1000 + 500);
    }
    std::cout << ">>>>>>>>>>> child pushed: " << cnt << std::endl;
    exit(0);
  } else if (pid1 > 0) {
    pid_t pid2 = fork();
    if (pid2 == 0) {
      // Child2, reading
      usleep(5000);
      ShmRingBufferPayload buffer(CAPACITY, false);
      char buf[100];
      int cnt{0};
      bool p = buffer.pop_front(buf, 100);
      // WARN: possible exit before pop all buffers
      while (p || buffer.count() > 0) {
        std::cout << "pop_ front: " << buf << std::endl;
        if (p)
          cnt++;
        usleep(rand() % 900 + 500);
        p = buffer.pop_front(buf, 100);
      }
      std::cout << "pop_front: " << cnt << std::endl;
      exit(0);

    } else if (pid2 > 0) {
      // parent process
      ShmRingBufferPayload buffer(CAPACITY, true);
      int start = 1000;
      int cnt{0};
      for (int i = start; i < start + 10 * CAPACITY; ++i) {
        if (i % 2 == 0) {
          if (buffer.push_back(var1, sizeof(var1)))
            cnt++;
          std::cout << "parent: insert " << i << ", index " << buffer.end()
                    << "; count: " << buffer.count()
                    << "; length: " << sizeof(var1) << std::endl; // FIXME

        } else if (i % 3 == 0) {
          if (buffer.push_back(var2, sizeof(var2)))
            cnt++;
          std::cout << "parent: insert " << i << ", index " << buffer.end()
                    << "; count: " << buffer.count()
                    << "; length: " << sizeof(var2) << std::endl; // FIXME

        } else if (i % 5 == 0) {
          if (buffer.push_back(var3, sizeof(var3)))
            cnt++;
          std::cout << "parent: insert " << i << ", index " << buffer.end()
                    << "; count: " << buffer.count()
                    << "; length: " << sizeof(var3) << std::endl; // FIXME
        }

        usleep(rand() % 1000 + 500);
      }
      std::cout << ">>>>>>>>>>> parent pushed: " << cnt << std::endl;
      int status;
      while ((wpid = wait(&status)) > 0)
        ;
      std::cout << "Ring Buffer count:" << std::endl;
      std::cout << buffer.count() << std::endl;
    } else {
      // fork failed
      std::cout << "fork() failed." << std::endl;
      return 1;
    }

  } else {
    // fork failed
    std::cout << "fork() failed." << std::endl;
    return 1;
  }

  return 0;
}
