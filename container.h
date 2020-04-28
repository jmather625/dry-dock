#pragma once

#include <sys/types.h>
#include <stdbool.h>

typedef struct {
  char* container_root_path;
  char** exec_command;
  char* mem_limit;
  char* swap_limit;
  char* cpu_limit;
} container_params_t;

void container_print_usage();
int setup_container_process(void* options_ptr);
void setup_cgroups(container_params_t* options, pid_t container_pid);
void zombie_slayer();
