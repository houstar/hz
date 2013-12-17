#ifndef _HZLIB_H
#define _HZLIB_H

#ifdef __cplusplus
extern "C" {
#endif

#define HZ_RUN               0
#define HZ_FLUSH             1
#define HZ_FINISH            2

#define HZ_OK                0
#define HZ_RUN_OK            1
#define HZ_FLUSH_OK          2
#define HZ_FINISH_OK         3
#define HZ_STREAM_END        4
#define HZ_SEQUENCE_ERROR    (-1)
#define HZ_PARAM_ERROR       (-2)
#define HZ_MEM_ERROR         (-3)
#define HZ_DATA_ERROR        (-4)
#define HZ_DATA_ERROR_MAGIC  (-5)
#define HZ_IO_ERROR          (-6)
#define HZ_UNEXPECTED_EOF    (-7)
#define HZ_OUTBUFF_FULL      (-8)
#define HZ_CONFIG_ERROR      (-9)

typedef 
   struct {
      char *next_in;
      unsigned int avail_in;
      unsigned int total_in_lo32;
      unsigned int total_in_hi32;

      char *next_out;
      unsigned int avail_out;
      unsigned int total_out_lo32;
      unsigned int total_out_hi32;

      void *state;

      void *(*hzalloc)(void *,int,int);
      void (*hzfree)(void *,void *);
      void *opaque;
   } 
   hz_stream;

#ifndef HZ_NO_STDIO
/* Need a definitition for FILE */
#include <stdio.h>
#endif

#define HZ_API(func) func
#define HZ_EXTERN extern

/*-- Core (low-level) library functions --*/

HZ_EXTERN int HZ_API(HZ2_hzCompressInit) ( 
      hz_stream* strm, 
      int        blockSize100k, 
      int        verbosity, 
      int        workFactor 
   );

HZ_EXTERN int HZ_API(HZ2_hzCompress) ( 
      hz_stream* strm, 
      int action 
   );

HZ_EXTERN int HZ_API(HZ2_hzCompressEnd) ( 
      hz_stream* strm 
   );

HZ_EXTERN int HZ_API(HZ2_hzDecompressInit) ( 
      hz_stream *strm, 
      int       verbosity, 
      int       small
   );

HZ_EXTERN int HZ_API(HZ2_hzDecompress) ( 
      hz_stream* strm 
   );

HZ_EXTERN int HZ_API(HZ2_hzDecompressEnd) ( 
      hz_stream *strm 
   );



/*-- High(er) level library functions --*/

#ifndef HZ_NO_STDIO
#define HZ_MAX_UNUSED 5000

typedef void HZFILE;

HZ_EXTERN HZFILE* HZ_API(HZ2_hzReadOpen) ( 
      int*  hzerror,   
      FILE* f, 
      int   verbosity, 
      int   small,
      void* unused,    
      int   nUnused 
   );

HZ_EXTERN void HZ_API(HZ2_hzReadClose) ( 
      int*    hzerror, 
      HZFILE* b 
   );

HZ_EXTERN void HZ_API(HZ2_hzReadGetUnused) ( 
      int*    hzerror, 
      HZFILE* b, 
      void**  unused,  
      int*    nUnused 
   );

HZ_EXTERN int HZ_API(HZ2_hzRead) ( 
      int*    hzerror, 
      HZFILE* b, 
      void*   buf, 
      int     len 
   );

HZ_EXTERN HZFILE* HZ_API(HZ2_hzWriteOpen) ( 
      int*  hzerror,      
      FILE* f, 
      int   blockSize100k, 
      int   verbosity, 
      int   workFactor 
   );

HZ_EXTERN void HZ_API(HZ2_hzWrite) ( 
      int*    hzerror, 
      HZFILE* b, 
      void*   buf, 
      int     len 
   );

HZ_EXTERN void HZ_API(HZ2_hzWriteClose) ( 
      int*          hzerror, 
      HZFILE*       b, 
      int           abandon, 
      unsigned int* nbytes_in, 
      unsigned int* nbytes_out 
   );

HZ_EXTERN void HZ_API(HZ2_hzWriteClose64) ( 
      int*          hzerror, 
      HZFILE*       b, 
      int           abandon, 
      unsigned int* nbytes_in_lo32, 
      unsigned int* nbytes_in_hi32, 
      unsigned int* nbytes_out_lo32, 
      unsigned int* nbytes_out_hi32
   );
#endif


/*-- Utility functions --*/

HZ_EXTERN int HZ_API(HZ2_hzBuffToBuffCompress) ( 
      char*         dest, 
      unsigned int* destLen,
      char*         source, 
      unsigned int  sourceLen,
      int           blockSize100k, 
      int           verbosity, 
      int           workFactor 
   );

HZ_EXTERN int HZ_API(HZ2_hzBuffToBuffDecompress) ( 
      char*         dest, 
      unsigned int* destLen,
      char*         source, 
      unsigned int  sourceLen,
      int           small, 
      int           verbosity 
   );


/*--
   Code contributed by Yoshioka Tsuneo (tsuneo@rr.iij4u.or.jp)
   to support better zlib compatibility.
   This code is not _officially_ part of libhzip2 (yet);
   I haven't tested it, documented it, or considered the
   threading-safeness of it.
   If this code breaks, please contact both Yoshioka and me.
--*/

HZ_EXTERN const char * HZ_API(HZ2_hzlibVersion) (
      void
   );

#ifndef HZ_NO_STDIO
HZ_EXTERN HZFILE * HZ_API(HZ2_hzopen) (
      const char *path,
      const char *mode
   );

HZ_EXTERN HZFILE * HZ_API(HZ2_hzdopen) (
      int        fd,
      const char *mode
   );
         
HZ_EXTERN int HZ_API(HZ2_hzread) (
      HZFILE* b, 
      void* buf, 
      int len 
   );

HZ_EXTERN int HZ_API(HZ2_hzwrite) (
      HZFILE* b, 
      void*   buf, 
      int     len 
   );

HZ_EXTERN int HZ_API(HZ2_hzflush) (
      HZFILE* b
   );

HZ_EXTERN void HZ_API(HZ2_hzclose) (
      HZFILE* b
   );

HZ_EXTERN const char * HZ_API(HZ2_hzerror) (
      HZFILE *b, 
      int    *errnum
   );
#endif

#ifdef __cplusplus
}
#endif

#endif

/*-------------------------------------------------------------*/
/*--- end                                           hzlib.h ---*/
/*-------------------------------------------------------------*/
