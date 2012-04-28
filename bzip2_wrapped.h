typedef unsigned char   Bool;

void compressStream ( FILE *stream, FILE *zStream );
Bool uncompressStream ( FILE *zStream, FILE *stream );
Bool testStream ( FILE *zStream );

