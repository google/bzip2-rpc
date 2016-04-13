/* Copyright 2016 Google Inc. All Rights Reserved.
 *
 * Use of this source code is governed by the bzip2
 * license that can be found in the LICENSE file. */

#include "rpc-util.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <execinfo.h>
#include <unistd.h>

void _log_at(int level, const char *filename, int lineno, const char *format, ...) {
  if (level < _rpc_verbose) return;
  va_list args;
  va_start(args, format);
  fprintf(stderr, "%*s[%d %s:%d] ", _rpc_indent, "", getpid(), filename, lineno);
  vfprintf(stderr, format, args);
  fprintf(stderr, "\n");
  va_end(args);
  if (level >= 10) exit(1);
}

static void __attribute__((constructor)) _log_init(void) {
  const char *value = getenv("RPC_DEBUG");
  if (!value) return;
  _rpc_verbose = atoi(value);
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
  char debug_buffer[] = "RPC_DEBUG=xxxxxxx";
  char * envp[] = {nonce_buffer, debug_buffer, NULL};
  sprintf(nonce_buffer, "API_NONCE_FD=%d", sock_fd);
  sprintf(debug_buffer, "RPC_DEBUG=%d", _rpc_verbose);
  verbose_("in child process, about to fexecve(fd=%d ('%s'), API_NONCE_FD=%d)",
           xfd, filename, sock_fd);
  /* Execute the driver program. */
  fexecve(xfd, argv, envp);
  fatal_("!!! in child process, failed to fexecve, errno=%d (%s)", errno, strerror(errno));
}

void TerminateChild(pid_t child) {
  if (child > 0) {
    int status;
    log_("kill child %d", child);
    kill(child, SIGKILL);
    log_("reap child %d", child);
    pid_t rc = waitpid(child, &status, 0);
    log_("reaped child %d, rc=%d, status=%x", child, rc, status);
    child = 0;
  }
}

int GetTransferredFd(int sock_fd, int nonce) {
  int value = -1;
  struct iovec iov;
  iov.iov_base = &value;
  iov.iov_len = sizeof(nonce);
  unsigned char data[CMSG_SPACE(sizeof(int))];
  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_controllen = sizeof(data);
  msg.msg_control = data;

  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_len = CMSG_LEN(sizeof(int));
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;

  int rc;
  do {
    rc = recvmsg(sock_fd, &msg, 0);
  } while (rc == -1 && errno == EINTR);
  cmsg = CMSG_FIRSTHDR(&msg);
  if (cmsg == NULL)
    fatal_("no cmsghdr received");
  if (cmsg->cmsg_len != CMSG_LEN(sizeof(int)))
    fatal_("unexpected cmsg_len %d", cmsg->cmsg_len);
  if (cmsg->cmsg_level != SOL_SOCKET)
    fatal_("unexpected cmsg_level %d", cmsg->cmsg_level);
  if (cmsg->cmsg_type != SCM_RIGHTS)
    fatal_("unexpected cmsg_type %d", cmsg->cmsg_type);
  if (value != nonce)
    fatal_("unexpected nonce value %d not %d", value, nonce);

  int fd = *((int *) CMSG_DATA(cmsg));
  log_("received fd %d across socket %d nonce=%d rc=%d", fd, sock_fd, nonce, rc);
  return fd;
}

// Returns nonce to be sent instead
int TransferFd(int sock_fd, int fd) {
  int nonce = rand();
  struct iovec iov;
  iov.iov_base = &nonce;
  iov.iov_len = sizeof(nonce);
  unsigned char data[CMSG_SPACE(sizeof(int))];
  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_controllen = sizeof(data);
  msg.msg_control = data;

  struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_len = CMSG_LEN(sizeof(int));
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  *((int*)CMSG_DATA(cmsg)) = fd;

  int rc = sendmsg(sock_fd, &msg, 0);
  log_("sent fd %d across socket %d with nonce=%d rc=%d", fd, sock_fd, nonce, rc);
  return nonce;
}
