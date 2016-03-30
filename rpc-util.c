#include "rpc-util.h"

#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
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
