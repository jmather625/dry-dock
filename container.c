#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/mman.h>
#include "container.h"

#define CHILD_STACK_SIZE      (1024 * 1024) // Get scary memory errors if 1024 and 2*1024.

void container_print_usage() {
  printf("./container container executable\n");
}

void zombie_slayer() {
  int status;
  while (waitpid((pid_t) (-1), 0, WNOHANG) > 0) {}

  write(STDERR_FILENO, "Done slaying!\n", 14);
}

int setup_container_process(void* options_ptr) {
  container_params_t options = *((container_params_t*) options_ptr);

  // Need to change to the new root directory before chroot or it is very easy to potentially escape.
  if (chdir(options.container_root_path) == -1) {
    perror("Failed to navigate to container path");
    exit(EXIT_FAILURE);
  }
  // We can now chroot from our current working directory.
  if (chroot("./") == -1) {
    perror("Chroot failed");
    exit(EXIT_FAILURE);
  }

  // We are now PID 1 of our namespace, so time to act like init and clean up after anything that gets orphaned.
  // To do this, we are going to fork and have the user's program run in a new process in our new namespaces.
  pid_t child_pid = fork();
  if (child_pid == -1) {
    perror("Forking process to exec failed");
    return EXIT_FAILURE;
  }
  // Child.
  if (child_pid == 0) {
    execvp(options.exec_command[0], options.exec_command);
    perror("Exec in container failed");
    exit(EXIT_FAILURE); // Only get here if something went wrong.
  }

  fprintf(stderr, "Pid %d\n", child_pid);
  // We are now free to act as init and reap zombies.
  zombie_slayer();

  exit(EXIT_SUCCESS);

}

int main(int argc, char** argv) {
  if (argc < 3) {
    container_print_usage();
    return EXIT_FAILURE;
  }

  container_params_t options = {
    .container_root_path = argv[1],
    .exec_command = &argv[2]
  };

  int namespaces = CLONE_NEWPID | CLONE_NEWIPC; // Determines what new namespaces we will create for our containerized process.
  int clone_flags = SIGCHLD;

  // Got this from man page.
  char* child_stack = mmap(NULL, CHILD_STACK_SIZE, PROT_READ | PROT_WRITE,
    MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
  if (child_stack == MAP_FAILED) {
    perror("Mmap failed to allocate memory for stack");
  }

  char* child_stack_ptr = child_stack + CHILD_STACK_SIZE; // Stack grows down.
  pid_t child_pid = clone(setup_container_process, child_stack_ptr, clone_flags | namespaces, &options);
  if (child_pid == -1) {
    perror("Cloning process to create container failed");
    return EXIT_FAILURE;
  }

  int status;
  if (waitpid(-1, &status, 0) == -1) {
    perror("Waitpid for container failed");
  }

  if (munmap(child_stack, CHILD_STACK_SIZE) == -1) {
    perror("Failed to free mmapped stack");
  }

  return EXIT_SUCCESS;
}
