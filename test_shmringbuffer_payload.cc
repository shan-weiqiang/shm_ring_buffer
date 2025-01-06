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

  const int CAPACITY = 50;
  pid_t pid1 = fork();
  pid_t wpid;
  if (pid1 == 0) {
    // child process must start after master process
    usleep(5000);
    ShmRingBufferPayload buffer(CAPACITY, false);
    int start = 1000;
    for (int i = start; i < start + 10 * CAPACITY; ++i) {
      if (i % 2 == 0) {
        buffer.push_back(var1, sizeof(var1));
        std::cout << "child: insert " << i << ", index " << buffer.end()
                  << "; count: " << buffer.count()
                  << "; length: " << sizeof(var1) << std::endl; // FIXME
      } else if (i % 3 == 0) {
        buffer.push_back(var2, sizeof(var2));
        std::cout << "child: insert " << i << ", index " << buffer.end()
                  << "; count: " << buffer.count()
                  << "; length: " << sizeof(var2) << std::endl; // FIXME
      } else if (i % 5 == 0) {
        buffer.push_back(var3, sizeof(var3));
        std::cout << "child: insert " << i << ", index " << buffer.end()
                  << "; count: " << buffer.count()
                  << "; length: " << sizeof(var3) << std::endl; // FIXME
      }

      usleep(rand() % 1000 + 500);
    }
    exit(0);
  } else if (pid1 > 0) {
    pid_t pid2 = fork();
    if (pid2 == 0) {
      // Child2, reading
      usleep(5000);
      ShmRingBufferPayload buffer(CAPACITY, false);
      char buf[100];
      while (buffer.pop_front(buf, 100)) {
        std::cout << "pop_ front: " << buf << std::endl;
        usleep(rand() % 900 + 500);
      }
      exit(0);

    } else if (pid2 > 0) {
      // parent process
      ShmRingBufferPayload buffer(CAPACITY, true);
      int start = 1000;
      for (int i = start; i < start + 10 * CAPACITY; ++i) {
        if (i % 2 == 0) {
          buffer.push_back(var1, sizeof(var1));
          std::cout << "child: insert " << i << ", index " << buffer.end()
                    << "; count: " << buffer.count()
                    << "; length: " << sizeof(var1) << std::endl; // FIXME
        } else if (i % 3 == 0) {
          buffer.push_back(var2, sizeof(var2));
          std::cout << "child: insert " << i << ", index " << buffer.end()
                    << "; count: " << buffer.count()
                    << "; length: " << sizeof(var2) << std::endl; // FIXME
        } else if (i % 5 == 0) {
          buffer.push_back(var3, sizeof(var3));
          std::cout << "child: insert " << i << ", index " << buffer.end()
                    << "; count: " << buffer.count()
                    << "; length: " << sizeof(var3) << std::endl; // FIXME
        }

        usleep(rand() % 1000 + 500);
      }
      int status;
      while ((wpid = wait(&status)) > 0)
        ;
      std::cout << "Ring Buffer:" << std::endl;
      std::cout << buffer.unparse() << std::endl;
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
