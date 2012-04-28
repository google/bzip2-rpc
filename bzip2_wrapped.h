typedef unsigned char   Bool;

void wrapped_compressStream ( FILE *stream, FILE *zStream );
Bool wrapped_uncompressStream ( FILE *zStream, FILE *stream );
Bool wrapped_testStream ( FILE *zStream );

