#include "shmringbuffer.hh"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <sched.h>
#include <sys/wait.h>
#include <unistd.h>

struct LogNode {
  int ts;  // 0 for child; 1 for parent
  int len; // length
#define MAX_LOG_LEN 256
  char log[MAX_LOG_LEN];

  const std::string unparse() {
    return "[" + std::to_string(ts) + "] " + std::string(&log[0]);
  }
};

int main() {
  /* initialize random seed: */
  srand(time(NULL));

  const int CAPACITY = 20;
  pid_t pid1 = fork();
  pid_t wpid;
  if (pid1 == 0) {
    // child process must start after master process
    usleep(500);
    ShmRingBuffer<LogNode> buffer(CAPACITY, false);
    int start = 1000;
    LogNode log;
    log.ts = 0;
    for (int i = start; i < start + 10 * CAPACITY; ++i) {
      snprintf(log.log, MAX_LOG_LEN, "%zu: %d", buffer.end(), i);
      buffer.push_back(log);
      std::cout << "child: insert " << i << ", index " << buffer.end()
                << "; count: " << buffer.count() << std::endl; // FIXME
      usleep(rand() % 1000 + 500);
    }
    exit(0);
  } else if (pid1 > 0) {
    pid_t pid2 = fork();
    if (pid2 == 0) {
      // Child2, reading
      usleep(500);
      ShmRingBuffer<LogNode> buffer(CAPACITY, false);
      for (int i = 0; i < 20; ++i) {
        auto node = buffer.dump_front();
        std::cout << string(node.unparse()) << std::endl;
        usleep(rand() % 900 + 500);
      }
      exit(0);

    } else if (pid2 > 0) {
      // parent process
      ShmRingBuffer<LogNode> buffer(CAPACITY, true);
      int start = 2000;
      LogNode log;
      log.ts = 1;
      for (int i = start; i < start + 10 * CAPACITY; ++i) {
        snprintf(log.log, MAX_LOG_LEN, "%zu: %d", buffer.end(), i);
        buffer.push_back(log);
        std::cout << "parent: insert " << i << ", index " << buffer.end()
                  << "; count: " << buffer.count() << std::endl; // FIXME
        usleep(rand() % 900 + 500);
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
