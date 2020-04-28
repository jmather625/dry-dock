#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "util.h"

int close_fd_interrupt_proof(int fd) {
  // Keep trying to close a file descriptor until we succeeded or got an error.
  while (close(fd) != 0) {
    // EINTR means we got interrupted, but nothing necessarily went wrong, so we should try again.
    if (errno != EINTR) {
      return -1;
    }
  }

  // Success!
  return 0;
}

int open_fd_interrupt_proof(const char *pathname, int flags) {
  int fd;
  // Keep trying to open a file descriptor until we succeeded or got an error.
  while ((open(pathname, flags)) == -1) {
    // EINTR means we got interrupted, but nothing necessarily went wrong, so we should try again.
    if (errno != EINTR) {
      return -1;
    }
  }

  // Success!
  return fd;
}

ssize_t read_all_from_fd(int fd, char *buffer, size_t count) {
  size_t total_read = 0;
  ssize_t just_read = 0;

  // Keep reading until we run out of bytes (either we've read count), or read returns 0.
  while (total_read < count && (just_read = read(fd, buffer + total_read, count - total_read))) {
    // Handle errors.
    if (just_read == -1) {
      // We were interrupted, but nothing went wrong and nothing more was read; retry.
      if (errno == EINTR) {
        continue;
      }
      return -1;
    }
    total_read += just_read;
  }
  return total_read;
}

ssize_t write_all_to_fd(int fd, const char *buffer, size_t count) {
  // Your Code Here
  // All of this pretty much just taken from my lecture notes directly.
  size_t total_written = 0;
  ssize_t just_written = 0;

  // Loop until we have read everything (as much as we want or have run out, whatever comes first).
  while (total_written < count && (just_written = write(fd, buffer + total_written, count - total_written))) {
    // Handle errors.
    if (just_written == -1) {
      // We were interrupted, but nothing went wrong and nothing more was written; retry.
      if (errno == EINTR) {
        continue;
      }
      return -1;
    }
    total_written += just_written;
  }
  return total_written;
}
