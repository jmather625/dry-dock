#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sched.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/mount.h>

#include "container.h"
#include "util.h"

#define CHILD_STACK_SIZE      (1024 * 1024) // Get scary memory errors if 1024 and 2*1024.
#define CGROUP_PATH_V1        "/sys/fs/cgroup"
#define CGROUP_MEMORY_DIR     "/sys/fs/cgroup/memory/dry-dock"
#define CGROUP_PROCS          "/sys/fs/cgroup/memory/dry-dock/cgroup.procs"
#define CGROUP_MEMORY_LIMIT   "/sys/fs/cgroup/memory/dry-dock/memory.limit_in_bytes"
#define CGROUP_SWAP_LIMIT     "/sys/fs/cgroup/memory/dry-dock/memory.memsw.limit_in_bytes"

void container_print_usage() {
  printf("./container container executable\n");
}

void zombie_slayer() {
  int status;
  int result;
  // TODO Switch to signal rather than blocking call? Only if init needs to do other things.
  while ((result = waitpid((pid_t) -1, 0, 0)) <= 0) {
    if (result == -1) {
      perror("Container ran into error waiting on processes");
    }
  }
}


int setup_container_process(void* options_ptr) {
  container_params_t* options = (container_params_t*) options_ptr;

  setup_cgroups(options, getpid());

  if (unshare(CLONE_NEWNET | CLONE_NEWNS | CLONE_NEWIPC | CLONE_NEWUTS | CLONE_NEWUSER)) {
    perror("Failed to create new namespaces for container process");
    exit(EXIT_FAILURE);
  }

  // Need to change to the new root directory before chroot or it is very easy to potentially escape.
  if (chdir(options->container_root_path) == -1) {
    perror("Failed to navigate to container path");
    exit(EXIT_FAILURE);
  }
  // We can now chroot from our current working directory.
  puts("Chrooting into container...");
  if (chroot("./") == -1) {
    perror("Chroot failed");
    exit(EXIT_FAILURE);
  }

  puts("Mounting /proc...");
  if (mount("proc", "/proc", "proc", 0, "") != 0) {
    perror("Mounting /proc failed");
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
    fprintf(stderr, "Going to exec in container\n");
    execvp(options->exec_command[0], options->exec_command);
    perror("Exec in container failed");
    exit(EXIT_FAILURE); // Only get here if something went wrong.
  }

  // We are now free to act as init and reap zombies.
  zombie_slayer();

  puts("Shutting down container...");
  if (umount("proc") != 0) {
    perror("Unmounting /proc failed");
  }

  exit(EXIT_SUCCESS);
}


void setup_cgroups(container_params_t* options, pid_t container_pid) {
  //mount -t cgroup -o all cgroup /sys/fs/cgroup
  puts("Mounting /sys/fs/cgroup...");
  // Try to mount cgroup and fail if this fails for a reason other than it already being mounted.
  if (mount("cgroup", CGROUP_PATH_V1, "cgroup", 0, "") != 0 && errno != EBUSY) {
    perror("Mounting /sys/fs/cgroup failed");
    fputs(">>>>>>>> Warning: No resource limits will be set! <<<<<<<<\n", stderr);
    return;
  }

  // Modify memory and swap limits.
  // First create cgroup directory.
  if (mkdir(CGROUP_MEMORY_DIR, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) < 0 && errno != EEXIST) {
    perror("Unable to create new cgroup directory");
    fputs(">>>>>>>> Warning: No memory resource limits will be set! <<<<<<<<\n", stderr);
  }

  // Open this file and write the new limit to it.
  int memory_limit_fd = open_fd_interrupt_proof(CGROUP_MEMORY_LIMIT, O_WRONLY | O_TRUNC);
  if (memory_limit_fd < 0) {
    perror("Failed to set memory usage limit");
  }
  else {
    if (write_all_to_fd(memory_limit_fd, options->mem_limit, strlen(options->mem_limit)) == -1) {
      perror("Failed to set memory usage limit");
    }
    if (close_fd_interrupt_proof(memory_limit_fd) == -1) {
      perror("Failed to close memory limit file");
    }
  }

  // Determine the number of bytes we need to write the PID as a string.
  char tmp[1];
  size_t pid_length = snprintf(tmp, 1, "%u", container_pid);
  char pid_str[pid_length];
  sprintf(pid_str, "%u", container_pid);

  int cgroup_procs_fd = open_fd_interrupt_proof(CGROUP_PROCS, O_WRONLY | O_TRUNC);
  if (cgroup_procs_fd < 0) {
    perror("Failed to set all memory limits");
  }
  else {
    if (write_all_to_fd(cgroup_procs_fd, pid_str, pid_length) == -1) {
      perror("Failed to set all memory limits");
    }
    if (close_fd_interrupt_proof(memory_limit_fd) == -1) {
      perror("Failed to set all memory limits");
    }
  }
}


int main(int argc, char** argv) {
  if (argc < 3) {
    container_print_usage();
    return EXIT_FAILURE;
  }

  container_params_t options = {
    .container_root_path = argv[1],
    .exec_command = &argv[2],
    .mem_limit = "500",
    .swap_limit = "0",
    .cpu_limit = "0.4"
  };

  // Determines what new namespaces we will create for our containerized process.
  // int namespaces = CLONE_NEWPID | CLONE_NEWNET | CLONE_NEWNS | CLONE_NEWIPC | CLONE_NEWUTS | CLONE_NEWUSER;
  int namespaces = CLONE_NEWPID;
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
  if (waitpid((pid_t) -1, &status, 0) == -1) {
    perror("Waitpid for container failed");
  }

  if (munmap(child_stack, CHILD_STACK_SIZE) == -1) {
    perror("Failed to free mmapped stack");
  }

  if (rmdir(CGROUP_MEMORY_DIR) != 0) {
    perror("Deleting cgroup failed");
  }

  return EXIT_SUCCESS;
}
