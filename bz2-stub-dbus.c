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

#include <dbus/dbus.h>

#include "rpc-util.h"

int _rpc_verbose = 4;  /* smaller number => more verbose */
int _rpc_indent = 0;

static const char *g_exe_file = "./bz2-driver-dbus";
static int g_exe_fd = -1;  /* File descriptor to driver executable */
/* Before main(), get an FD for the driver program, so that it is still
   accessible even if the application enters a sandbox. */
void __attribute__((constructor)) _stub_construct(void) {
  g_exe_fd = OpenDriver(g_exe_file);
}

#define DRIVER_OBJECT_PATH_PATTERN "/nonce/xxxxxxxxxxx"
#define DRIVER_OBJECT_PATH_LEN     20
struct DriverConnection {
  /* Child process ID for the driver process */
  pid_t pid;
  /* DBus connection to driver process */
  DBusConnection *dbus;
  /* DBus object path '/nonce/xxxxxxxxxxx' */
  char objpath[DRIVER_OBJECT_PATH_LEN];
} g_conn;


static DBusMessage *ConnectionNewRequest(struct DriverConnection *conn, const char *method) {
  return dbus_message_new_method_call(NULL, conn->objpath, NULL, method);
}

static DBusMessage *ConnectionBlockingSendReply(struct DriverConnection *conn,
                                                DBusMessage *req, DBusError *err) {
  DBusMessage *rsp = NULL;
  if (!(rsp = dbus_connection_send_with_reply_and_block(conn->dbus, req, -1, err))) {
    error_("!!! send_with_reply_and_block failed: %s: %s", err->name, err->message);
    return NULL;
  }
  dbus_message_unref(req);
  return rsp;
}

static void DestroyConnection(struct DriverConnection *conn) {
  api_("DestroyConnection(conn=%p {pid=%d dbus_conn=%p objpath='%s'})",
       conn, conn->pid, conn->dbus, conn->objpath);

  if (conn->dbus) {
    verbose_("close and unref DBus conn=%p", conn->dbus);
    dbus_connection_close(conn->dbus);
    dbus_connection_unref(conn->dbus);
    conn->dbus = NULL;
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

static struct DriverConnection *CreateConnection(void) {
  struct DriverConnection *conn = &g_conn;
  /* Create socket for bootstrap communication with child */
  int socket_fds[2] = {-1, -1};
  int rc = socketpair(AF_UNIX, SOCK_STREAM, 0, socket_fds);
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
    // Child process: run the driver
    close(socket_fds[0]);
    RunDriver(g_exe_fd, g_exe_file, socket_fds[1]);
  }

  /* Read bootstrap information back from the child */
  /* First: uint32_t len, char server_add[len] */
  uint32_t len;
  rc = read(socket_fds[0], &len, sizeof(len));
  assert (rc == sizeof(len));
  char *server_address = (char *)malloc(len);
  rc = read(socket_fds[0], server_address, len);
  assert (rc == len);
  /* Second: uint64_t nonce (used to confirm that the D-Bus connection we set up
   * later is indeed to the child process */
  uint64_t nonce;
  rc = read(socket_fds[0], &nonce, sizeof(nonce));
  assert (rc == sizeof(nonce));
  verbose_("started child process %d, read socket name '%s', nonce %ld",
           conn->pid, server_address, nonce);
  close(socket_fds[0]);
  close(socket_fds[1]);
  snprintf(conn->objpath, 20, "/nonce/%ld", nonce);

  /* Initialize D-Bus private connection; use the nonce as the object address */
  DBusError err;
  dbus_error_init(&err);
  conn->dbus = dbus_connection_open_private(server_address, &err);
  if (!conn->dbus) {
    error_("!!! dbus_connection_open_private failed: %s: %s\n", err.name, err.message);
    free(server_address);
    DestroyConnection(conn);
    return NULL;
  }
  log_("got connection %p to private bus '%s' with {pid=%d dbus_conn=%p objpath='%s'})",
       conn, server_address, conn->pid, conn->dbus, conn->objpath);
  free(server_address);

  /* Send a no-op message to work around a D-Bus problem (if the first message sent
     includes a file descriptor, it fails). */
  DBusMessage *req = ConnectionNewRequest(conn, "__noop");
  DBusMessage *rsp = ConnectionBlockingSendReply(conn, req, &err);
  if (rsp == NULL) {
    DestroyConnection(conn);
    return NULL;
  }
  dbus_message_unref(rsp);

  return conn;
}


/*****************************************************************************/
/* Everything above here is generic, and would be useful for any remoted API */
/*****************************************************************************/



/* RPC-Forwarding versions of libbz2 entrypoints */

int BZ2_bzCompressStream(int ifd, int ofd, int blockSize100k, int verbosity, int workFactor) {
  static const char *method = "BZ2_bzCompressStream";
  struct DriverConnection *conn = CreateConnection();
  DBusMessage *msg = ConnectionNewRequest(conn, method);

  DBusMessageIter msg_it;
  dbus_message_iter_init_append(msg, &msg_it);
  dbus_int32_t vx;
  vx = ifd;
  dbus_message_iter_append_basic(&msg_it, DBUS_TYPE_UNIX_FD, &vx);
  vx = ofd;
  dbus_message_iter_append_basic(&msg_it, DBUS_TYPE_UNIX_FD, &vx);
  vx = blockSize100k;
  dbus_message_iter_append_basic(&msg_it, DBUS_TYPE_INT32, &vx);
  vx = verbosity;
  dbus_message_iter_append_basic(&msg_it, DBUS_TYPE_INT32, &vx);
  vx = workFactor;
  dbus_message_iter_append_basic(&msg_it, DBUS_TYPE_INT32, &vx);

  DBusError err;
  dbus_error_init(&err);
  api_("%s(%d, %d, %d, %d, %d) =>", method, ifd, ofd, blockSize100k, verbosity, workFactor);
  DBusMessage *rsp = ConnectionBlockingSendReply(conn, msg, &err);
  assert (rsp != NULL);

  DBusMessageIter rsp_it;
  dbus_message_iter_init(rsp, &rsp_it);
  assert (dbus_message_iter_get_arg_type(&rsp_it) == DBUS_TYPE_INT32);
  dbus_message_iter_get_basic(&rsp_it, &vx);
  dbus_message_iter_next(&rsp_it);
  int retval = vx;
  api_("%s(%d, %d, %d, %d, %d) return %d <=", method, ifd, ofd, blockSize100k, verbosity, workFactor, retval);
  dbus_message_unref(rsp);
  DestroyConnection(conn);
  return retval;
}

int BZ2_bzDecompressStream(int ifd, int ofd, int verbosity, int small) {
  static const char *method = "BZ2_bzDecompressStream";
  struct DriverConnection *conn = CreateConnection();
  DBusMessage *msg = ConnectionNewRequest(conn, method);

  DBusMessageIter msg_it;
  dbus_message_iter_init_append(msg, &msg_it);
  dbus_int32_t vx;
  vx = ifd;
  dbus_message_iter_append_basic(&msg_it, DBUS_TYPE_UNIX_FD, &vx);
  vx = ofd;
  dbus_message_iter_append_basic(&msg_it, DBUS_TYPE_UNIX_FD, &vx);
  vx = verbosity;
  dbus_message_iter_append_basic(&msg_it, DBUS_TYPE_INT32, &vx);
  vx = small;
  dbus_message_iter_append_basic(&msg_it, DBUS_TYPE_INT32, &vx);

  DBusError err;
  dbus_error_init(&err);
  api_("%s(%d, %d, %d, %d) =>", method, ifd, ofd, verbosity, small);
  DBusMessage *rsp = ConnectionBlockingSendReply(conn, msg, &err);
  assert (rsp != NULL);

  DBusMessageIter rsp_it;
  dbus_message_iter_init(rsp, &rsp_it);
  assert (dbus_message_iter_get_arg_type(&rsp_it) == DBUS_TYPE_INT32);
  dbus_message_iter_get_basic(&rsp_it, &vx);
  dbus_message_iter_next(&rsp_it);
  int retval = vx;
  api_("%s(%d, %d, %d, %d) return %d <=", method, ifd, ofd, verbosity, small, retval);
  dbus_message_unref(rsp);
  DestroyConnection(conn);
  return retval;
}

int BZ2_bzTestStream(int ifd, int verbosity, int small) {
  static const char *method = "BZ2_bzTestStream";
  struct DriverConnection *conn = CreateConnection();
  DBusMessage *msg = ConnectionNewRequest(conn, method);

  DBusMessageIter msg_it;
  dbus_message_iter_init_append(msg, &msg_it);
  dbus_int32_t vx;
  vx = ifd;
  dbus_message_iter_append_basic(&msg_it, DBUS_TYPE_UNIX_FD, &vx);
  vx = verbosity;
  dbus_message_iter_append_basic(&msg_it, DBUS_TYPE_INT32, &vx);
  vx = small;
  dbus_message_iter_append_basic(&msg_it, DBUS_TYPE_INT32, &vx);

  DBusError err;
  dbus_error_init(&err);
  api_("%s(%d, %d, %d) =>", method, ifd, verbosity, small);
  DBusMessage *rsp = ConnectionBlockingSendReply(conn, msg, &err);
  assert (rsp != NULL);

  DBusMessageIter rsp_it;
  dbus_message_iter_init(rsp, &rsp_it);
  assert (dbus_message_iter_get_arg_type(&rsp_it) == DBUS_TYPE_INT32);
  dbus_message_iter_get_basic(&rsp_it, &vx);
  dbus_message_iter_next(&rsp_it);
  int retval = vx;
  api_("%s(%d, %d, %d) return %d <=", method, ifd, verbosity, small, retval);
  dbus_message_unref(rsp);
  DestroyConnection(conn);
  return retval;
}

const char *BZ2_bzlibVersion(void) {
  static const char *method = "BZ2_bzlibVersion";
  static const char *saved_version = NULL;
  if (saved_version) {
    api_("%s() return '%s' <= (saved)", method, saved_version);
    return saved_version;
  }

  struct DriverConnection *conn = CreateConnection();
  DBusMessage *msg = ConnectionNewRequest(conn, method);

  DBusError err;
  dbus_error_init(&err);
  api_("%s() =>", method);
  DBusMessage *rsp = ConnectionBlockingSendReply(conn, msg, &err);
  assert (rsp != NULL);

  DBusMessageIter rsp_it;
  dbus_message_iter_init(rsp, &rsp_it);
  assert (dbus_message_iter_get_arg_type(&rsp_it) == DBUS_TYPE_STRING);
  const char *retval;
  dbus_message_iter_get_basic(&rsp_it, &retval);
  dbus_message_iter_next(&rsp_it);
  api_("%s() return '%s' <=", method, retval);
  saved_version = strdup(retval);
  dbus_message_unref(rsp);
  DestroyConnection(conn);
  return saved_version;
}
