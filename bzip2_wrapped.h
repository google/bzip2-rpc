typedef unsigned char   Bool;
typedef unsigned char   UChar;
typedef int             Int32;
typedef unsigned int    UInt32;

/*--
  IntNative is your platform's `native' int size.
  Only here to avoid probs with 64-bit platforms.
--*/
typedef int IntNative;

#define True  ((Bool)1)
#define False ((Bool)0)

#define SET_BINARY_MODE(fd) /**/

/* in the child */
void wrapped_compressStream ( FILE *stream, FILE *zStream );
Bool wrapped_uncompressStream ( FILE *zStream, FILE *stream );
Bool wrapped_testStream ( FILE *zStream );

/* in the parent */
void wrapped_applySavedFileAttrToOutputFile ( IntNative fd );
void wrapped_clear_outputHandleJustInCase( void );

/* in both */
extern Int32   _blockSize100k;
#define         blockSize100k (*(const Int32 *)&_blockSize100k)
extern Int32   _verbosity;
#define         verbosity (*(const Int32 *)&_verbosity)
extern Int32   _workFactor;
#define         workFactor (*(const Int32 *)&_workFactor)
