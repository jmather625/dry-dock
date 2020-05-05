/**
Simple program that tries to create more children.
Use this to test that the cgroup PID limitations will start killing things.
*/

#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>

int main(int argc, char const *argv[]) {
  for (int i = 0; i < 20; ++i) {
    pid_t pid = fork();
    if (pid == -1) {
      fprintf(stderr, "Error!\n");
    }
    else if (pid == 0) {
      sleep(10);
      return 0;
    }

    fprintf(stderr, "%d\n", i);

  }
   return 0;
}
