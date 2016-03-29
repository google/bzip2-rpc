#include "bzlib_private.h"

/*---------------------------------------------------*/
/*--- An implementation of 64-bit ints.  Sigh.    ---*/
/*--- Roll on widespread deployment of ANSI C9X ! ---*/
/*---------------------------------------------------*/

void uInt64_from_UInt32s ( UInt64* n, UInt32 lo32, UInt32 hi32 )
{
   n->b[7] = (UChar)((hi32 >> 24) & 0xFF);
   n->b[6] = (UChar)((hi32 >> 16) & 0xFF);
   n->b[5] = (UChar)((hi32 >> 8)  & 0xFF);
   n->b[4] = (UChar) (hi32        & 0xFF);
   n->b[3] = (UChar)((lo32 >> 24) & 0xFF);
   n->b[2] = (UChar)((lo32 >> 16) & 0xFF);
   n->b[1] = (UChar)((lo32 >> 8)  & 0xFF);
   n->b[0] = (UChar) (lo32        & 0xFF);
}

double uInt64_to_double ( UInt64* n )
{
   Int32  i;
   double base = 1.0;
   double sum  = 0.0;
   for (i = 0; i < 8; i++) {
      sum  += base * (double)(n->b[i]);
      base *= 256.0;
   }
   return sum;
}

Bool uInt64_isZero ( UInt64* n )
{
   Int32 i;
   for (i = 0; i < 8; i++)
      if (n->b[i] != 0) return 0;
   return 1;
}


/* Divide *n by 10, and return the remainder.  */
Int32 uInt64_qrm10 ( UInt64* n )
{
   UInt32 rem, tmp;
   Int32  i;
   rem = 0;
   for (i = 7; i >= 0; i--) {
      tmp = rem * 256 + n->b[i];
      n->b[i] = tmp / 10;
      rem = tmp % 10;
   }
   return rem;
}


/* ... and the Whole Entire Point of all this UInt64 stuff is
   so that we can supply the following function.
*/
void uInt64_toAscii ( char* outbuf, UInt64* n )
{
   Int32  i, q;
   UChar  buf[32];
   Int32  nBuf   = 0;
   UInt64 n_copy = *n;
   do {
      q = uInt64_qrm10 ( &n_copy );
      buf[nBuf] = q + '0';
      nBuf++;
   } while (!uInt64_isZero(&n_copy));
   outbuf[nBuf] = 0;
   for (i = 0; i < nBuf; i++) 
      outbuf[i] = buf[nBuf-i-1];
}


/*---------------------------------------------------*/
/*--- Processing of complete files and streams    ---*/
/*---------------------------------------------------*/

/*---------------------------------------------*/
static
Bool myfeof ( FILE* f )
{
   Int32 c = fgetc ( f );
   if (c == EOF) return True;
   ungetc ( c, f );
   return False;
}


/*---------------------------------------------------*/
int BZ_API(BZ2_bzCompressStream)( int        ifd,
                                  int        ofd,
                                  int        blockSize100k,
                                  int        verbosity,
                                  int        workFactor )
{
   FILE*   stream;
   FILE*   zStream;
   BZFILE* bzf = NULL;
   UChar   ibuf[5000];
   Int32   nIbuf;
   UInt32  nbytes_in_lo32, nbytes_in_hi32;
   UInt32  nbytes_out_lo32, nbytes_out_hi32;
   Int32   bzerr, ret;

   stream = fdopen(ifd, "r");
   if (!stream || ferror(stream)) return BZ_IO_ERROR;
   zStream = fdopen(ofd, "w");
   if (!zStream || ferror(zStream)) return BZ_IO_ERROR;

   bzf = BZ2_bzWriteOpen ( &bzerr, zStream,
                           blockSize100k, verbosity, workFactor );
   if (bzerr != BZ_OK) return bzerr;

   if (verbosity >= 2) fprintf ( stderr, "\n" );

   while (True) {

      if (myfeof(stream)) break;
      nIbuf = fread ( ibuf, sizeof(UChar), 5000, stream );
      if (ferror(stream)) return BZ_IO_ERROR;
      if (nIbuf > 0) BZ2_bzWrite ( &bzerr, bzf, (void*)ibuf, nIbuf );
      if (bzerr != BZ_OK) return bzerr;

   }

   BZ2_bzWriteClose64 ( &bzerr, bzf, 0,
                        &nbytes_in_lo32, &nbytes_in_hi32,
                        &nbytes_out_lo32, &nbytes_out_hi32 );
   if (bzerr != BZ_OK) return bzerr;

   if (ferror(zStream)) return BZ_IO_ERROR;
   ret = fflush ( zStream );
   if (ret == EOF) return BZ_IO_ERROR;

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

   return BZ_OK;
}

/*---------------------------------------------------*/
int BZ_API(BZ2_bzDecompressStream)( int        ifd,
                                    int        ofd,
                                    int        verbosity,
                                    int        small )
{
   FILE*   stream;
   FILE*   zStream;
   BZFILE* bzf = NULL;
   Int32   bzerr, ret, nread, streamNo, i;
   UChar   obuf[5000];
   UChar   unused[BZ_MAX_UNUSED];
   Int32   nUnused;
   const void* unusedTmpV;
   UChar*  unusedTmp;

   nUnused = 0;
   streamNo = 0;

   zStream = fdopen(ifd, "r");
   if (!zStream || ferror(zStream)) return BZ_IO_ERROR;
   stream = fdopen(ofd, "w");
   if (!stream || ferror(stream)) return BZ_IO_ERROR;

   while (True) {

      bzf = BZ2_bzReadOpen ( &bzerr, zStream, verbosity,
                             small, unused, nUnused );
      if (bzerr != BZ_OK) return bzerr;
      if (bzf == NULL) return BZ_IO_ERROR;
      streamNo++;

      while (bzerr == BZ_OK) {
         nread = BZ2_bzRead ( &bzerr, bzf, obuf, 5000 );
         if (bzerr == BZ_DATA_ERROR_MAGIC) {
           if (streamNo == 1) {
             return bzerr;
           } else {
             return BZ_OK;
           }
         }
         if ((bzerr == BZ_OK || bzerr == BZ_STREAM_END) && nread > 0)
            fwrite ( obuf, sizeof(UChar), nread, stream );
         if (ferror(stream)) return BZ_IO_ERROR;
      }
      if (bzerr != BZ_STREAM_END) return bzerr;

      BZ2_bzReadGetUnused ( &bzerr, bzf, &unusedTmpV, &nUnused );
      if (bzerr != BZ_OK) return bzerr;

      unusedTmp = (UChar*)unusedTmpV;
      for (i = 0; i < nUnused; i++) unused[i] = unusedTmp[i];

      BZ2_bzReadClose ( &bzerr, bzf );
      if (bzerr != BZ_OK) return bzerr;

      if (nUnused == 0 && myfeof(zStream)) break;
   }

   if (ferror(zStream)) return BZ_IO_ERROR;
   if (ferror(stream)) return BZ_IO_ERROR;
   ret = fflush ( stream );
   if (ret != 0) return BZ_IO_ERROR;
   if (verbosity >= 2) fprintf ( stderr, "\n    " );
   return BZ_OK;
}

/*---------------------------------------------------*/
int BZ_API(BZ2_bzTestStream)( int        ifd,
                              int        verbosity,
                              int        small )
{
   FILE*   zStream;
   BZFILE* bzf = NULL;
   Int32   bzerr, streamNo, i;
   UChar   obuf[5000];
   UChar   unused[BZ_MAX_UNUSED];
   Int32   nUnused;
   const void* unusedTmpV;
   UChar*  unusedTmp;

   nUnused = 0;
   streamNo = 0;

   zStream = fdopen(ifd, "r");
   if (!zStream || ferror(zStream)) return BZ_IO_ERROR;

   while (True) {

      bzf = BZ2_bzReadOpen ( &bzerr, zStream, verbosity,
                             small, unused, nUnused );
      if (bzerr != BZ_OK) return bzerr;
      if (bzf == NULL) return BZ_IO_ERROR;
      streamNo++;

      while (bzerr == BZ_OK) {
         BZ2_bzRead ( &bzerr, bzf, obuf, 5000 );
         if (bzerr == BZ_DATA_ERROR_MAGIC) return bzerr;
      }
      if (bzerr != BZ_STREAM_END) return bzerr;

      BZ2_bzReadGetUnused ( &bzerr, bzf, &unusedTmpV, &nUnused );
      if (bzerr != BZ_OK) return bzerr;

      unusedTmp = (UChar*)unusedTmpV;
      for (i = 0; i < nUnused; i++) unused[i] = unusedTmp[i];

      BZ2_bzReadClose ( &bzerr, bzf );
      if (bzerr != BZ_OK) return bzerr;
      if (nUnused == 0 && myfeof(zStream)) break;

   }

   if (ferror(zStream)) return BZ_IO_ERROR;

   if (verbosity >= 2) fprintf ( stderr, "\n    " );
   return BZ_OK;
}

