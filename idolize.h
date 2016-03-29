#ifndef _IDOLIZE_H
#define _IDOLIZE_H

#ifdef IDL_GENERATE

/* Function annotations */

/* Initialization function for API */
#define __init		__attribute__((annotate("idl:init")))
/* Termination function for API */
#define __term		__attribute__((annotate("idl:term")))
/* Don't include this function */
#define __skip		__attribute__((annotate("idl:skip")))
/* Function taking a filename that has an alternative version taking a file descriptor */
#define __fdalt(f)	__attribute__((annotate("idl:fdalt=" #f)))

/* Parameter and return value annotations */
/* C string, \0-terminated */
#define __cstring	__attribute__((annotate("idl:cstring")))
/* Pointer to memory with size (in bytes) given by:
 *  - the N-th parameter (counting from 1) if sz is an integer N
 *  - the field or parameter with name sz if sz is an identifier
 */
#define __size(sz)	__attribute__((annotate("idl:size=" #sz)))
/* Pointer to array of <paramN> entries */
#define __count(n)	__attribute__((annotate("idl:count=" #n)))
/* Pointer to freshly-allocated memory that the caller owns */
#define __move		__attribute__((annotate("idl:move")))
/* File descriptor */
#define __isfd		__attribute__((annotate("idl:fd")))
/* Parameter that is (just) an output value */
#define __out		__attribute__((annotate("idl:out")))
/* Pointer that is an opaque handle */
#define __handle	__attribute__((annotate("idl:handle")))
/* Pointer to static data */
#define __static	__attribute__((annotate("idl:static")))

#else

#define __init
#define __term
#define __skip
#define __fdalt(n)

#define __cstring
#define __size(sz)
#define __count(n)
#define __move
#define __isfd
#define __out
#define __handle
#define __static

#endif

#endif
