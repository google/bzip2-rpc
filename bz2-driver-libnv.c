#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <nv.h>

#include "rpc-util.h"
#include "bzlib.h"

int verbose = 4;

/* API-specfic message handler prototype */
nvlist_t *APIMessageHandler(const nvlist_t *msg);

static void MainLoop(int sock_fd) {
  while (1) {
    verbose_("blocking read from fd %d...", sock_fd);
    nvlist_t *msg = nvlist_recv(sock_fd, 0);
    verbose_("handle incoming request on fd %d...", sock_fd);
    nvlist_t *rsp = APIMessageHandler(msg);
    nvlist_destroy(msg);
    if (rsp) {
      verbose_("send response on fd %d", sock_fd);
      int rc = nvlist_send(sock_fd, rsp);
      if (rc != 0) {
        error_("failed to send response, %d", errno);
      }
      nvlist_destroy(rsp);
    }
  }
}

int main(int argc, char *argv[]) {
  signal(SIGSEGV, CrashHandler);
  signal(SIGABRT, CrashHandler);
  api_("'%s' program start", argv[0]);

  const char *fd_str = getenv("API_NONCE_FD");
  assert (fd_str != NULL);
  int sock_fd = atoi(fd_str);
  MainLoop(sock_fd);

  api_("'%s' program stop", argv[0]);
  return 0;
}


/*****************************************************************************/
/* Everything above here is generic, and would be useful for any remoted API */
/*****************************************************************************/

static int proxied_BZ2_bzCompressStream(const nvlist_t *msg, nvlist_t *rsp) {
  static const char *method = "BZ2_bzCompressStream";
  int ifd = nvlist_get_descriptor(msg, "ifd");
  int ofd = nvlist_get_descriptor(msg, "ofd");
  int blockSize100k = nvlist_get_number(msg, "blockSize100k");
  int verbosity = nvlist_get_number(msg, "verbosity");
  int workFactor = nvlist_get_number(msg, "workFactor");

  api_("=> %s(%d, %d, %d, %d, %d)", method, ifd, ofd, blockSize100k, verbosity, workFactor);
  int retval = BZ2_bzCompressStream(ifd, ofd, blockSize100k, verbosity, workFactor);

  api_("=> %s(%d, %d, %d, %d, %d) return %d", method, ifd, ofd, blockSize100k, verbosity, workFactor, retval);
  nvlist_add_number(rsp, "retval", retval);
  return 0;
}

static int proxied_BZ2_bzDecompressStream(const nvlist_t *msg, nvlist_t *rsp) {
  static const char *method = "BZ2_bzDecompressStream";
  int ifd = nvlist_get_descriptor(msg, "ifd");
  int ofd = nvlist_get_descriptor(msg, "ofd");
  int verbosity = nvlist_get_number(msg, "verbosity");
  int small = nvlist_get_number(msg, "small");

  api_("=> %s(%d, %d, %d, %d)", method, ifd, ofd, verbosity, small);
  int retval = BZ2_bzDecompressStream(ifd, ofd, verbosity, small);

  api_("=> %s(%d, %d, %d, %d) return %d", method, ifd, ofd, verbosity, small, retval);
  nvlist_add_number(rsp, "retval", retval);
  return 0;
}

static int proxied_BZ2_bzTestStream(const nvlist_t *msg, nvlist_t *rsp) {
  static const char *method = "BZ2_bzTestStream";
  int ifd = nvlist_get_descriptor(msg, "ifd");
  int verbosity = nvlist_get_number(msg, "verbosity");
  int small = nvlist_get_number(msg, "small");

  api_("=> %s(%d, %d, %d)", method, ifd, verbosity, small);
  int retval = BZ2_bzTestStream(ifd, verbosity, small);

  api_("=> %s(%d, %d, %d) return %d", method, ifd, verbosity, small, retval);
  nvlist_add_number(rsp, "retval", retval);
  return 0;
}

static int proxied_BZ2_bzlibVersion(const nvlist_t *msg, nvlist_t *rsp) {
  static const char *method = "BZ2_bzlibVersion";
  api_("=> %s()", method);
  const char * retval = BZ2_bzlibVersion();
  api_("=> %s()", method);
  nvlist_add_string(rsp, "retval", retval);
  return 0;
}

/* This is the general entrypoint for this specific API */
nvlist_t *APIMessageHandler(const nvlist_t *msg) {
  nvlist_t *rsp = nvlist_create(0);
  const char *cmd = nvlist_get_string(msg, "cmd");
  int rc;

  if (strcmp(cmd, "BZ2_bzCompressStream") == 0) {
    rc = proxied_BZ2_bzCompressStream(msg, rsp);
  } else if (strcmp(cmd, "BZ2_bzDecompressStream") == 0) {
    rc = proxied_BZ2_bzDecompressStream(msg, rsp);
  } else if (strcmp(cmd, "BZ2_bzTestStream") == 0) {
    rc = proxied_BZ2_bzTestStream(msg, rsp);
  } else if (strcmp(cmd, "BZ2_bzlibVersion") == 0) {
    rc = proxied_BZ2_bzlibVersion(msg, rsp);
  } else {
    rc = -1;
  }
  if (rc != 0) {
    nvlist_destroy(rsp);
    rsp = NULL;
  }
  return rsp;
}
