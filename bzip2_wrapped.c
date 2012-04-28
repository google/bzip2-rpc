#include "bzip2_wrapped.c"

/*---------------------------------------------*/
void compressStream ( FILE *stream, FILE *zStream )
{
   BZFILE* bzf = NULL;
   UChar   ibuf[5000];
   Int32   nIbuf;
   UInt32  nbytes_in_lo32, nbytes_in_hi32;
   UInt32  nbytes_out_lo32, nbytes_out_hi32;
   Int32   bzerr, bzerr_dummy, ret;

   SET_BINARY_MODE(stream);
   SET_BINARY_MODE(zStream);

   if (ferror(stream)) goto errhandler_io;
   if (ferror(zStream)) goto errhandler_io;

   bzf = BZ2_bzWriteOpen ( &bzerr, zStream, 
                           blockSize100k, verbosity, workFactor );   
   if (bzerr != BZ_OK) goto errhandler;

   if (verbosity >= 2) fprintf ( stderr, "\n" );

   while (True) {

      if (myfeof(stream)) break;
      nIbuf = fread ( ibuf, sizeof(UChar), 5000, stream );
      if (ferror(stream)) goto errhandler_io;
      if (nIbuf > 0) BZ2_bzWrite ( &bzerr, bzf, (void*)ibuf, nIbuf );
      if (bzerr != BZ_OK) goto errhandler;

   }

   BZ2_bzWriteClose64 ( &bzerr, bzf, 0, 
                        &nbytes_in_lo32, &nbytes_in_hi32,
                        &nbytes_out_lo32, &nbytes_out_hi32 );
   if (bzerr != BZ_OK) goto errhandler;

   if (ferror(zStream)) goto errhandler_io;
   ret = fflush ( zStream );
   if (ret == EOF) goto errhandler_io;
   if (zStream != stdout) {
      Int32 fd = fileno ( zStream );
      if (fd < 0) goto errhandler_io;
      applySavedFileAttrToOutputFile ( fd );
      ret = fclose ( zStream );
      outputHandleJustInCase = NULL;
      if (ret == EOF) goto errhandler_io;
   }
   outputHandleJustInCase = NULL;
   if (ferror(stream)) goto errhandler_io;
   ret = fclose ( stream );
   if (ret == EOF) goto errhandler_io;

   if (verbosity >= 1) {
      if (nbytes_in_lo32 == 0 && nbytes_in_hi32 == 0) {
	 fprintf ( stderr, " no data compressed.\n");
      } else {
	 Char   buf_nin[32], buf_nout[32];
	 UInt64 nbytes_in,   nbytes_out;
	 double nbytes_in_d, nbytes_out_d;
	 uInt64_from_UInt32s ( &nbytes_in, 
			       nbytes_in_lo32, nbytes_in_hi32 );
	 uInt64_from_UInt32s ( &nbytes_out, 
			       nbytes_out_lo32, nbytes_out_hi32 );
	 nbytes_in_d  = uInt64_to_double ( &nbytes_in );
	 nbytes_out_d = uInt64_to_double ( &nbytes_out );
	 uInt64_toAscii ( buf_nin, &nbytes_in );
	 uInt64_toAscii ( buf_nout, &nbytes_out );
	 fprintf ( stderr, "%6.3f:1, %6.3f bits/byte, "
		   "%5.2f%% saved, %s in, %s out.\n",
		   nbytes_in_d / nbytes_out_d,
		   (8.0 * nbytes_out_d) / nbytes_in_d,
		   100.0 * (1.0 - nbytes_out_d / nbytes_in_d),
		   buf_nin,
		   buf_nout
		 );
      }
   }

   return;

   errhandler:
   BZ2_bzWriteClose64 ( &bzerr_dummy, bzf, 1, 
                        &nbytes_in_lo32, &nbytes_in_hi32,
                        &nbytes_out_lo32, &nbytes_out_hi32 );
   switch (bzerr) {
      case BZ_CONFIG_ERROR:
         configError(); break;
      case BZ_MEM_ERROR:
         outOfMemory (); break;
      case BZ_IO_ERROR:
         errhandler_io:
         ioError(); break;
      default:
         panic ( "compress:unexpected error" );
   }

   panic ( "compress:end" );
   /*notreached*/
}



/*---------------------------------------------*/
Bool uncompressStream ( FILE *zStream, FILE *stream )
{
   BZFILE* bzf = NULL;
   Int32   bzerr, bzerr_dummy, ret, nread, streamNo, i;
   UChar   obuf[5000];
   UChar   unused[BZ_MAX_UNUSED];
   Int32   nUnused;
   void*   unusedTmpV;
   UChar*  unusedTmp;

   nUnused = 0;
   streamNo = 0;

   SET_BINARY_MODE(stream);
   SET_BINARY_MODE(zStream);

   if (ferror(stream)) goto errhandler_io;
   if (ferror(zStream)) goto errhandler_io;

   while (True) {

      bzf = BZ2_bzReadOpen ( 
               &bzerr, zStream, verbosity, 
               (int)smallMode, unused, nUnused
            );
      if (bzf == NULL || bzerr != BZ_OK) goto errhandler;
      streamNo++;

      while (bzerr == BZ_OK) {
         nread = BZ2_bzRead ( &bzerr, bzf, obuf, 5000 );
         if (bzerr == BZ_DATA_ERROR_MAGIC) goto trycat;
         if ((bzerr == BZ_OK || bzerr == BZ_STREAM_END) && nread > 0)
            fwrite ( obuf, sizeof(UChar), nread, stream );
         if (ferror(stream)) goto errhandler_io;
      }
      if (bzerr != BZ_STREAM_END) goto errhandler;

      BZ2_bzReadGetUnused ( &bzerr, bzf, &unusedTmpV, &nUnused );
      if (bzerr != BZ_OK) panic ( "decompress:bzReadGetUnused" );

      unusedTmp = (UChar*)unusedTmpV;
      for (i = 0; i < nUnused; i++) unused[i] = unusedTmp[i];

      BZ2_bzReadClose ( &bzerr, bzf );
      if (bzerr != BZ_OK) panic ( "decompress:bzReadGetUnused" );

      if (nUnused == 0 && myfeof(zStream)) break;
   }

   closeok:
   if (ferror(zStream)) goto errhandler_io;
   if (stream != stdout) {
      Int32 fd = fileno ( stream );
      if (fd < 0) goto errhandler_io;
      applySavedFileAttrToOutputFile ( fd );
   }
   ret = fclose ( zStream );
   if (ret == EOF) goto errhandler_io;

   if (ferror(stream)) goto errhandler_io;
   ret = fflush ( stream );
   if (ret != 0) goto errhandler_io;
   if (stream != stdout) {
      ret = fclose ( stream );
      outputHandleJustInCase = NULL;
      if (ret == EOF) goto errhandler_io;
   }
   outputHandleJustInCase = NULL;
   if (verbosity >= 2) fprintf ( stderr, "\n    " );
   return True;

   trycat: 
   if (forceOverwrite) {
      rewind(zStream);
      while (True) {
      	 if (myfeof(zStream)) break;
      	 nread = fread ( obuf, sizeof(UChar), 5000, zStream );
      	 if (ferror(zStream)) goto errhandler_io;
      	 if (nread > 0) fwrite ( obuf, sizeof(UChar), nread, stream );
      	 if (ferror(stream)) goto errhandler_io;
      }
      goto closeok;
   }
  
   errhandler:
   BZ2_bzReadClose ( &bzerr_dummy, bzf );
   switch (bzerr) {
      case BZ_CONFIG_ERROR:
         configError(); break;
      case BZ_IO_ERROR:
         errhandler_io:
         ioError(); break;
      case BZ_DATA_ERROR:
         crcError();
      case BZ_MEM_ERROR:
         outOfMemory();
      case BZ_UNEXPECTED_EOF:
         compressedStreamEOF();
      case BZ_DATA_ERROR_MAGIC:
         if (zStream != stdin) fclose(zStream);
         if (stream != stdout) fclose(stream);
         if (streamNo == 1) {
            return False;
         } else {
            if (noisy)
            fprintf ( stderr, 
                      "\n%s: %s: trailing garbage after EOF ignored\n",
                      progName, inName );
            return True;       
         }
      default:
         panic ( "decompress:unexpected error" );
   }

   panic ( "decompress:end" );
   return True; /*notreached*/
}


/*---------------------------------------------*/
Bool testStream ( FILE *zStream )
{
   BZFILE* bzf = NULL;
   Int32   bzerr, bzerr_dummy, ret, nread, streamNo, i;
   UChar   obuf[5000];
   UChar   unused[BZ_MAX_UNUSED];
   Int32   nUnused;
   void*   unusedTmpV;
   UChar*  unusedTmp;

   nUnused = 0;
   streamNo = 0;

   SET_BINARY_MODE(zStream);
   if (ferror(zStream)) goto errhandler_io;

   while (True) {

      bzf = BZ2_bzReadOpen ( 
               &bzerr, zStream, verbosity, 
               (int)smallMode, unused, nUnused
            );
      if (bzf == NULL || bzerr != BZ_OK) goto errhandler;
      streamNo++;

      while (bzerr == BZ_OK) {
         nread = BZ2_bzRead ( &bzerr, bzf, obuf, 5000 );
         if (bzerr == BZ_DATA_ERROR_MAGIC) goto errhandler;
      }
      if (bzerr != BZ_STREAM_END) goto errhandler;

      BZ2_bzReadGetUnused ( &bzerr, bzf, &unusedTmpV, &nUnused );
      if (bzerr != BZ_OK) panic ( "test:bzReadGetUnused" );

      unusedTmp = (UChar*)unusedTmpV;
      for (i = 0; i < nUnused; i++) unused[i] = unusedTmp[i];

      BZ2_bzReadClose ( &bzerr, bzf );
      if (bzerr != BZ_OK) panic ( "test:bzReadGetUnused" );
      if (nUnused == 0 && myfeof(zStream)) break;

   }

   if (ferror(zStream)) goto errhandler_io;
   ret = fclose ( zStream );
   if (ret == EOF) goto errhandler_io;

   if (verbosity >= 2) fprintf ( stderr, "\n    " );
   return True;

   errhandler:
   BZ2_bzReadClose ( &bzerr_dummy, bzf );
   if (verbosity == 0) 
      fprintf ( stderr, "%s: %s: ", progName, inName );
   switch (bzerr) {
      case BZ_CONFIG_ERROR:
         configError(); break;
      case BZ_IO_ERROR:
         errhandler_io:
         ioError(); break;
      case BZ_DATA_ERROR:
         fprintf ( stderr,
                   "data integrity (CRC) error in data\n" );
         return False;
      case BZ_MEM_ERROR:
         outOfMemory();
      case BZ_UNEXPECTED_EOF:
         fprintf ( stderr,
                   "file ends unexpectedly\n" );
         return False;
      case BZ_DATA_ERROR_MAGIC:
         if (zStream != stdin) fclose(zStream);
         if (streamNo == 1) {
          fprintf ( stderr, 
                    "bad magic number (file not created by bzip2)\n" );
            return False;
         } else {
            if (noisy)
            fprintf ( stderr, 
                      "trailing garbage after EOF ignored\n" );
            return True;       
         }
      default:
         panic ( "test:unexpected error" );
   }

   panic ( "test:end" );
   return True; /*notreached*/
}
