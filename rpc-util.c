/* Copyright 2016 Google Inc. All Rights Reserved.
 *
 * Use of this source code is governed by the bzip2
 * license that can be found in the LICENSE file. */

#include "rpc-util.h"

#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <execinfo.h>
#include <unistd.h>

void _log_at(int level, const char *filename, int lineno, const char *format, ...) {
  if (level < verbose) return;
  va_list args;
  va_start(args, format);
  fprintf(stderr, "[%d %s:%d] ", getpid(), filename, lineno);
  vfprintf(stderr, format, args);
  fprintf(stderr, "\n");
  va_end(args);
  if (level >= 10) exit(1);
}

void CrashHandler(int sig) {
  void *bt[20];
  size_t count;
  int ii;

  error_("Crash handler for signal %d", sig);
  count = backtrace(bt, 20);
  char **frames = backtrace_symbols(bt, count);
  for (ii = 0; ii < count; ii++) {
    error_("  %s", frames[ii]);
  }
  exit(1);
}

int OpenDriver(const char* filename) {
  int fd = open(filename, O_RDONLY|O_CLOEXEC);
  if (fd < 0) {
    error_("failed to open driver executable %s, errno=%d", filename, errno);
  }
  verbose_("opened fd=%d for '%s'", fd, filename);
  return fd;
}

void RunDriver(int xfd, const char *filename, int sock_fd) {
  /* Child process: store the socket FD in the environment */
  char *argv[] = {(char *)filename, NULL};
  char nonce_buffer[] = "API_NONCE_FD=xxxxxxxx";
  char * envp[] = {nonce_buffer, NULL};
  sprintf(nonce_buffer, "API_NONCE_FD=%d", sock_fd);
  verbose_("in child process, about to fexecve(fd=%d ('%s'), API_NONCE_FD=%d)",
           xfd, filename, sock_fd);
  /* Execute the driver program. */
  fexecve(xfd, argv, envp);
  fatal_("!!! in child process, failed to fexecve, errno=%d (%s)", errno, strerror(errno));
}
