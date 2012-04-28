#include <stdio.h>
#include <sys/types.h>

int lc_available(void);

void lc_send_to_parent(const char *return_type, const char *function,
		       const char *arg_types, ...);

int lc_is_wrapped(void);

struct lc_capability {
  const char *name;
  void (*invoke)(int fd);
};

int lc_wrap_filter(int (*func)(FILE *in, FILE *out), FILE *in, FILE *out,
		   const struct lc_capability *caps, size_t ncaps);

void lc_write_void(int fd);
int lc_read_int(int fd, int *result);
int lc_read_string(int fd, char **result, uint32_t max);
