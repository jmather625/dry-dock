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

#define CHILD_STACK_SIZE              (1024 * 1024) // Get scary memory errors if 1024 and 2*1024.
#define CGROUP_PATH_V1                "/sys/fs/cgroup"

// memory namespace
#define CGROUP_MEMORY_DIR             "/sys/fs/cgroup/memory/drydock"
#define CGROUP_MEMORY_PROCS           "/sys/fs/cgroup/memory/drydock/cgroup.procs"
#define CGROUP_MEMORY_LIMIT           "/sys/fs/cgroup/memory/drydock/memory.limit_in_bytes"
#define CGROUP_MEM_PLUS_SWAP_LIMIT    "/sys/fs/cgroup/memory/drydock/memory.memsw.limit_in_bytes"

// pid namespace
#define CGROUP_PID_DIR                "/sys/fs/cgroup/pids/drydock/"
#define CGROUP_PID_PROCS              "/sys/fs/cgroup/pids/drydock/cgroup.procs"
#define CGROUP_PID_LIMIT              "/sys/fs/cgroup/pids/drydock/pids.max"

// cpu namespace
#define CGROUP_CPU_DIR                "/sys/fs/cgroup/cpu/drydock/"
#define CGROUP_CPU_PROCS              "/sys/fs/cgroup/cpu/drydock/cgroup.procs"
#define CGROUP_CPU_PERIOD             "/sys/fs/cgroup/cpu/drydock/cpu.cfs_period_us"
#define CGROUP_CPU_QUOTA              "/sys/fs/cgroup/cpu/drydock/cpu.cfs_quota_us"

// network namespace to join
#define NETWORK_NAMESPACE             "/var/run/netns/netns0"

static bool* cgroups_done;

void container_print_usage() {
  printf("./container [config_file] container executable\n");
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

  // Busy wait until all of that cgroup stuff has been setup from main.
  puts("Waiting for all cgroups to be setup...");
  while(!(*cgroups_done)) {}

  if (unshare(CLONE_NEWIPC) == -1) {
    perror("Failed to create new namespaces for container process");
    exit(EXIT_FAILURE);
  }

  puts("Joining a network namespace...");
  int fd = open(NETWORK_NAMESPACE, O_RDONLY, 0);
  if (fd < 0) {
    perror("Failed to open namespace path");
    exit(EXIT_FAILURE);
  }
  if (setns(fd, 0)) {
    perror("Failed to set the network namespace");
  }
  close(fd);

  // Need to change to the new root directory before chroot or it is very easy to potentially escape.
  printf("chdir-ing to %s\n", options->container_root_path);
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
    fprintf(stderr, "Going to exec in container this command: %s\n", options->exec_command[0]);
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


void setup_memory_cgroup(container_params_t* options, char* container_pid, size_t container_pid_len) {
  puts("Setting memory limits for container...");

  if (mkdir(CGROUP_MEMORY_DIR, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) == -1) {
    perror("Failed to create CGROUP_MEMORY_DIR");
    fputs(">>>>>>>> Warning: No memory or swap limits will be set! <<<<<<<<\n", stderr);
    return;
  }
  FILE* f = fopen(CGROUP_MEMORY_LIMIT, "w");
  if (f) {
    size_t num_bytes = strlen(options->mem_limit);
    if (fwrite(options->mem_limit, sizeof(char), num_bytes, f) != num_bytes) {
      perror("Failed to write memory limit to CGROUP_MEMORY_LIMIT");
      fputs(">>>>>>>> Warning: Memory limit not set! <<<<<<<<\n", stderr);
    }
    if (fclose(f) != 0) {
      perror("Failed to close CGROUP_MEMORY_LIMIT");
    }
  }
  else {
    perror("Failed to open CGROUP_MEMORY_LIMIT");
    perror("Failed to write memory limit to CGROUP_MEMORY_LIMIT");
    fputs(">>>>>>>> Warning: Memory limit not set! <<<<<<<<\n", stderr);
  }

  f = fopen(CGROUP_MEM_PLUS_SWAP_LIMIT, "w");
  if (f) {
    size_t num_bytes = strlen(options->mem_plus_swap_limit);
    if (fwrite(options->mem_plus_swap_limit, sizeof(char), num_bytes, f) != num_bytes) {
      perror("Failed to write memory plus swap limit to CGROUP_MEM_PLUS_SWAP_LIMIT");
      fputs(">>>>>>>> Warning: Memory plus swap limit not set! <<<<<<<<\n", stderr);
      fputs(">>>>>>>> Note: If container exceeds memory limit, it will use swap instead of killing processes! <<<<<<<<\n", stderr);
    }
    if (fclose(f) != 0) {
      perror("Failed to close CGROUP_MEM_PLUS_SWAP_LIMIT");
    }
  }
  else {
    perror("Failed to open CGROUP_MEM_PLUS_SWAP_LIMIT");
    fputs(">>>>>>>> Warning: Memory plus swap limit not set! <<<<<<<<\n", stderr);
    fputs(">>>>>>>> Note: If container exceeds memory limit, it will use swap instead of killing processes! <<<<<<<<\n", stderr);
  }

  f = fopen(CGROUP_MEMORY_PROCS, "w");
  if (f) {;
    if (fwrite(container_pid, sizeof(char), container_pid_len, f) != container_pid_len) {
      perror("Failed to write container PID to CGROUP_MEMORY_PROCS");
      fputs(">>>>>>>> Warning: No memory or swap limits will be set! <<<<<<<<\n", stderr);
    }
    if (fclose(f) != 0) {
      perror("Failed to close CGROUP_MEMORY_PROCS");
    }
  }
  else {
    perror("Failed to write container PID to CGROUP_MEMORY_PROCS");
    fputs(">>>>>>>> Warning: No memory or swap limits will be set! <<<<<<<<\n", stderr);
  }
}


void setup_pid_cgroup(container_params_t* options, char* container_pid, size_t container_pid_len) {
  puts("Setting number of processes limit for container...");

  if (mkdir(CGROUP_PID_DIR, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) == -1) {
    perror("Failed to create CGROUP_PID_DIR");
    fputs(">>>>>>>> Warning: No PID limits will be set! <<<<<<<<\n", stderr);
    return;
  }
  FILE* f = fopen(CGROUP_PID_LIMIT, "w");
  if (f) {
    size_t num_bytes = strlen(options->pid_limit);
    if (fwrite(options->pid_limit, sizeof(char), num_bytes, f) != num_bytes) {
      perror("Failed to write PID limit to CGROUP_PID_LIMIT");
      fputs(">>>>>>>> Warning: PID limit not set! <<<<<<<<\n", stderr);
    }
    if (fclose(f) != 0) {
      perror("Failed to close CGROUP_PID_LIMIT");
    }
  }
  else {
    perror("Failed to open CGROUP_PID_LIMIT");
    fputs(">>>>>>>> Warning: PID limit not set! <<<<<<<<\n", stderr);
  }

  f = fopen(CGROUP_PID_PROCS, "w");
  if (f) {;
    if (fwrite(container_pid, sizeof(char), container_pid_len, f) != container_pid_len) {
      perror("Failed to write container PID to CGROUP_PID_PROCS");
      fputs(">>>>>>>> Warning: No PID limits will be set! <<<<<<<<\n", stderr);
    }
    if (fclose(f) != 0) {
      perror("Failed to close CGROUP_PID_PROCS");
    }
  }
  else {
    perror("Failed to write container PID to CGROUP_PID_PROCS");
    fputs(">>>>>>>> Warning: No PID limits will be set! <<<<<<<<\n", stderr);
  }
}


void setup_cpu_cgroup(container_params_t* options, char* container_pid, size_t container_pid_len) {
  puts("Setting CPU limits for container...");

  if (mkdir(CGROUP_CPU_DIR, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) == -1) {
    perror("Failed to create CGROUP_CPU_DIR");
    fputs(">>>>>>>> Warning: No CPU limits will be set! <<<<<<<<\n", stderr);
    return;
  }
  FILE* f = fopen(CGROUP_CPU_PERIOD, "w");
  if (f) {
    size_t num_bytes = strlen(options->cpu_period);
    if (fwrite(options->cpu_period, sizeof(char), num_bytes, f) != num_bytes) {
      perror("Failed to write CPU period to CGROUP_CPU_PERIOD");
      fputs(">>>>>>>> Warning: CPU limit may be set to something very strange! <<<<<<<<\n", stderr);
    }
    if (fclose(f) != 0) {
      perror("Failed to close CGROUP_CPU_PERIOD");
    }
  }
  else {
    perror("Failed to open CGROUP_CPU_PERIOD");
    fputs(">>>>>>>> Warning: CPU may be set to something very strange! <<<<<<<<\n", stderr);
  }

  f = fopen(CGROUP_CPU_QUOTA, "w");
  if (f) {
    size_t num_bytes = strlen(options->cpu_quota);
    if (fwrite(options->cpu_quota, sizeof(char), num_bytes, f) != num_bytes) {
      perror("Failed to write CPU quota to CGROUP_CPU_PERIOD");
      fputs(">>>>>>>> Warning: CPU limit may be set to something very strange! <<<<<<<<\n", stderr);
    }
    if (fclose(f) != 0) {
      perror("Failed to close CGROUP_CPU_QUOTA");
    }
  }
  else {
    perror("Failed to open CGROUP_CPU_QUOTA");
    fputs(">>>>>>>> Warning: CPU may be set to something very strange! <<<<<<<<\n", stderr);
  }

  f = fopen(CGROUP_CPU_PROCS, "w");
  if (f) {;
    if (fwrite(container_pid, sizeof(char), container_pid_len, f) != container_pid_len) {
      perror("Failed to write container PID to CGROUP_PID_PROCS");
      fputs(">>>>>>>> Warning: No PID limits will be set! <<<<<<<<\n", stderr);
    }
    if (fclose(f) != 0) {
      perror("Failed to close CGROUP_PID_PROCS");
    }
  }
  else {
    perror("Failed to write container PID to CGROUP_CPU_PROCS");
    fputs(">>>>>>>> Warning: No CPU limits will be set! <<<<<<<<\n", stderr);
  }
}

void setup_network_cgroup(container_params_t* options, char* container_pid, size_t container_pid_len) {

}


void setup_cgroups(container_params_t* options, pid_t container_pid) {
  puts("Setting up cgroups for resource limits...");

  // //mount -t cgroup -o all cgroup /sys/fs/cgroup
  // puts("Mounting /sys/fs/cgroup if necessary...");
  // if (mount("cgroup", CGROUP_PATH_V1, "cgroup", 0, "") != 0 && errno != EBUSY) {
  //   perror("Mounting /sys/fs/cgroup failed");
  //   fputs(">>>>>>>> Warning: No resource limits will be set! <<<<<<<<\n", stderr);
  //   return;
  // }

  // Use snprintf to find the number of bytes we need to store the PID as a string.
  // Note: snprintf does not count the NULL byte in its return value.
  size_t container_pid_str_len = snprintf(NULL, 0, "%u", container_pid);
  char container_pid_str[++container_pid_str_len]; // Need to increment container_pid_len by 1.
  snprintf(container_pid_str, container_pid_str_len, "%u", container_pid);

  setup_memory_cgroup(options, container_pid_str, container_pid_str_len);
  setup_pid_cgroup(options, container_pid_str, container_pid_str_len);
  setup_cpu_cgroup(options, container_pid_str, container_pid_str_len);
  setup_network_cgroup(options, container_pid_str, container_pid_str_len);

  return;
}


void clean_up_cgroups() {
  puts("Cleaning up cgroups...");
  if (rmdir("/sys/fs/cgroup/memory/drydock") != 0) {
    perror("Deleting memory cgroup failed");
  }
  if (rmdir("/sys/fs/cgroup/pids/drydock") != 0) {
    perror("Deleting pid cgroup failed");
  }
  if (rmdir("/sys/fs/cgroup/cpu/drydock") != 0) {
    perror("Deleting cpu cgroup failed");
  }
}


int main(int argc, char** argv) {
  if (argc < 3) {
    container_print_usage();
    return EXIT_FAILURE;
  }

  // TODO: CPU period should probably be held fixed with the CPU quota computed as a fraction of it.
  container_params_t options = {
    .container_root_path = argv[argc-2],
    .exec_command = &argv[argc-1],
    .mem_limit = "41943040",
    .mem_plus_swap_limit = "41943040",
    .pid_limit = "10",
    .cpu_period = "1000000",
    .cpu_quota = "200000"
  };

  if(argc==4){
    int config = open(argv[1], O_RDONLY);
    if(config == -1){
      perror("Cannot open config_file");
      return EXIT_FAILURE;
    }
    char buff[1000];
    ssize_t bytes_read = read(config, buff, 1000);
    if(bytes_read == 0 || bytes_read == -1){
      perror("Cannot read config_file");
      return EXIT_FAILURE;
    }

    buff[bytes_read] = '\0';

    printf("Opening and reading config file...\n");
    char* pointer = NULL;
    char* token = NULL;
    token = strtok(buff, "\n");
    while(token != NULL){
      if((pointer = strstr(token, "mem_limit:")) != NULL){
        printf("Changing mem_limit to: %s\n", pointer+11);
        options.mem_limit = pointer+11;
      }
      if((pointer = strstr(token, "mem_plus_swap_limit:")) != NULL){
        printf("Changing mem_plus_swap_limit to: %s\n", pointer+21);
        options.mem_plus_swap_limit = pointer+21;
      }
      if((pointer = strstr(token, "pid_limit:")) != NULL){
        printf("Changing pid_limit to: %s\n", pointer+11);
        options.pid_limit = pointer+11;
      }
      if((pointer = strstr(token, "CPU%:")) != NULL){
        printf("Changing CPU Percentage: %s\n", pointer+6);
        int new_quota = atoi(pointer+6)*10000;
        if(new_quota < 0){
          printf("Cannot change CPU usage to negative amount...and why would you?\n");
          return EXIT_FAILURE;}
          if(new_quota > 1000000){
            printf("Cannot change CPU usage over 100%%...nice try though\n");
            return EXIT_FAILURE;
          }
          char quota[10];
          sprintf(quota, "%d", new_quota);

          options.cpu_quota = quota;
        }
        token = strtok(NULL, "\n");
      }
    }
    // Determines what new namespaces we will create for our containerized process.
    // Note, NEWIPC is going to be set from within that process since we need to synchronize over cgroups_done.

    // int namespaces = CLONE_NEWPID | CLONE_NEWNET | CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWUSER;
    int namespaces = CLONE_NEWPID | CLONE_NEWNET | CLONE_NEWNS;
    int clone_flags = SIGCHLD;

    // Got this from man page.
    char* child_stack = mmap(NULL, CHILD_STACK_SIZE, PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
      if (child_stack == MAP_FAILED) {
        perror("Mmap failed to allocate memory for stack");
      }

      cgroups_done = mmap(0, sizeof(bool), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

      char* child_stack_ptr = child_stack + CHILD_STACK_SIZE; // Stack grows down.
      pid_t child_pid = clone(setup_container_process, child_stack_ptr, clone_flags | namespaces, &options);
      if (child_pid == -1) {
        perror("Cloning process to create container failed");
        return EXIT_FAILURE;
      }

      setup_cgroups(&options, child_pid);

      *cgroups_done = true;

      int status;
      if (waitpid((pid_t) -1, &status, 0) == -1) {
        perror("Waitpid for container failed");
      }

      if (munmap(child_stack, CHILD_STACK_SIZE) == -1) {
        perror("Failed to free mmapped stack");
      }

      if (munmap(cgroups_done, sizeof(bool)) == -1) {
        perror("Failed to free mmapped flag");
      }

      // Need to delete cgroups here because the container no longer has access.
      clean_up_cgroups();

      return EXIT_SUCCESS;
    }
