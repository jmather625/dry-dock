#pragma once

#include <sys/types.h>

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))


/**
* Just some functions that will ensure read, write, close, and open do not needlessly fail on interrupts.
*/

int close_fd_interrupt_proof(int fd);
int open_fd_interrupt_proof(const char *pathname, int flags);
ssize_t read_all_from_fd(int fd, char *buffer, size_t count);
ssize_t write_all_to_fd(int fd, const char *buffer, size_t count);
