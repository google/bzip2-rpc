typedef unsigned char   Bool;
typedef unsigned char   UChar;
typedef int             Int32;
typedef unsigned int    UInt32;

#define True  ((Bool)1)
#define False ((Bool)0)

#define SET_BINARY_MODE(fd) /**/

void wrapped_compressStream ( FILE *stream, FILE *zStream );
Bool wrapped_uncompressStream ( FILE *zStream, FILE *stream );
Bool wrapped_testStream ( FILE *zStream );

extern Int32 _blockSize100k;
#define       blockSize100k (*(const Int32 *)&_blockSize100k)

