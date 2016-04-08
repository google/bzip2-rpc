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
public:
  Bz2Impl(int sock_fd) : Bz2::Server(), sock_fd_(sock_fd) {}
  kj::Promise<void> compressStream(CompressStreamContext context) override {
    static const char *method = "BZ2_bzCompressStream";
    auto msg = context.getParams();
    int ifd_nonce = msg.getIfd();
    int ifd = GetTransferredFd(sock_fd_, ifd_nonce);
    int ofd_nonce = msg.getOfd();
    int ofd = GetTransferredFd(sock_fd_, ofd_nonce);
    int blockSize100k = msg.getBlockSize100k();
    int verbosity = msg.getVerbosity();
    int workFactor = msg.getWorkFactor();
    api_("=> %s(%d, %d, %d, %d, %d)", method, ifd, ofd, blockSize100k, verbosity, workFactor);
    int retval = BZ2_bzCompressStream(ifd, ofd, blockSize100k, verbosity, workFactor);
    api_("=> %s(%d, %d, %d, %d, %d) return %d", method, ifd, ofd, blockSize100k, verbosity, workFactor, retval);
    auto rsp = context.getResults();
    rsp.setResult(retval);
    close(ifd);
    close(ofd);
    return kj::READY_NOW;
  }
  kj::Promise<void> decompressStream(DecompressStreamContext context) override {
    static const char *method = "BZ2_bzDecompressStream";
    auto msg = context.getParams();
    int ifd_nonce = msg.getIfd();
    int ifd = GetTransferredFd(sock_fd_, ifd_nonce);
    int ofd_nonce = msg.getOfd();
    int ofd = GetTransferredFd(sock_fd_, ofd_nonce);
    int verbosity = msg.getVerbosity();
    int small = msg.getSmall();
    api_("=> %s(%d, %d, %d, %d)", method, ifd, ofd, verbosity, small);
    int retval = BZ2_bzDecompressStream(ifd, ofd, verbosity, small);
    api_("=> %s(%d, %d, %d, %d) return %d", method, ifd, ofd, verbosity, small, retval);
    auto rsp = context.getResults();
    rsp.setResult(retval);
    close(ifd);
    close(ofd);
    return kj::READY_NOW;
  }
  kj::Promise<void> testStream(TestStreamContext context) override {
    static const char *method = "BZ2_bzTestStream";
    auto msg = context.getParams();
    int ifd_nonce = msg.getIfd();
    int ifd = GetTransferredFd(sock_fd_, ifd_nonce);
    int verbosity = msg.getVerbosity();
    int small = msg.getSmall();
    api_("=> %s(%d, %d, %d)", method, ifd, verbosity, small);
    int retval = BZ2_bzTestStream(ifd, verbosity, small);
    api_("=> %s(%d, %d, %d) return %d", method, ifd, verbosity, small, retval);
    auto rsp = context.getResults();
    rsp.setResult(retval);
    close(ifd);
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

private:
  int sock_fd_;
};

}  // namespace bz2

int main(int argc, char *argv[]) {
  signal(SIGSEGV, CrashHandler);
  signal(SIGABRT, CrashHandler);
  const char *fd_str = getenv("API_NONCE_FD");
  assert (fd_str != NULL);
  int sock_fd = atoi(fd_str);
  api_("'%s' program start, parent socket %d", argv[0], sock_fd);

  // Build the address of a UNIX socket for the service.
  const char *sockfile = tempnam(nullptr, "gsck");
  std::string server_address = "unix:";
  server_address += sockfile;
  log_("listening on %s", server_address.c_str());
  capnp::EzRpcServer server(kj::heap<bz2::Bz2Impl>(sock_fd), server_address);

  // Tell the parent the address we're listening on.
  uint32_t len = server_address.size() + 1;
  int rc;
  rc = write(sock_fd, &len, sizeof(len));
  assert (rc == sizeof(len));
  rc = write(sock_fd, server_address.c_str(), len);
  assert ((uint32_t)rc == len);

  // Main loop
  auto& waitScope = server.getWaitScope();
  kj::NEVER_DONE.wait(waitScope);  // Run forever
  api_("'%s' program stop", argv[0]);
  return 0;
}
