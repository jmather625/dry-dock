#pragma once

#include <sys/types.h>
#include <stdbool.h>

typedef struct {
  char* container_root_path;
  char** exec_command;
  char* mem_limit;
  char* mem_plus_swap_limit;
  char* pid_limit;
  char* cpu_period; // Period in microseconds before the CPU time used towards a quota is reset.
  char* cpu_quota;
} container_params_t;

void container_print_usage();
int setup_container_process(void* options_ptr);
void setup_cgroups(container_params_t* options, pid_t container_pid);
void setup_memory_cgroup(container_params_t* options, char* container_pid, size_t container_pid_len);
void setup_pid_cgroup(container_params_t* options, char* container_pid, size_t container_pid_len);
void setup_cpu_cgroup(container_params_t* options, char* container_pid, size_t container_pid_len);
void clean_up_cgroups();
void zombie_slayer();
