/* Copyright 2016 Google Inc. All Rights Reserved.
 *
 * Use of this source code is governed by the bzip2
 * license that can be found in the LICENSE file. */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <nv.h>

#include "rpc-util.h"

int verbose = 4;  /* smaller number => more verbose */

static const char *g_exe_file = "./bz2-driver-libnv";
static int g_exe_fd = -1;  /* File descriptor to driver executable */
/* Before main(), get an FD for the driver program, so that it is still
   accessible even if the application enters a sandbox. */
void __attribute__((constructor)) _stub_construct(void) {
  g_exe_fd = OpenDriver(g_exe_file);
}

struct DriverConnection {
  /* Child process ID for the driver process */
  pid_t pid;
  /* Socket pair for communcation with driver process */
  int socket_fds[2];
} g_conn;


static struct DriverConnection *CreateConnection(void) {
  struct DriverConnection *conn = &g_conn;
  /* Create socket for communication with child */
  conn->socket_fds[0] = -1;
  conn->socket_fds[1] = -1;
  int rc = socketpair(AF_UNIX, SOCK_STREAM, 0, conn->socket_fds);
  if (rc < 0) {
    error_("failed to open sockets, errno=%d (%s)", errno, strerror(errno));
    return NULL;
  }

  api_("CreateConnection(g_exe_fd=%d, '%s')", g_exe_fd, g_exe_file);

  conn->pid = fork();
  if (conn->pid < 0) {
    error_("failed to fork, errno=%d (%s)", errno, strerror(errno));
    return NULL;
  }

  if (conn->pid == 0) {
    /* Child process: store the socket FD in the environment */
    char *argv[] = {(char *)"bz2-driver", NULL};
    char nonce_buffer[] = "API_NONCE_FD=xxxxxxxx";
    char * envp[] = {nonce_buffer, NULL};
    sprintf(nonce_buffer, "API_NONCE_FD=%d", conn->socket_fds[1]);
    verbose_("in child process, about to fexecve(fd=%d ('%s'), API_NONCE_FD=%d)",
             g_exe_fd, g_exe_file, conn->socket_fds[1]);
    fexecve(g_exe_fd, argv, envp);
    error_("!!! in child process, failed to fexecve, errno=%d (%s)", errno, strerror(errno));
    exit(1);
  }

  return conn;
}

static void DestroyConnection(struct DriverConnection *conn) {
  int ii;
  api_("DestroyConnection(conn=%p {pid=%d })", conn, conn->pid);

  for (ii = 0; ii < 2; ii ++) {
    if (conn->socket_fds[ii] >= 0) {
      verbose_("close socket_fds[%d]= %d", ii, conn->socket_fds[ii]);
      close(conn->socket_fds[ii]);
      conn->socket_fds[ii] = -1;
    }
  }
  if (conn->pid > 0) {
    int status;
    log_("kill conn->pid %d", conn->pid);
    kill(conn->pid, SIGKILL);
    log_("reap conn->pid %d", conn->pid);
    pid_t rc = waitpid(conn->pid, &status, 0);
    log_("reaped conn->pid %d, rc=%d, status=%x", conn->pid, rc, status);
    conn->pid = 0;
  }
}


/*****************************************************************************/
/* Everything above here is generic, and would be useful for any remoted API */
/*****************************************************************************/



/* RPC-Forwarding versions of libbz2 entrypoints */

int BZ2_bzCompressStream(int ifd, int ofd, int blockSize100k, int verbosity, int workFactor) {
  static const char *cmd = "BZ2_bzCompressStream";
  struct DriverConnection *conn = CreateConnection();
  nvlist_t *nvl;

  nvl = nvlist_create(0);
  nvlist_add_string(nvl, "cmd", cmd);
  nvlist_add_descriptor(nvl, "ifd", ifd);
  nvlist_add_descriptor(nvl, "ofd", ofd);
  nvlist_add_number(nvl, "blockSize100k", (uint64_t)blockSize100k);
  nvlist_add_number(nvl, "verbosity", (uint64_t)verbosity);
  nvlist_add_number(nvl, "workFactor", (uint64_t)workFactor);

  api_("%s(%d, %d, %d, %d, %d) =>", cmd, ifd, ofd, blockSize100k, verbosity, workFactor);
  nvl = nvlist_xfer(conn->socket_fds[0], nvl, 0);

  assert (nvl != NULL);
  int retval = nvlist_get_number(nvl, "retval");
  api_("%s(%d, %d, %d, %d, %d) return %d <=", cmd, ifd, ofd, blockSize100k, verbosity, workFactor, retval);
  nvlist_destroy(nvl);
  DestroyConnection(conn);
  return retval;
}

int BZ2_bzDecompressStream(int ifd, int ofd, int verbosity, int small) {
  static const char *cmd = "BZ2_bzDecompressStream";
  struct DriverConnection *conn = CreateConnection();
  nvlist_t *nvl;

  nvl = nvlist_create(0);
  nvlist_add_string(nvl, "cmd", cmd);
  nvlist_add_descriptor(nvl, "ifd", ifd);
  nvlist_add_descriptor(nvl, "ofd", ofd);
  nvlist_add_number(nvl, "verbosity", (uint64_t)verbosity);
  nvlist_add_number(nvl, "small", (uint64_t)small);

  api_("%s(%d, %d, %d, %d) =>", cmd, ifd, ofd, verbosity, small);
  nvl = nvlist_xfer(conn->socket_fds[0], nvl, 0);

  assert (nvl != NULL);
  int retval = nvlist_get_number(nvl, "retval");
  api_("%s(%d, %d, %d, %d) return %d <=", cmd, ifd, ofd, verbosity, small, retval);
  nvlist_destroy(nvl);
  DestroyConnection(conn);
  return retval;
}

int BZ2_bzTestStream(int ifd, int verbosity, int small) {
  static const char *cmd = "BZ2_bzTestStream";
  struct DriverConnection *conn = CreateConnection();
  nvlist_t *nvl;

  nvl = nvlist_create(0);
  nvlist_add_string(nvl, "cmd", cmd);
  nvlist_add_descriptor(nvl, "ifd", ifd);
  nvlist_add_number(nvl, "verbosity", (uint64_t)verbosity);
  nvlist_add_number(nvl, "small", (uint64_t)small);

  api_("%s(%d, %d, %d) =>", cmd, ifd, verbosity, small);
  nvl = nvlist_xfer(conn->socket_fds[0], nvl, 0);

  assert (nvl != NULL);
  int retval = nvlist_get_number(nvl, "retval");
  api_("%s(%d, %d, %d) return %d <=", cmd, ifd, verbosity, small, retval);
  nvlist_destroy(nvl);
  DestroyConnection(conn);
  return retval;
}

const char *BZ2_bzlibVersion(void) {
  static const char *cmd = "BZ2_bzlibVersion";
  static const char *saved_version = NULL;
  if (saved_version) {
    api_("%s() return '%s' <= (saved)", cmd, saved_version);
    return saved_version;
  }

  struct DriverConnection *conn = CreateConnection();
  nvlist_t *nvl;

  nvl = nvlist_create(0);
  nvlist_add_string(nvl, "cmd", cmd);

  api_("%s() =>", cmd);
  nvl = nvlist_xfer(conn->socket_fds[0], nvl, 0);

  assert (nvl != NULL);
  const char *retval = nvlist_get_string(nvl, "retval");
  api_("%s() return '%s' <=", cmd, retval);
  saved_version = strdup(retval);
  nvlist_destroy(nvl);
  DestroyConnection(conn);
  return saved_version;
}
