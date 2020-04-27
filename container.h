#pragma once

typedef struct {
  char* container_root_path;
  char** exec_command;
} container_params_t;

void container_print_usage();
int setup_container_process(void* options_ptr);
void zombie_slayer();
