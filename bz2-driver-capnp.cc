// Copyright 2016 Google Inc. All Rights Reserved.
//
// Use of this source code is governed by the bzip2
// license that can be found in the LICENSE file.

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

#include "rpc-util.h"

#include <memory>
#include <capnp/ez-rpc.h>

#include "bzlib.capnp.h"

#include "bzlib.h"

int verbose = 4;

namespace bz2 {

class Bz2Impl final : public Bz2::Server {
  kj::Promise<void> compressStream(CompressStreamContext context) override {
    static const char *method = "BZ2_bzCompressStream";
    auto msg = context.getParams();
    int ifd = msg.getIfd();
    int ofd = msg.getOfd();
    int blockSize100k = msg.getBlockSize100k();
    int verbosity = msg.getVerbosity();
    int workFactor = msg.getWorkFactor();
    api_("=> %s(%d, %d, %d, %d, %d)", method, ifd, ofd, blockSize100k, verbosity, workFactor);
    int retval = BZ2_bzCompressStream(ifd, ofd, blockSize100k, verbosity, workFactor);
    api_("=> %s(%d, %d, %d, %d, %d) return %d", method, ifd, ofd, blockSize100k, verbosity, workFactor, retval);
    auto rsp = context.getResults();
    rsp.setResult(retval);
    return kj::READY_NOW;
  }
  kj::Promise<void> decompressStream(DecompressStreamContext context) override {
    static const char *method = "BZ2_bzDecompressStream";
    auto msg = context.getParams();
    int ifd = msg.getIfd();
    int ofd = msg.getOfd();
    int verbosity = msg.getVerbosity();
    int small = msg.getSmall();
    api_("=> %s(%d, %d, %d, %d)", method, ifd, ofd, verbosity, small);
    int retval = BZ2_bzDecompressStream(ifd, ofd, verbosity, small);
    api_("=> %s(%d, %d, %d, %d) return %d", method, ifd, ofd, verbosity, small, retval);
    auto rsp = context.getResults();
    rsp.setResult(retval);
    return kj::READY_NOW;
  }
  kj::Promise<void> testStream(TestStreamContext context) override {
    static const char *method = "BZ2_bzTestStream";
    auto msg = context.getParams();
    int ifd = msg.getIfd();
    int verbosity = msg.getVerbosity();
    int small = msg.getSmall();
    api_("=> %s(%d, %d, %d)", method, ifd, verbosity, small);
    int retval = BZ2_bzTestStream(ifd, verbosity, small);
    api_("=> %s(%d, %d, %d) return %d", method, ifd, verbosity, small, retval);
    auto rsp = context.getResults();
    rsp.setResult(retval);
    return kj::READY_NOW;
  }
  kj::Promise<void> libVersion(LibVersionContext context) override {
    static const char *method = "BZ2_bzlibVersion";
    api_("=> %s()", method);
    const char * retval = BZ2_bzlibVersion();
    api_("<= %s() return '%s'", method, retval);
    auto rsp = context.getResults();
    rsp.setVersion(retval);
    return kj::READY_NOW;
  }
};

}  // namespace bz2

int main(int argc, char *argv[]) {
  signal(SIGSEGV, CrashHandler);
  signal(SIGABRT, CrashHandler);
  api_("'%s' program start", argv[0]);

  // Listen on a UNIX socket
  const char *sockfile = tempnam(nullptr, "gsck");
  std::string server_address = "unix:";
  server_address += sockfile;
  log_("listening on %s", server_address.c_str());

  // Tell the parent the address we're listening on.
  uint32_t len = server_address.size() + 1;

  const char *fd_str = getenv("API_NONCE_FD");
  assert (fd_str != NULL);
  int sock_fd = atoi(fd_str);
  int rc;
  rc = write(sock_fd, &len, sizeof(len));
  assert (rc == sizeof(len));
  rc = write(sock_fd, server_address.c_str(), len);
  assert ((uint32_t)rc == len);
  close(sock_fd);

  capnp::EzRpcServer server(kj::heap<bz2::Bz2Impl>(),server_address);
  auto& waitScope = server.getWaitScope();
  kj::NEVER_DONE.wait(waitScope);  // Run forever
  api_("'%s' program stop", argv[0]);
  return 0;
}
