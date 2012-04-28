// Candidates for libcapsicum

#include "capsicum.h"

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/capability.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

int lc_available(void) {
   static int known;
   static int available;

   if (!known) {
      known = 1;
      cap_rights_t rights;
      if (cap_getrights(0, &rights) == 0 || errno != ENOSYS)
	 available = 1;
   }
   return available;
}

static int lc_wrapped;
int lc_is_wrapped(void) { return lc_wrapped; }

static void lc_panic(const char * const msg) {
  static int panicking;

  if (lc_is_wrapped() && !panicking) {
    panicking = 1;
    lc_send_to_parent("void", "lc_panic", "int string", errno, msg);
    exit(0);
  }
  fprintf(stderr, "lc_panic: %s\n", msg);
  perror("lc_panic");
  exit(1);
}

static
void lc_closeallbut(const int *fds, const int nfds) {
  struct rlimit rl;
  int fd;
  int n;

  if (getrlimit(RLIMIT_NOFILE, &rl) < 0)
    lc_panic("Can't getrlimit");

  for (fd = 0; fd < rl.rlim_max; ++fd) {
    for (n = 0; n < nfds; ++n)
      if (fds[n] == fd)
	goto next;
    if (close(fd) < 0 && errno != EBADF) {
      lc_panic("Can't close");
    }
  next:
    continue;
  }
}   

static int lc_limitfd(int fd, cap_rights_t rights)
{
  int fd_cap;
  int error;
  
  fd_cap = cap_new(fd, rights);
  if (fd_cap < 0)
    return -1;
  if (dup2(fd_cap, fd) < 0) {
    error = errno;
    close(fd_cap);
    errno = error;
    return -1;
  }
  close(fd_cap);
  return 0;
}

static int lc_parent_fd;

static void lc_write_string(int fd, const char *string) {
  uint32_t size = strlen(string);
  if (write(fd, &size, sizeof size) != sizeof size)
    lc_panic("write failed");
  if (write(fd, string, size) != size)
    lc_panic("write failed");
}

static void lc_write_int(int fd, int n) {
  if (write(fd, &n, sizeof n) != sizeof n)
    lc_panic("write_int failed");
}

void lc_write_void(int fd) {
  lc_write_int(fd, 0xdeadbeef);
}

static size_t lc_full_read(int fd, void *buffer, size_t count) {
  size_t n;

  for (n = 0; n < count; ) {
    ssize_t r = read(fd, (char *)buffer + n, count - n);
    if (r == 0)
      return 0;
    if (r < 0)
      lc_panic("full_read");
    n += r;
  }
  return n;
}

int lc_read_string(int fd, char **result, uint32_t max) {
  uint32_t size;

  // FIXME: check for errors
  if (lc_full_read(fd, &size, sizeof size) != sizeof size)
    return 0;
  fprintf(stderr, "Read string size %d\n", size);
  if (size > max)
    lc_panic("oversized string read");
  *result = malloc(size + 1);
  size_t n = lc_full_read(fd, *result, size);
  if (n != size)
    lc_panic("string read failed");
  (*result)[size] = '\0';
  return 1;
}

int lc_read_int(int fd, int *result) {
  if (lc_full_read(fd, result, sizeof *result) != sizeof *result)
    return 0;
  return 1;
}

static int lc_read_void(int fd) {
  unsigned v;

  if (!lc_read_int(fd, &v))
    return 0;
  assert(v == 0xdeadbeef);
  return 1;
}

void lc_send_to_parent(const char * const return_type,
		       const char * const function,
		       const char * const arg_types,
		       ...) {
  va_list ap;

  assert(lc_is_wrapped());
  va_start(ap, arg_types);
  fprintf(stderr, "Send: %s\n", function);
  lc_write_string(lc_parent_fd, function);
  if (!strcmp(arg_types, "int"))
    lc_write_int(lc_parent_fd, va_arg(ap, int));
  else if (!strcmp(arg_types, "void"))
    /* do nothing */;
  else
    assert(!"unknown arg_types");
  assert(!strcmp(return_type, "void"));
  lc_read_void(lc_parent_fd);
}

static void lc_process_messages(int fd, const struct lc_capability *caps,
				size_t ncaps) {
  for ( ; ; ) {
    char *name;
    size_t n;

    if (!lc_read_string(fd, &name, 100))
      return;

    for (n = 0; n < ncaps; ++n)
      if (!strcmp(caps[n].name, name)) {
	caps[n].invoke(fd);
	goto done;
      }

    fprintf(stderr, "Can't process capability \"%s\"\n", name);
    lc_panic("bad capability");
  done:
    continue;
  }
}

// FIXME: do this some other way, since we can't stop the child from
// using our code.
#define FILTER_EXIT  123
int lc_wrap_filter(int (*func)(FILE *in, FILE *out), FILE *in, FILE *out,
		   const struct lc_capability *caps, size_t ncaps) {
  int ifd,  ofd, pid, status;
  int pfds[2];

  ifd = fileno(in);
  ofd = fileno(out);

  if (pipe(pfds) < 0)
    lc_panic("Cannot pipe");

  if ((pid = fork()) < 0)
    lc_panic("Cannot fork");

  if (pid != 0) {
    /* Parent process */
    close(pfds[1]);
    lc_process_messages(pfds[0], caps, ncaps);

    wait(&status);
    if(WIFEXITED(status)) {
      status = WEXITSTATUS(status);
      if (status != 0 && status != FILTER_EXIT)
	lc_panic("Unexpected child status");
      return status == 0;
    } else {
      lc_panic("Child exited abnormally");
    }
  } else { 
    /* Child process */
    int fds[4];

    lc_wrapped = 1;
    close(pfds[0]);
    lc_parent_fd = pfds[1];
    if(lc_limitfd(ifd, CAP_READ | CAP_SEEK) < 0
       || lc_limitfd(ofd, CAP_WRITE | CAP_SEEK) < 0
       // FIXME: CAP_SEEK should not be needed!
       || lc_limitfd(lc_parent_fd, CAP_READ | CAP_WRITE | CAP_SEEK) < 0) {
      lc_panic("Cannot limit descriptors");
    }
    fds[0] = 2;
    fds[1] = ofd;
    fds[2] = ifd;
    fds[3] = lc_parent_fd;
    lc_closeallbut(fds, 4);

    if (lc_available() && cap_enter() < 0)
      lc_panic("cap_enter() failed");

    if((*func)(in, out)) {
      exit(0);
    } else {
      exit(FILTER_EXIT);
    }
    /* NOTREACHED */
  }
  /* NOTREACHED */
  return 0;
}
