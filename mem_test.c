/**
Simple program that tries to progressively allocate more memory.
Use this to test that the cgroup memory limitations will kill the process.
*/

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

// Idea for this from https://stackoverflow.com/questions/1865501/c-program-on-linux-to-exhaust-memory

int main(int argc, char const *argv[]) {
  for (int i = 1; i <= 100; ++i) {
    size_t mem_to_allocate = i * 1024 * 1024; // We want to allocate i MB per iteration.

    char* p = malloc(mem_to_allocate);
    if (!p) {
      printf("Failed to allocate %d MB\n", i);
      break;
    }
    memset(p, 0, mem_to_allocate);
    free(p);
    printf("Allocated %d MB\n", i);
  }

  return 0;
}
