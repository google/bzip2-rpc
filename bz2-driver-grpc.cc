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
#include <grpc++/grpc++.h>

#include "bzlib.grpc.pb.h"
#include "bzlib.h"

int _rpc_verbose = 4;
int _rpc_indent = 4;

namespace bz2 {

class Bz2ServiceImpl final : public Bz2::Service {
public:
  Bz2ServiceImpl(int sock_fd) : Service(), sock_fd_(sock_fd) {}
  grpc::Status CompressStream(grpc::ServerContext* context,
                              const CompressStreamRequest* msg,
                              CompressStreamReply* rsp) {
    static const char *method = "BZ2_bzCompressStream";
    int ifd_nonce = msg->ifd();
    int ifd = GetTransferredFd(sock_fd_, ifd_nonce);
    int ofd_nonce = msg->ofd();
    int ofd = GetTransferredFd(sock_fd_, ofd_nonce);
    int blockSize100k = msg->blocksize100k();
    int verbosity = msg->verbosity();
    int workFactor = msg->workfactor();
    api_("=> %s(%d, %d, %d, %d, %d)", method, ifd, ofd, blockSize100k, verbosity, workFactor);
    int retval = BZ2_bzCompressStream(ifd, ofd, blockSize100k, verbosity, workFactor);
    api_("=> %s(%d, %d, %d, %d, %d) return %d", method, ifd, ofd, blockSize100k, verbosity, workFactor, retval);
    rsp->set_result(retval);
    close(ifd);
    close(ofd);
    return grpc::Status::OK;
  }
  grpc::Status DecompressStream(grpc::ServerContext* context,
                                const DecompressStreamRequest* msg,
                                DecompressStreamReply* rsp) {
    static const char *method = "BZ2_bzDecompressStream";
    int ifd_nonce = msg->ifd();
    int ifd = GetTransferredFd(sock_fd_, ifd_nonce);
    int ofd_nonce = msg->ofd();
    int ofd = GetTransferredFd(sock_fd_, ofd_nonce);
    int verbosity = msg->verbosity();
    int small = msg->small();
    api_("=> %s(%d, %d, %d, %d)", method, ifd, ofd, verbosity, small);
    int retval = BZ2_bzDecompressStream(ifd, ofd, verbosity, small);
    api_("=> %s(%d, %d, %d, %d) return %d", method, ifd, ofd, verbosity, small, retval);
    rsp->set_result(retval);
    close(ifd);
    close(ofd);
    return grpc::Status::OK;
  }
  grpc::Status TestStream(grpc::ServerContext* context,
                          const TestStreamRequest* msg,
                          TestStreamReply* rsp) {
    static const char *method = "BZ2_bzTestStream";
    int ifd_nonce = msg->ifd();
    int ifd = GetTransferredFd(sock_fd_, ifd_nonce);
    int verbosity = msg->verbosity();
    int small = msg->small();
    api_("=> %s(%d, %d, %d)", method, ifd, verbosity, small);
    int retval = BZ2_bzTestStream(ifd, verbosity, small);
    api_("=> %s(%d, %d, %d) return %d", method, ifd, verbosity, small, retval);
    rsp->set_result(retval);
    close(ifd);
    return grpc::Status::OK;
  }
  grpc::Status LibVersion(grpc::ServerContext* context,
                          const LibVersionRequest* msg,
                          LibVersionReply* rsp) {
    static const char *method = "BZ2_bzlibVersion";
    api_("=> %s()", method);
    const char * retval = BZ2_bzlibVersion();
    rsp->set_version(retval);
    api_("<= %s() return '%s'", method, retval);
    return grpc::Status::OK;
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

  grpc::ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  bz2::Bz2ServiceImpl service(sock_fd);
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());

  // Tell the parent the address we're listening on.
  uint32_t len = server_address.size() + 1;
  int rc;
  rc = write(sock_fd, &len, sizeof(len));
  assert (rc == sizeof(len));
  rc = write(sock_fd, server_address.c_str(), len);
  assert ((uint32_t)rc == len);

  // Main loop
  server->Wait();

  api_("'%s' program stop", argv[0]);
  return 0;
}
