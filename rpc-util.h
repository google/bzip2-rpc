#ifndef _RPC_UTIL_H
#define _RPC_UTIL_H

/* Logging & other utilities */
#include <stdarg.h>

extern int verbose;

#ifdef __cplusplus
extern "C" {
#endif

void _log_at(int level, const char *filename, int lineno, const char *format, ...);
void CrashHandler(int sig);
int OpenDriver(const char* filename);

#ifdef __cplusplus
}
#endif

#define verbose_(...) _log_at(0, __FILE__, __LINE__, __VA_ARGS__)
#define log_(...)     _log_at(1, __FILE__, __LINE__, __VA_ARGS__)
#define api_(...)     _log_at(2, __FILE__, __LINE__, __VA_ARGS__)
#define warning_(...) _log_at(3, __FILE__, __LINE__, __VA_ARGS__)
#define error_(...)   _log_at(4, __FILE__, __LINE__, __VA_ARGS__)
#define fatal_(...)   _log_at(10,__FILE__, __LINE__, __VA_ARGS__)

#endif
