typedef unsigned char   Bool;
typedef char            Char;
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
#ifdef __GNUC__
#  define NORETURN __attribute__ ((noreturn))
#else
#  define NORETURN /**/
#endif

#define FILE_NAME_LEN 1034

/* in the child */
void wrapped_compressStream ( FILE *stream, FILE *zStream );
Bool wrapped_uncompressStream ( FILE *zStream, FILE *stream );
Bool wrapped_testStream ( FILE *zStream );

/* in the parent */
void wrapped_applySavedFileAttrToOutputFile ( IntNative fd );
void wrapped_clear_outputHandleJustInCase ( void );
void wrapped_configError ( void ) NORETURN;
void wrapped_outOfMemory ( void ) NORETURN;
void wrapped_ioError ( void ) NORETURN;
void wrapped_panic ( const Char* ) NORETURN;
void wrapped_crcError ( void ) NORETURN;
void wrapped_compressedStreamEOF ( void ) NORETURN;

/* in both */
extern Int32   _blockSize100k;
#define         blockSize100k (*(const Int32 *)&_blockSize100k)
extern Int32   _verbosity;
#define         verbosity (*(const Int32 *)&_verbosity)
extern Int32   _workFactor;
#define         workFactor (*(const Int32 *)&_workFactor)
extern Bool    _smallMode;
#define         smallMode (*(const Bool *)&_smallMode)
extern Bool    _forceOverwrite;
#define         forceOverwrite (*(const Bool *)&_forceOverwrite)
extern Bool    _noisy;
#define         noisy (*(const Bool *)&_noisy)
extern Char   *_progName;
#define         progName (*(Char * const *)_progName)
extern Char    _inName [FILE_NAME_LEN];
#define         inName (*(Char * const *)_inName)

