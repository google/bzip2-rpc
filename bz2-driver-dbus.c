/* Copyright 2016 Google Inc. All Rights Reserved.
 *
 * Use of this source code is governed by the bzip2
 * license that can be found in the LICENSE file. */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <assert.h>
#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <dbus/dbus.h>

#include "rpc-util.h"
#include "bzlib.h"

int _rpc_verbose = 4;
int _rpc_indent = 4;

static int pollfd_size = 0;  // Num allocated
static int pollfd_count = 0;  // Num used
static struct pollfd *pollfds = NULL;
static DBusWatch **watches = NULL;

static int conn_size = 0;  // Num allocated
static int conn_count = 0;  // Num used
static DBusConnection **conns = NULL;

static int FindWatch(DBusWatch *watch) {
  int ii;
  for (ii = 0; ii < pollfd_count; ii++) {
    if (watches[ii] == watch) return ii;
  }
  return -1;
}

static int FlagsToEvents(int flags) {
  int events = POLLERR | POLLHUP;
  if (flags & DBUS_WATCH_READABLE) events |= POLLIN;
  if (flags & DBUS_WATCH_WRITABLE) events |= POLLOUT;
  return events;
}

static dbus_bool_t AddWatch(DBusWatch *watch, void *data) {
  if (pollfd_count == pollfd_size) {
    pollfd_size *= 2;
    struct pollfd *p = realloc(pollfds, pollfd_size*sizeof(struct pollfd));
    DBusWatch **w = realloc(watches, pollfd_size*sizeof(DBusWatch*));
    if (p == NULL || w == NULL) {
      error_("!!! failed to alloc extra space for watches");
      free(p);
      free(w);
      return FALSE;
    }
    watches = w;
    pollfds = p;
  }
  watches[pollfd_count] = watch;
  struct pollfd *pdf = &(pollfds[pollfd_count]);
  pollfd_count++;
  pdf->fd = dbus_watch_get_unix_fd(watch);
  pdf->events = (dbus_watch_get_enabled(watch) ?
                 FlagsToEvents(dbus_watch_get_flags(watch)): 0);
  verbose_("Add watch on watch=%p fd=%d events=%s%s", watch, pdf->fd,
           (pdf->events & POLLIN) ? "R" : "",
           (pdf->events & POLLOUT) ? "W" : "");
  return TRUE;
}

static void RemoveWatch(DBusWatch *watch, void *data) {
  verbose_("Remove watch on watch=%p", watch);
  int ii = FindWatch(watch);
  if (ii == -1) {
    warning_("Failed to find removed watch %p", watch);
    return;
  }
  // Move up
  pollfd_count--;
  if (ii < pollfd_count) {
    memmove(&(pollfds[ii]), &(pollfds[ii+1]),
            (pollfd_count - ii)*sizeof(struct pollfd));
    memmove(&(watches[ii]), &(watches[ii+1]),
            (pollfd_count - ii)*sizeof(DBusWatch *));
  }
}

static void ToggleWatch(DBusWatch *watch, void *data) {
  int ii = FindWatch(watch);
  struct pollfd *pdf = &(pollfds[ii]);
  if (ii == -1) {
    warning_("Failed to find toggled watch %p", watch);
    return;
  }
  pdf->events = (dbus_watch_get_enabled(watch) ?
                 FlagsToEvents(dbus_watch_get_flags(watch)): 0);
  verbose_("Toggle watch on watch=%p fd=%d events=%s%s", watch, pdf->fd,
           (pdf->events & POLLIN) ? "R" : "",
           (pdf->events & POLLOUT) ? "W" : "");
}


// @@@ do something with timeouts
static dbus_bool_t AddTimeout(DBusTimeout *timeout, void *data) {
  warning_("Add timeout");
  return TRUE;
}

static void RemoveTimeout(DBusTimeout *timeout, void *data) {
  warning_("Remove timeout");
}

static void ToggleTimeout(DBusTimeout *timeout, void *data) {
  warning_("Toggle timeout");
}

static void DispatchStatus(DBusConnection *conn, DBusDispatchStatus status, void *data) {
  verbose_("DispatchStatus callback on conn=%p with status %d %s", conn, status,
           status == DBUS_DISPATCH_DATA_REMAINS ? "DATA_REMAINS" :
           status == DBUS_DISPATCH_COMPLETE ? "COMPLETE" :
           status == DBUS_DISPATCH_NEED_MEMORY ? "NEED_MEMORY" : "<unknown>");
  if (status == DBUS_DISPATCH_DATA_REMAINS) {
    // Can't do dbus_connection_dispatch(conn); here!
    // Flag that this conn needs dispatch in main loop
  }
}

static void NoOpUnregister(DBusConnection *conn, void *data) {
  verbose_("NoOpUnregister(conn=%p)", conn);
}

static DBusHandlerResult NoopHandler(DBusConnection *conn, DBusMessage *msg) {
  DBusMessage *rsp = dbus_message_new_method_return(msg);
  if (!rsp) {
    warning_("failed to get response message");
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }
  if (!dbus_connection_send(conn, rsp, NULL)) {
    warning_("dbus_connection_send failed for reply");
    dbus_message_unref(rsp);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }
  dbus_connection_flush(conn);
  dbus_message_unref(rsp);
  return DBUS_HANDLER_RESULT_HANDLED;
}

/* API-specfic message handler prototype */
DBusHandlerResult APIMessageHandler(const char *method, DBusConnection *conn, DBusMessage *msg, void *data);

static DBusHandlerResult MessageHandler(DBusConnection *conn, DBusMessage *msg, void *data) {
  const char *objpath = (const char *)data;
  const char *interface = dbus_message_get_interface(msg);
  const char *path = dbus_message_get_path(msg);
  const char *method = dbus_message_get_member(msg);
  verbose_("MessageHandler(conn=%p, msg{if='%s', member='%s', path='%s'})",
           conn, interface ? interface : "<null>", method, path);
  if (strcmp(path, objpath) != 0) {
    error_("Unexpected path! drop connection");
    dbus_connection_close(conn);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }
  if (strcmp(method, "__noop") == 0) {
    return NoopHandler(conn, msg);
  }
  return APIMessageHandler(method, conn, msg, data);
}

static DBusObjectPathVTable vt = {
  .unregister_function = NoOpUnregister,
  .message_function = MessageHandler
};


static void MainLoop(const char* objpath) {
  int rc;
  int ii;
  for (ii = 0; ii < pollfd_count; ii++) pollfds[ii].revents = 0;

  while (1) {
    verbose_("poll on fds: ");
    for (ii = 0; ii < pollfd_count; ii++) {
      verbose_("  %d%s%s", pollfds[ii].fd,
               (pollfds[ii].events & POLLIN) ? "R" : "",
               (pollfds[ii].events & POLLOUT) ? "W" : "");
    }
    rc = poll(pollfds, pollfd_count, -1);
    if (rc < 0) {
      warning_("poll() failed with errno=%d", errno);
      return;
    }
    if (rc > 0) {
      for (ii = 0; ii < pollfd_count; ii++) {
        if (pollfds[ii].revents) {
          verbose_("[%d] fd=%d event=%s%s%s%s watch=%p",
                   ii, pollfds[ii].fd,
                   (pollfds[ii].revents & POLLIN) ? "R" : "",
                   (pollfds[ii].revents & POLLOUT) ? "W" : "",
                   (pollfds[ii].revents & POLLERR) ? "!" : "",
                   (pollfds[ii].revents & POLLHUP) ? "0" : "",
                   watches[ii]);
          unsigned int flags = 0;
          if (pollfds[ii].revents & POLLIN)  flags |= DBUS_WATCH_READABLE;
          if (pollfds[ii].revents & POLLOUT) flags |= DBUS_WATCH_WRITABLE;
          if (pollfds[ii].revents & POLLERR) flags |= DBUS_WATCH_ERROR;
          if (pollfds[ii].revents & POLLHUP) flags |= DBUS_WATCH_HANGUP;
          if (!dbus_watch_handle(watches[ii], flags)) {
            warning_("dbus_watch_handle failed");
          }
        }
      }
      for (ii = 0; ii < conn_count; ii++) {
        if (!dbus_connection_get_is_connected(conns[ii])) {
          warning_("Drop connection [%d] conn=%p", ii, conns[ii]);
          dbus_connection_unref(conns[ii]);
          conn_count--;
          if (ii < conn_count) {
            memmove(&(conns[ii]), &(conns[ii+1]),
                    (conn_count - ii)*sizeof(DBusConnection *));
          }
          ii--;
        } else {
          verbose_("Dispatch connection [%d] conn=%p", ii, conns[ii]);
          while (dbus_connection_dispatch(conns[ii]) == DBUS_DISPATCH_DATA_REMAINS)
            ;
        }
      }
    }
  }
}

/* Callback for a new connection being accepted on the listening socket */
static void NewConnection(DBusServer *server, DBusConnection *conn, void *data) {
  const char *objpath = (const char *)data;
  if (conn_count == conn_size) {
    conn_size *= 2;
    DBusConnection **c = realloc(watches, pollfd_size*sizeof(DBusConnection*));
    if (c == NULL) {
      error_("!!! failed to alloc extra space for connections");
      dbus_connection_close(conn);
      return;
    }
    conns = c;
  }
  log_("New connection [%d] conn=%p", conn_count, conn);
  conns[conn_count++] = conn;
  dbus_connection_ref(conn);

  dbus_connection_set_allow_anonymous(conn, FALSE);
  dbus_connection_set_dispatch_status_function(conn, DispatchStatus, NULL, NULL);
  if (!dbus_connection_set_watch_functions(conn, AddWatch, RemoveWatch, ToggleWatch, NULL, NULL)) {
    error_("!!! failed to set watch functions");
  }
  if (!dbus_connection_set_timeout_functions(conn, AddTimeout, RemoveTimeout, ToggleTimeout, NULL, NULL)) {
    error_("!!! failed to set timeout functions");
  }

  verbose_("register_object_path(conn=%p, '%s')", conn, objpath);
  if (!dbus_connection_register_object_path(conn, objpath, &vt, (void *)objpath)) {
    error_("!!! failed to register object path %s", objpath);
  }

  verbose_("connection is_auth=%s is_anon=%s",
           dbus_connection_get_is_authenticated(conn) ? "Y" : "N",
           dbus_connection_get_is_anonymous(conn) ? "Y" : "N");

  if (dbus_connection_get_dispatch_status(conn) != DBUS_DISPATCH_COMPLETE) {
    verbose_("Dispatch connection %p", conn);
    while (dbus_connection_dispatch(conn) == DBUS_DISPATCH_DATA_REMAINS)
      ;
  }
}

int main(int argc, char *argv[]) {
  signal(SIGSEGV, CrashHandler);
  signal(SIGABRT, CrashHandler);
  const char *fd_str = getenv("API_NONCE_FD");
  assert (fd_str != NULL);
  int sock_fd = atoi(fd_str);
  api_("'%s' program start, parent socket %d", argv[0], sock_fd);
  pollfd_size = 4;
  pollfds = calloc(pollfd_size, sizeof(struct pollfd));
  watches = calloc(pollfd_size, sizeof(DBusWatch*));
  conn_size = 2;
  conns = calloc(conn_size, sizeof(DBusConnection*));
  if (!pollfds || !watches || !conns) {
    error_("!!! failed to get initial memory");
    exit(1);
  }

  /* Listen on a UNIX socket in /tmp */
  DBusError err;
  dbus_error_init(&err);
  const char *dbus_addr = "unix:tmpdir=/tmp";
  DBusServer *server = dbus_server_listen(dbus_addr, &err);
  if (!server) {
    error_("dbus_server_listen(%s) failed, %s: %s", dbus_addr, err.name, err.message);
    return 1;
  }

  const char *server_address = dbus_server_get_address(server);
  log_("listening on %s", server_address);

  /* Tell the parent the address we're listening on, plus a nonce that is only
   * known to us and the parent. */
  uint32_t len = strlen(server_address) + 1;
  srand(time(NULL));
  uint64_t nonce = rand();
  verbose_("len=%d, addr='%s', nonce='%ld'", len, server_address, nonce);

  int rc;
  rc = write(sock_fd, &len, sizeof(len));
  assert (rc == sizeof(len));
  rc = write(sock_fd, server_address, len);
  assert (rc == len);
  rc = write(sock_fd, &nonce, sizeof(nonce));
  assert (rc == sizeof(nonce));
  close(sock_fd);

  char objpath[] = "/nonce/xxxxxxxxxxx";
  sprintf(objpath, "/nonce/%ld", nonce);
  dbus_server_set_new_connection_function(server, NewConnection, objpath, NULL);
  dbus_server_set_watch_functions(server, AddWatch, RemoveWatch, ToggleWatch, NULL, NULL);
  dbus_server_set_timeout_functions(server, AddTimeout, RemoveTimeout, ToggleTimeout, NULL, NULL);
  const char *mechanisms[] = {"EXTERNAL", NULL};
  dbus_server_set_auth_mechanisms(server, mechanisms);
  MainLoop(objpath);

  api_("'%s' program stop", argv[0]);
  return 0;
}


/*****************************************************************************/
/* Everything above here is generic, and would be useful for any remoted API */
/*****************************************************************************/

static DBusHandlerResult proxied_BZ2_bzCompressStream(DBusConnection *conn, DBusMessage *msg) {
  static const char *method = "BZ2_bzCompressStream";
  DBusMessage *rsp = dbus_message_new_method_return(msg);
  if (!rsp) {
    warning_("failed to get response message");
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }
  DBusMessageIter rsp_it;
  dbus_message_iter_init_append(rsp, &rsp_it);

  int ifd;
  int ofd;
  int blockSize100k;
  int verbosity;
  int workFactor;
  DBusMessageIter msg_it;
  dbus_message_iter_init(msg, &msg_it);
  dbus_int32_t vx;
  assert (dbus_message_iter_get_arg_type(&msg_it) == DBUS_TYPE_UNIX_FD);
  dbus_message_iter_get_basic(&msg_it, &vx);
  dbus_message_iter_next(&msg_it);
  ifd = vx;
  assert (dbus_message_iter_get_arg_type(&msg_it) == DBUS_TYPE_UNIX_FD);
  dbus_message_iter_get_basic(&msg_it, &vx);
  dbus_message_iter_next(&msg_it);
  ofd = vx;
  assert (dbus_message_iter_get_arg_type(&msg_it) == DBUS_TYPE_INT32);
  dbus_message_iter_get_basic(&msg_it, &vx);
  dbus_message_iter_next(&msg_it);
  blockSize100k = vx;
  assert (dbus_message_iter_get_arg_type(&msg_it) == DBUS_TYPE_INT32);
  dbus_message_iter_get_basic(&msg_it, &vx);
  dbus_message_iter_next(&msg_it);
  verbosity = vx;
  assert (dbus_message_iter_get_arg_type(&msg_it) == DBUS_TYPE_INT32);
  dbus_message_iter_get_basic(&msg_it, &vx);
  dbus_message_iter_next(&msg_it);
  workFactor = vx;

  api_("=> %s(%d, %d, %d, %d, %d)", method, ifd, ofd, blockSize100k, verbosity, workFactor);
  int retval = BZ2_bzCompressStream(ifd, ofd, blockSize100k, verbosity, workFactor);

  api_("=> %s(%d, %d, %d, %d, %d) return %d", method, ifd, ofd, blockSize100k, verbosity, workFactor, retval);
  vx = retval;
  dbus_message_iter_append_basic(&rsp_it, DBUS_TYPE_INT32, &vx);

  if (!dbus_connection_send(conn, rsp, NULL)) {
    warning_("dbus_connection_send failed for reply");
    dbus_message_unref(rsp);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }
  dbus_connection_flush(conn);
  dbus_message_unref(rsp);
  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult proxied_BZ2_bzDecompressStream(DBusConnection *conn, DBusMessage *msg) {
  static const char *method = "BZ2_bzDecompressStream";
  DBusMessage *rsp = dbus_message_new_method_return(msg);
  if (!rsp) {
    warning_("failed to get response message");
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }
  DBusMessageIter rsp_it;
  dbus_message_iter_init_append(rsp, &rsp_it);

  int ifd;
  int ofd;
  int verbosity;
  int small;
  DBusMessageIter msg_it;
  dbus_message_iter_init(msg, &msg_it);
  dbus_int32_t vx;
  assert (dbus_message_iter_get_arg_type(&msg_it) == DBUS_TYPE_UNIX_FD);
  dbus_message_iter_get_basic(&msg_it, &vx);
  dbus_message_iter_next(&msg_it);
  ifd = vx;
  assert (dbus_message_iter_get_arg_type(&msg_it) == DBUS_TYPE_UNIX_FD);
  dbus_message_iter_get_basic(&msg_it, &vx);
  dbus_message_iter_next(&msg_it);
  ofd = vx;
  assert (dbus_message_iter_get_arg_type(&msg_it) == DBUS_TYPE_INT32);
  dbus_message_iter_get_basic(&msg_it, &vx);
  dbus_message_iter_next(&msg_it);
  verbosity = vx;
  assert (dbus_message_iter_get_arg_type(&msg_it) == DBUS_TYPE_INT32);
  dbus_message_iter_get_basic(&msg_it, &vx);
  dbus_message_iter_next(&msg_it);
  small = vx;

  api_("=> %s(%d, %d, %d, %d)", method, ifd, ofd, verbosity, small);
  int retval = BZ2_bzDecompressStream(ifd, ofd, verbosity, small);

  api_("=> %s(%d, %d, %d, %d) return %d", method, ifd, ofd, verbosity, small, retval);
  vx = retval;
  dbus_message_iter_append_basic(&rsp_it, DBUS_TYPE_INT32, &vx);

  if (!dbus_connection_send(conn, rsp, NULL)) {
    warning_("dbus_connection_send failed for reply");
    dbus_message_unref(rsp);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }
  dbus_connection_flush(conn);
  dbus_message_unref(rsp);
  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult proxied_BZ2_bzTestStream(DBusConnection *conn, DBusMessage *msg) {
  static const char *method = "BZ2_bzTestStream";
  DBusMessage *rsp = dbus_message_new_method_return(msg);
  if (!rsp) {
    warning_("failed to get response message");
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }
  DBusMessageIter rsp_it;
  dbus_message_iter_init_append(rsp, &rsp_it);

  int ifd;
  int verbosity;
  int small;
  DBusMessageIter msg_it;
  dbus_message_iter_init(msg, &msg_it);
  dbus_int32_t vx;
  assert (dbus_message_iter_get_arg_type(&msg_it) == DBUS_TYPE_UNIX_FD);
  dbus_message_iter_get_basic(&msg_it, &vx);
  dbus_message_iter_next(&msg_it);
  ifd = vx;
  assert (dbus_message_iter_get_arg_type(&msg_it) == DBUS_TYPE_INT32);
  dbus_message_iter_get_basic(&msg_it, &vx);
  dbus_message_iter_next(&msg_it);
  verbosity = vx;
  assert (dbus_message_iter_get_arg_type(&msg_it) == DBUS_TYPE_INT32);
  dbus_message_iter_get_basic(&msg_it, &vx);
  dbus_message_iter_next(&msg_it);
  small = vx;

  api_("=> %s(%d, %d, %d)", method, ifd, verbosity, small);
  int retval = BZ2_bzTestStream(ifd, verbosity, small);

  api_("=> %s(%d, %d, %d) return %d", method, ifd, verbosity, small, retval);
  vx = retval;
  dbus_message_iter_append_basic(&rsp_it, DBUS_TYPE_INT32, &vx);

  if (!dbus_connection_send(conn, rsp, NULL)) {
    warning_("dbus_connection_send failed for reply");
    dbus_message_unref(rsp);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }
  dbus_connection_flush(conn);
  dbus_message_unref(rsp);
  return DBUS_HANDLER_RESULT_HANDLED;
}
static DBusHandlerResult proxied_BZ2_bzlibVersion(DBusConnection *conn, DBusMessage *msg) {
  static const char *method = "BZ2_bzlibVersion";
  DBusMessage *rsp = dbus_message_new_method_return(msg);
  if (!rsp) {
    warning_("failed to get response message");
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }
  DBusMessageIter rsp_it;
  dbus_message_iter_init_append(rsp, &rsp_it);

  DBusMessageIter msg_it;
  dbus_message_iter_init(msg, &msg_it);

  api_("=> %s()", method);
  const char * retval = BZ2_bzlibVersion();

  api_("<= %s() return '%s'", method, retval);
  dbus_message_iter_append_basic(&rsp_it, DBUS_TYPE_STRING, &retval);

  if (!dbus_connection_send(conn, rsp, NULL)) {
    warning_("dbus_connection_send failed for reply");
    dbus_message_unref(rsp);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }
  dbus_connection_flush(conn);
  dbus_message_unref(rsp);
  return DBUS_HANDLER_RESULT_HANDLED;
}

/* This is the general entrypoint for this specific API */
DBusHandlerResult APIMessageHandler(const char *method, DBusConnection *conn, DBusMessage *msg, void *data) {
  if (strcmp(method, "BZ2_bzCompressStream") == 0) {
    return proxied_BZ2_bzCompressStream(conn, msg);
  } else if (strcmp(method, "BZ2_bzDecompressStream") == 0) {
    return proxied_BZ2_bzDecompressStream(conn, msg);
  } else if (strcmp(method, "BZ2_bzTestStream") == 0) {
    return proxied_BZ2_bzTestStream(conn, msg);
  } else if (strcmp(method, "BZ2_bzlibVersion") == 0) {
    return proxied_BZ2_bzlibVersion(conn, msg);
  } else {
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }
}
