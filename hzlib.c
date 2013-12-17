#include "hzlib_pri.h"


/*---------------------------------------------------*/
/*--- Compression stuff                           ---*/
/*---------------------------------------------------*/


/*---------------------------------------------------*/
#ifndef HZ_NO_STDIO
void HZ2_hz__AssertH__fail ( int errcode )
{
   fprintf(stderr, 
      "\n\nhzip2/libhzip2: internal error number %d.\n"
      "This is a bug in hzip2/libhzip2, %s.\n"
      "Please report it to me at:houstar@foxmail.com.  If this happened\n"
      "when you were using some program which uses libhzip2 as a\n"
      "component, you should also report this bug to the author(s)\n"
      "of that program.  Please make an effort to report this bug;\n"
      "timely and accurate bug reports eventually lead to higher\n"
      "quality software.  Thanks.  Julian Seward, 10 December 2007.\n\n",
      errcode,
      HZ2_hzlibVersion()
   );

   if (errcode == 1007) {
   fprintf(stderr,
      "\n*** A special note about internal error number 1007 ***\n"
      "\n"
      "Experience suggests that a common cause of i.e. 1007\n"
      "is unreliable memory or other hardware.  The 1007 assertion\n"
      "just happens to cross-check the results of huge numbers of\n"
      "memory reads/writes, and so acts (unintendedly) as a stress\n"
      "test of your memory system.\n"
      "\n"
      "I suggest the following: try compressing the file again,\n"
      "possibly monitoring progress in detail with the -vv flag.\n"
      "\n"
      "* If the error cannot be reproduced, and/or happens at different\n"
      "  points in compression, you may have a flaky memory system.\n"
      "  Try a memory-test program.  I have used Memtest86\n"
      "  (www.memtest86.com).  At the time of writing it is free (GPLd).\n"
      "  Memtest86 tests memory much more thorougly than your BIOSs\n"
      "  power-on test, and may find failures that the BIOS doesn't.\n"
      "\n"
      "* If the error can be repeatably reproduced, this is a bug in\n"
      "  hzip2, and I would very much like to hear about it.  Please\n"
      "  let me know, and, ideally, save a copy of the file causing the\n"
      "  problem -- without which I will be unable to investigate it.\n"
      "\n"
   );
   }

   exit(3);
}
#endif


/*---------------------------------------------------*/
static
int hz_config_ok ( void )
{
   if (sizeof(int)   != 4) return 0;
   if (sizeof(short) != 2) return 0;
   if (sizeof(char)  != 1) return 0;
   return 1;
}


/*---------------------------------------------------*/
static
void* default_hzalloc ( void* opaque, Int32 items, Int32 size )
{
   void* v = malloc ( items * size );
   return v;
}

static
void default_hzfree ( void* opaque, void* addr )
{
   if (addr != NULL) free ( addr );
}


/*---------------------------------------------------*/
static
void prepare_new_block ( EState* s )
{
   Int32 i;
   s->nblock = 0;
   s->numZ = 0;
   s->state_out_pos = 0;
   HZ_INITIALISE_CRC ( s->blockCRC );
   for (i = 0; i < 256; i++) s->inUse[i] = False;
   s->blockNo++;
}


/*---------------------------------------------------*/
static
void init_RL ( EState* s )
{
   s->state_in_ch  = 256;
   s->state_in_len = 0;
}


static
Bool isempty_RL ( EState* s )
{
   if (s->state_in_ch < 256 && s->state_in_len > 0)
      return False; else
      return True;
}


/*---------------------------------------------------*/
int HZ_API(HZ2_hzCompressInit) 
                    ( hz_stream* strm, 
                     int        blockSize100k,
                     int        verbosity,
                     int        workFactor )
{
   Int32   n;
   EState* s;

   if (!hz_config_ok()) return HZ_CONFIG_ERROR;

   if (strm == NULL || 
       blockSize100k < 1 || blockSize100k > 9 ||
       workFactor < 0 || workFactor > 250)
     return HZ_PARAM_ERROR;

   if (workFactor == 0) workFactor = 30;
   if (strm->hzalloc == NULL) strm->hzalloc = default_hzalloc;
   if (strm->hzfree == NULL) strm->hzfree = default_hzfree;

   s = HZALLOC( sizeof(EState) );
   if (s == NULL) return HZ_MEM_ERROR;
   s->strm = strm;

   s->arr1 = NULL;
   s->arr2 = NULL;
   s->ftab = NULL;

   n       = 100000 * blockSize100k;
   s->arr1 = HZALLOC( n                  * sizeof(UInt32) );
   s->arr2 = HZALLOC( (n+HZ_N_OVERSHOOT) * sizeof(UInt32) );
   s->ftab = HZALLOC( 65537              * sizeof(UInt32) );

   if (s->arr1 == NULL || s->arr2 == NULL || s->ftab == NULL) {
      if (s->arr1 != NULL) HZFREE(s->arr1);
      if (s->arr2 != NULL) HZFREE(s->arr2);
      if (s->ftab != NULL) HZFREE(s->ftab);
      if (s       != NULL) HZFREE(s);
      return HZ_MEM_ERROR;
   }

   s->blockNo           = 0;
   s->state             = HZ_S_INPUT;
   s->mode              = HZ_M_RUNNING;
   s->combinedCRC       = 0;
   s->blockSize100k     = blockSize100k;
   s->nblockMAX         = 100000 * blockSize100k - 19;
   s->verbosity         = verbosity;
   s->workFactor        = workFactor;

   s->block             = (UChar*)s->arr2;
   s->mtfv              = (UInt16*)s->arr1;
   s->zbits             = NULL;
   s->ptr               = (UInt32*)s->arr1;

   strm->state          = s;
   strm->total_in_lo32  = 0;
   strm->total_in_hi32  = 0;
   strm->total_out_lo32 = 0;
   strm->total_out_hi32 = 0;
   init_RL ( s );
   prepare_new_block ( s );
   return HZ_OK;
}


/*---------------------------------------------------*/
static
void add_pair_to_block ( EState* s )
{
   Int32 i;
   UChar ch = (UChar)(s->state_in_ch);
   for (i = 0; i < s->state_in_len; i++) {
      HZ_UPDATE_CRC( s->blockCRC, ch );
   }
   s->inUse[s->state_in_ch] = True;
   switch (s->state_in_len) {
      case 1:
         s->block[s->nblock] = (UChar)ch; s->nblock++;
         break;
      case 2:
         s->block[s->nblock] = (UChar)ch; s->nblock++;
         s->block[s->nblock] = (UChar)ch; s->nblock++;
         break;
      case 3:
         s->block[s->nblock] = (UChar)ch; s->nblock++;
         s->block[s->nblock] = (UChar)ch; s->nblock++;
         s->block[s->nblock] = (UChar)ch; s->nblock++;
         break;
      default:
         s->inUse[s->state_in_len-4] = True;
         s->block[s->nblock] = (UChar)ch; s->nblock++;
         s->block[s->nblock] = (UChar)ch; s->nblock++;
         s->block[s->nblock] = (UChar)ch; s->nblock++;
         s->block[s->nblock] = (UChar)ch; s->nblock++;
         s->block[s->nblock] = ((UChar)(s->state_in_len-4));
         s->nblock++;
         break;
   }
}


/*---------------------------------------------------*/
static
void flush_RL ( EState* s )
{
   if (s->state_in_ch < 256) add_pair_to_block ( s );
   init_RL ( s );
}


/*---------------------------------------------------*/
#define ADD_CHAR_TO_BLOCK(zs,zchh0)               \
{                                                 \
   UInt32 zchh = (UInt32)(zchh0);                 \
   /*-- fast track the common case --*/           \
   if (zchh != zs->state_in_ch &&                 \
       zs->state_in_len == 1) {                   \
      UChar ch = (UChar)(zs->state_in_ch);        \
      HZ_UPDATE_CRC( zs->blockCRC, ch );          \
      zs->inUse[zs->state_in_ch] = True;          \
      zs->block[zs->nblock] = (UChar)ch;          \
      zs->nblock++;                               \
      zs->state_in_ch = zchh;                     \
   }                                              \
   else                                           \
   /*-- general, uncommon cases --*/              \
   if (zchh != zs->state_in_ch ||                 \
      zs->state_in_len == 255) {                  \
      if (zs->state_in_ch < 256)                  \
         add_pair_to_block ( zs );                \
      zs->state_in_ch = zchh;                     \
      zs->state_in_len = 1;                       \
   } else {                                       \
      zs->state_in_len++;                         \
   }                                              \
}


/*---------------------------------------------------*/
static
Bool copy_input_until_stop ( EState* s )
{
   Bool progress_in = False;

   if (s->mode == HZ_M_RUNNING) {

      /*-- fast track the common case --*/
      while (True) {
         /*-- block full? --*/
         if (s->nblock >= s->nblockMAX) break;
         /*-- no input? --*/
         if (s->strm->avail_in == 0) break;
         progress_in = True;
         ADD_CHAR_TO_BLOCK ( s, (UInt32)(*((UChar*)(s->strm->next_in))) ); 
         s->strm->next_in++;
         s->strm->avail_in--;
         s->strm->total_in_lo32++;
         if (s->strm->total_in_lo32 == 0) s->strm->total_in_hi32++;
      }

   } else {

      /*-- general, uncommon case --*/
      while (True) {
         /*-- block full? --*/
         if (s->nblock >= s->nblockMAX) break;
         /*-- no input? --*/
         if (s->strm->avail_in == 0) break;
         /*-- flush/finish end? --*/
         if (s->avail_in_expect == 0) break;
         progress_in = True;
         ADD_CHAR_TO_BLOCK ( s, (UInt32)(*((UChar*)(s->strm->next_in))) ); 
         s->strm->next_in++;
         s->strm->avail_in--;
         s->strm->total_in_lo32++;
         if (s->strm->total_in_lo32 == 0) s->strm->total_in_hi32++;
         s->avail_in_expect--;
      }
   }
   return progress_in;
}


/*---------------------------------------------------*/
static
Bool copy_output_until_stop ( EState* s )
{
   Bool progress_out = False;

   while (True) {

      /*-- no output space? --*/
      if (s->strm->avail_out == 0) break;

      /*-- block done? --*/
      if (s->state_out_pos >= s->numZ) break;

      progress_out = True;
      *(s->strm->next_out) = s->zbits[s->state_out_pos];
      s->state_out_pos++;
      s->strm->avail_out--;
      s->strm->next_out++;
      s->strm->total_out_lo32++;
      if (s->strm->total_out_lo32 == 0) s->strm->total_out_hi32++;
   }

   return progress_out;
}


/*---------------------------------------------------*/
static
Bool handle_compress ( hz_stream* strm )
{
   Bool progress_in  = False;
   Bool progress_out = False;
   EState* s = strm->state;
   
   while (True) {

      if (s->state == HZ_S_OUTPUT) {
         progress_out |= copy_output_until_stop ( s );
         if (s->state_out_pos < s->numZ) break;
         if (s->mode == HZ_M_FINISHING && 
             s->avail_in_expect == 0 &&
             isempty_RL(s)) break;
         prepare_new_block ( s );
         s->state = HZ_S_INPUT;
         if (s->mode == HZ_M_FLUSHING && 
             s->avail_in_expect == 0 &&
             isempty_RL(s)) break;
      }

      if (s->state == HZ_S_INPUT) {
         progress_in |= copy_input_until_stop ( s );
         if (s->mode != HZ_M_RUNNING && s->avail_in_expect == 0) {
            flush_RL ( s );
            HZ2_compressBlock ( s, (Bool)(s->mode == HZ_M_FINISHING) );
            s->state = HZ_S_OUTPUT;
         }
         else
         if (s->nblock >= s->nblockMAX) {
            HZ2_compressBlock ( s, False );
            s->state = HZ_S_OUTPUT;
         }
         else
         if (s->strm->avail_in == 0) {
            break;
         }
      }

   }

   return progress_in || progress_out;
}


/*---------------------------------------------------*/
int HZ_API(HZ2_hzCompress) ( hz_stream *strm, int action )
{
   Bool progress;
   EState* s;
   if (strm == NULL) return HZ_PARAM_ERROR;
   s = strm->state;
   if (s == NULL) return HZ_PARAM_ERROR;
   if (s->strm != strm) return HZ_PARAM_ERROR;

   preswitch:
   switch (s->mode) {

      case HZ_M_IDLE:
         return HZ_SEQUENCE_ERROR;

      case HZ_M_RUNNING:
         if (action == HZ_RUN) {
            progress = handle_compress ( strm );
            return progress ? HZ_RUN_OK : HZ_PARAM_ERROR;
         } 
         else
	 if (action == HZ_FLUSH) {
            s->avail_in_expect = strm->avail_in;
            s->mode = HZ_M_FLUSHING;
            goto preswitch;
         }
         else
         if (action == HZ_FINISH) {
            s->avail_in_expect = strm->avail_in;
            s->mode = HZ_M_FINISHING;
            goto preswitch;
         }
         else 
            return HZ_PARAM_ERROR;

      case HZ_M_FLUSHING:
         if (action != HZ_FLUSH) return HZ_SEQUENCE_ERROR;
         if (s->avail_in_expect != s->strm->avail_in) 
            return HZ_SEQUENCE_ERROR;
         progress = handle_compress ( strm );
         if (s->avail_in_expect > 0 || !isempty_RL(s) ||
             s->state_out_pos < s->numZ) return HZ_FLUSH_OK;
         s->mode = HZ_M_RUNNING;
         return HZ_RUN_OK;

      case HZ_M_FINISHING:
         if (action != HZ_FINISH) return HZ_SEQUENCE_ERROR;
         if (s->avail_in_expect != s->strm->avail_in) 
            return HZ_SEQUENCE_ERROR;
         progress = handle_compress ( strm );
         if (!progress) return HZ_SEQUENCE_ERROR;
         if (s->avail_in_expect > 0 || !isempty_RL(s) ||
             s->state_out_pos < s->numZ) return HZ_FINISH_OK;
         s->mode = HZ_M_IDLE;
         return HZ_STREAM_END;
   }
   return HZ_OK; /*--not reached--*/
}


/*---------------------------------------------------*/
int HZ_API(HZ2_hzCompressEnd)  ( hz_stream *strm )
{
   EState* s;
   if (strm == NULL) return HZ_PARAM_ERROR;
   s = strm->state;
   if (s == NULL) return HZ_PARAM_ERROR;
   if (s->strm != strm) return HZ_PARAM_ERROR;

   if (s->arr1 != NULL) HZFREE(s->arr1);
   if (s->arr2 != NULL) HZFREE(s->arr2);
   if (s->ftab != NULL) HZFREE(s->ftab);
   HZFREE(strm->state);

   strm->state = NULL;   

   return HZ_OK;
}


/*---------------------------------------------------*/
/*--- Decompression stuff                         ---*/
/*---------------------------------------------------*/

/*---------------------------------------------------*/
int HZ_API(HZ2_hzDecompressInit) 
                     ( hz_stream* strm, 
                       int        verbosity,
                       int        small )
{
   DState* s;

   if (!hz_config_ok()) return HZ_CONFIG_ERROR;

   if (strm == NULL) return HZ_PARAM_ERROR;
   if (small != 0 && small != 1) return HZ_PARAM_ERROR;
   if (verbosity < 0 || verbosity > 4) return HZ_PARAM_ERROR;

   if (strm->hzalloc == NULL) strm->hzalloc = default_hzalloc;
   if (strm->hzfree == NULL) strm->hzfree = default_hzfree;

   s = HZALLOC( sizeof(DState) );
   if (s == NULL) return HZ_MEM_ERROR;
   s->strm                  = strm;
   strm->state              = s;
   s->state                 = HZ_X_MAGIC_1;
   s->bsLive                = 0;
   s->bsBuff                = 0;
   s->calculatedCombinedCRC = 0;
   strm->total_in_lo32      = 0;
   strm->total_in_hi32      = 0;
   strm->total_out_lo32     = 0;
   strm->total_out_hi32     = 0;
   s->smallDecompress       = (Bool)small;
   s->ll4                   = NULL;
   s->ll16                  = NULL;
   s->tt                    = NULL;
   s->currBlockNo           = 0;
   s->verbosity             = verbosity;

   return HZ_OK;
}


/*---------------------------------------------------*/
/* Return  True iff data corruption is discovered.
   Returns False if there is no problem.
*/
static
Bool unRLE_obuf_to_output_FAST ( DState* s )
{
   UChar k1;

   if (s->blockRandomised) {

      while (True) {
         /* try to finish existing run */
         while (True) {
            if (s->strm->avail_out == 0) return False;
            if (s->state_out_len == 0) break;
            *( (UChar*)(s->strm->next_out) ) = s->state_out_ch;
            HZ_UPDATE_CRC ( s->calculatedBlockCRC, s->state_out_ch );
            s->state_out_len--;
            s->strm->next_out++;
            s->strm->avail_out--;
            s->strm->total_out_lo32++;
            if (s->strm->total_out_lo32 == 0) s->strm->total_out_hi32++;
         }

         /* can a new run be started? */
         if (s->nblock_used == s->save_nblock+1) return False;
               
         /* Only caused by corrupt data stream? */
         if (s->nblock_used > s->save_nblock+1)
            return True;
   
         s->state_out_len = 1;
         s->state_out_ch = s->k0;
         HZ_GET_FAST(k1); HZ_RAND_UPD_MASK; 
         k1 ^= HZ_RAND_MASK; s->nblock_used++;
         if (s->nblock_used == s->save_nblock+1) continue;
         if (k1 != s->k0) { s->k0 = k1; continue; };
   
         s->state_out_len = 2;
         HZ_GET_FAST(k1); HZ_RAND_UPD_MASK; 
         k1 ^= HZ_RAND_MASK; s->nblock_used++;
         if (s->nblock_used == s->save_nblock+1) continue;
         if (k1 != s->k0) { s->k0 = k1; continue; };
   
         s->state_out_len = 3;
         HZ_GET_FAST(k1); HZ_RAND_UPD_MASK; 
         k1 ^= HZ_RAND_MASK; s->nblock_used++;
         if (s->nblock_used == s->save_nblock+1) continue;
         if (k1 != s->k0) { s->k0 = k1; continue; };
   
         HZ_GET_FAST(k1); HZ_RAND_UPD_MASK; 
         k1 ^= HZ_RAND_MASK; s->nblock_used++;
         s->state_out_len = ((Int32)k1) + 4;
         HZ_GET_FAST(s->k0); HZ_RAND_UPD_MASK; 
         s->k0 ^= HZ_RAND_MASK; s->nblock_used++;
      }

   } else {

      /* restore */
      UInt32        c_calculatedBlockCRC = s->calculatedBlockCRC;
      UChar         c_state_out_ch       = s->state_out_ch;
      Int32         c_state_out_len      = s->state_out_len;
      Int32         c_nblock_used        = s->nblock_used;
      Int32         c_k0                 = s->k0;
      UInt32*       c_tt                 = s->tt;
      UInt32        c_tPos               = s->tPos;
      char*         cs_next_out          = s->strm->next_out;
      unsigned int  cs_avail_out         = s->strm->avail_out;
      Int32         ro_blockSize100k     = s->blockSize100k;
      /* end restore */

      UInt32       avail_out_INIT = cs_avail_out;
      Int32        s_save_nblockPP = s->save_nblock+1;
      unsigned int total_out_lo32_old;

      while (True) {

         /* try to finish existing run */
         if (c_state_out_len > 0) {
            while (True) {
               if (cs_avail_out == 0) goto return_notr;
               if (c_state_out_len == 1) break;
               *( (UChar*)(cs_next_out) ) = c_state_out_ch;
               HZ_UPDATE_CRC ( c_calculatedBlockCRC, c_state_out_ch );
               c_state_out_len--;
               cs_next_out++;
               cs_avail_out--;
            }
            s_state_out_len_eq_one:
            {
               if (cs_avail_out == 0) { 
                  c_state_out_len = 1; goto return_notr;
               };
               *( (UChar*)(cs_next_out) ) = c_state_out_ch;
               HZ_UPDATE_CRC ( c_calculatedBlockCRC, c_state_out_ch );
               cs_next_out++;
               cs_avail_out--;
            }
         }   
         /* Only caused by corrupt data stream? */
         if (c_nblock_used > s_save_nblockPP)
            return True;

         /* can a new run be started? */
         if (c_nblock_used == s_save_nblockPP) {
            c_state_out_len = 0; goto return_notr;
         };   
         c_state_out_ch = c_k0;
         HZ_GET_FAST_C(k1); c_nblock_used++;
         if (k1 != c_k0) { 
            c_k0 = k1; goto s_state_out_len_eq_one; 
         };
         if (c_nblock_used == s_save_nblockPP) 
            goto s_state_out_len_eq_one;
   
         c_state_out_len = 2;
         HZ_GET_FAST_C(k1); c_nblock_used++;
         if (c_nblock_used == s_save_nblockPP) continue;
         if (k1 != c_k0) { c_k0 = k1; continue; };
   
         c_state_out_len = 3;
         HZ_GET_FAST_C(k1); c_nblock_used++;
         if (c_nblock_used == s_save_nblockPP) continue;
         if (k1 != c_k0) { c_k0 = k1; continue; };
   
         HZ_GET_FAST_C(k1); c_nblock_used++;
         c_state_out_len = ((Int32)k1) + 4;
         HZ_GET_FAST_C(c_k0); c_nblock_used++;
      }

      return_notr:
      total_out_lo32_old = s->strm->total_out_lo32;
      s->strm->total_out_lo32 += (avail_out_INIT - cs_avail_out);
      if (s->strm->total_out_lo32 < total_out_lo32_old)
         s->strm->total_out_hi32++;

      /* save */
      s->calculatedBlockCRC = c_calculatedBlockCRC;
      s->state_out_ch       = c_state_out_ch;
      s->state_out_len      = c_state_out_len;
      s->nblock_used        = c_nblock_used;
      s->k0                 = c_k0;
      s->tt                 = c_tt;
      s->tPos               = c_tPos;
      s->strm->next_out     = cs_next_out;
      s->strm->avail_out    = cs_avail_out;
      /* end save */
   }
   return False;
}



/*---------------------------------------------------*/
__inline__ Int32 HZ2_indexIntoF ( Int32 indx, Int32 *cftab )
{
   Int32 nb, na, mid;
   nb = 0;
   na = 256;
   do {
      mid = (nb + na) >> 1;
      if (indx >= cftab[mid]) nb = mid; else na = mid;
   }
   while (na - nb != 1);
   return nb;
}


/*---------------------------------------------------*/
/* Return  True iff data corruption is discovered.
   Returns False if there is no problem.
*/
static
Bool unRLE_obuf_to_output_SMALL ( DState* s )
{
   UChar k1;

   if (s->blockRandomised) {

      while (True) {
         /* try to finish existing run */
         while (True) {
            if (s->strm->avail_out == 0) return False;
            if (s->state_out_len == 0) break;
            *( (UChar*)(s->strm->next_out) ) = s->state_out_ch;
            HZ_UPDATE_CRC ( s->calculatedBlockCRC, s->state_out_ch );
            s->state_out_len--;
            s->strm->next_out++;
            s->strm->avail_out--;
            s->strm->total_out_lo32++;
            if (s->strm->total_out_lo32 == 0) s->strm->total_out_hi32++;
         }
   
         /* can a new run be started? */
         if (s->nblock_used == s->save_nblock+1) return False;

         /* Only caused by corrupt data stream? */
         if (s->nblock_used > s->save_nblock+1)
            return True;
   
         s->state_out_len = 1;
         s->state_out_ch = s->k0;
         HZ_GET_SMALL(k1); HZ_RAND_UPD_MASK; 
         k1 ^= HZ_RAND_MASK; s->nblock_used++;
         if (s->nblock_used == s->save_nblock+1) continue;
         if (k1 != s->k0) { s->k0 = k1; continue; };
   
         s->state_out_len = 2;
         HZ_GET_SMALL(k1); HZ_RAND_UPD_MASK; 
         k1 ^= HZ_RAND_MASK; s->nblock_used++;
         if (s->nblock_used == s->save_nblock+1) continue;
         if (k1 != s->k0) { s->k0 = k1; continue; };
   
         s->state_out_len = 3;
         HZ_GET_SMALL(k1); HZ_RAND_UPD_MASK; 
         k1 ^= HZ_RAND_MASK; s->nblock_used++;
         if (s->nblock_used == s->save_nblock+1) continue;
         if (k1 != s->k0) { s->k0 = k1; continue; };
   
         HZ_GET_SMALL(k1); HZ_RAND_UPD_MASK; 
         k1 ^= HZ_RAND_MASK; s->nblock_used++;
         s->state_out_len = ((Int32)k1) + 4;
         HZ_GET_SMALL(s->k0); HZ_RAND_UPD_MASK; 
         s->k0 ^= HZ_RAND_MASK; s->nblock_used++;
      }

   } else {

      while (True) {
         /* try to finish existing run */
         while (True) {
            if (s->strm->avail_out == 0) return False;
            if (s->state_out_len == 0) break;
            *( (UChar*)(s->strm->next_out) ) = s->state_out_ch;
            HZ_UPDATE_CRC ( s->calculatedBlockCRC, s->state_out_ch );
            s->state_out_len--;
            s->strm->next_out++;
            s->strm->avail_out--;
            s->strm->total_out_lo32++;
            if (s->strm->total_out_lo32 == 0) s->strm->total_out_hi32++;
         }
   
         /* can a new run be started? */
         if (s->nblock_used == s->save_nblock+1) return False;

         /* Only caused by corrupt data stream? */
         if (s->nblock_used > s->save_nblock+1)
            return True;
   
         s->state_out_len = 1;
         s->state_out_ch = s->k0;
         HZ_GET_SMALL(k1); s->nblock_used++;
         if (s->nblock_used == s->save_nblock+1) continue;
         if (k1 != s->k0) { s->k0 = k1; continue; };
   
         s->state_out_len = 2;
         HZ_GET_SMALL(k1); s->nblock_used++;
         if (s->nblock_used == s->save_nblock+1) continue;
         if (k1 != s->k0) { s->k0 = k1; continue; };
   
         s->state_out_len = 3;
         HZ_GET_SMALL(k1); s->nblock_used++;
         if (s->nblock_used == s->save_nblock+1) continue;
         if (k1 != s->k0) { s->k0 = k1; continue; };
   
         HZ_GET_SMALL(k1); s->nblock_used++;
         s->state_out_len = ((Int32)k1) + 4;
         HZ_GET_SMALL(s->k0); s->nblock_used++;
      }

   }
}


/*---------------------------------------------------*/
int HZ_API(HZ2_hzDecompress) ( hz_stream *strm )
{
   Bool    corrupt;
   DState* s;
   if (strm == NULL) return HZ_PARAM_ERROR;
   s = strm->state;
   if (s == NULL) return HZ_PARAM_ERROR;
   if (s->strm != strm) return HZ_PARAM_ERROR;

   while (True) {
      if (s->state == HZ_X_IDLE) return HZ_SEQUENCE_ERROR;
      if (s->state == HZ_X_OUTPUT) {
         if (s->smallDecompress)
            corrupt = unRLE_obuf_to_output_SMALL ( s ); else
            corrupt = unRLE_obuf_to_output_FAST  ( s );
         if (corrupt) return HZ_DATA_ERROR;
         if (s->nblock_used == s->save_nblock+1 && s->state_out_len == 0) {
            HZ_FINALISE_CRC ( s->calculatedBlockCRC );
            if (s->verbosity >= 3) 
               VPrintf2 ( " {0x%08x, 0x%08x}", s->storedBlockCRC, 
                          s->calculatedBlockCRC );
            if (s->verbosity >= 2) VPrintf0 ( "]" );
            if (s->calculatedBlockCRC != s->storedBlockCRC)
               return HZ_DATA_ERROR;
            s->calculatedCombinedCRC 
               = (s->calculatedCombinedCRC << 1) | 
                    (s->calculatedCombinedCRC >> 31);
            s->calculatedCombinedCRC ^= s->calculatedBlockCRC;
            s->state = HZ_X_BLKHDR_1;
         } else {
            return HZ_OK;
         }
      }
      if (s->state >= HZ_X_MAGIC_1) {
         Int32 r = HZ2_decompress ( s );
         if (r == HZ_STREAM_END) {
            if (s->verbosity >= 3)
               VPrintf2 ( "\n    combined CRCs: stored = 0x%08x, computed = 0x%08x", 
                          s->storedCombinedCRC, s->calculatedCombinedCRC );
            if (s->calculatedCombinedCRC != s->storedCombinedCRC)
               return HZ_DATA_ERROR;
            return r;
         }
         if (s->state != HZ_X_OUTPUT) return r;
      }
   }

   AssertH ( 0, 6001 );

   return 0;  /*NOTREACHED*/
}


/*---------------------------------------------------*/
int HZ_API(HZ2_hzDecompressEnd)  ( hz_stream *strm )
{
   DState* s;
   if (strm == NULL) return HZ_PARAM_ERROR;
   s = strm->state;
   if (s == NULL) return HZ_PARAM_ERROR;
   if (s->strm != strm) return HZ_PARAM_ERROR;

   if (s->tt   != NULL) HZFREE(s->tt);
   if (s->ll16 != NULL) HZFREE(s->ll16);
   if (s->ll4  != NULL) HZFREE(s->ll4);

   HZFREE(strm->state);
   strm->state = NULL;

   return HZ_OK;
}


#ifndef HZ_NO_STDIO
/*---------------------------------------------------*/
/*--- File I/O stuff                              ---*/
/*---------------------------------------------------*/

#define HZ_SETERR(eee)                    \
{                                         \
   if (hzerror != NULL) *hzerror = eee;   \
   if (hzf != NULL) hzf->lastErr = eee;   \
}

typedef 
   struct {
      FILE*     handle;
      Char      buf[HZ_MAX_UNUSED];
      Int32     bufN;
      Bool      writing;
      hz_stream strm;
      Int32     lastErr;
      Bool      initialisedOk;
   }
   hzFile;


/*---------------------------------------------*/
static Bool isEndOfFile ( FILE* f )
{
   Int32 c = fgetc ( f );
   if (c == EOF) return True;
   ungetc ( c, f );
   return False;
}


/*---------------------------------------------------*/
HZFILE* HZ_API(HZ2_hzWriteOpen) 
                    ( int*  hzerror,      
                      FILE* dstStream, 
                      int   blockSize100k, 
                      int   verbosity,
                      int   workFactor )
{
   Int32   ret;
   hzFile* hzf = NULL;

   HZ_SETERR(HZ_OK);

   if (dstStream == NULL ||
       (blockSize100k < 1 || blockSize100k > 9) ||
       (workFactor < 0 || workFactor > 250) ||
       (verbosity < 0 || verbosity > 4))
      { HZ_SETERR(HZ_PARAM_ERROR); return NULL; };

   if (ferror(dstStream))
      { HZ_SETERR(HZ_IO_ERROR); return NULL; };

   hzf = malloc ( sizeof(hzFile) );
   if (hzf == NULL)
      { HZ_SETERR(HZ_MEM_ERROR); return NULL; };

   HZ_SETERR(HZ_OK);
   hzf->initialisedOk = False;
   hzf->bufN          = 0;
   hzf->handle        = dstStream;
   hzf->writing       = True;
   hzf->strm.hzalloc  = NULL;
   hzf->strm.hzfree   = NULL;
   hzf->strm.opaque   = NULL;

   if (workFactor == 0) workFactor = 30;
   ret = HZ2_hzCompressInit ( &(hzf->strm), blockSize100k, 
                              verbosity, workFactor );
   if (ret != HZ_OK)
      { HZ_SETERR(ret); free(hzf); return NULL; };

   hzf->strm.avail_in = 0;
   hzf->initialisedOk = True;
   return hzf;   
}



/*---------------------------------------------------*/
void HZ_API(HZ2_hzWrite)
             ( int*    hzerror, 
               HZFILE* b, 
               void*   buf, 
               int     len )
{
   Int32 n, n2, ret;
   hzFile* hzf = (hzFile*)b;

   HZ_SETERR(HZ_OK);
   if (hzf == NULL || buf == NULL || len < 0)
      { HZ_SETERR(HZ_PARAM_ERROR); return; };
   if (!(hzf->writing))
      { HZ_SETERR(HZ_SEQUENCE_ERROR); return; };
   if (ferror(hzf->handle))
      { HZ_SETERR(HZ_IO_ERROR); return; };

   if (len == 0)
      { HZ_SETERR(HZ_OK); return; };

   hzf->strm.avail_in = len;
   hzf->strm.next_in  = buf;

   while (True) {
      hzf->strm.avail_out = HZ_MAX_UNUSED;
      hzf->strm.next_out = hzf->buf;
      ret = HZ2_hzCompress ( &(hzf->strm), HZ_RUN );
      if (ret != HZ_RUN_OK)
         { HZ_SETERR(ret); return; };

      if (hzf->strm.avail_out < HZ_MAX_UNUSED) {
         n = HZ_MAX_UNUSED - hzf->strm.avail_out;
         n2 = fwrite ( (void*)(hzf->buf), sizeof(UChar), 
                       n, hzf->handle );
         if (n != n2 || ferror(hzf->handle))
            { HZ_SETERR(HZ_IO_ERROR); return; };
      }

      if (hzf->strm.avail_in == 0)
         { HZ_SETERR(HZ_OK); return; };
   }
}


/*---------------------------------------------------*/
void HZ_API(HZ2_hzWriteClose)
                  ( int*          hzerror, 
                    HZFILE*       b, 
                    int           abandon,
                    unsigned int* nbytes_in,
                    unsigned int* nbytes_out )
{
   HZ2_hzWriteClose64 ( hzerror, b, abandon, 
                        nbytes_in, NULL, nbytes_out, NULL );
}


void HZ_API(HZ2_hzWriteClose64)
                  ( int*          hzerror, 
                    HZFILE*       b, 
                    int           abandon,
                    unsigned int* nbytes_in_lo32,
                    unsigned int* nbytes_in_hi32,
                    unsigned int* nbytes_out_lo32,
                    unsigned int* nbytes_out_hi32 )
{
   Int32   n, n2, ret;
   hzFile* hzf = (hzFile*)b;

   if (hzf == NULL)
      { HZ_SETERR(HZ_OK); return; };
   if (!(hzf->writing))
      { HZ_SETERR(HZ_SEQUENCE_ERROR); return; };
   if (ferror(hzf->handle))
      { HZ_SETERR(HZ_IO_ERROR); return; };

   if (nbytes_in_lo32 != NULL) *nbytes_in_lo32 = 0;
   if (nbytes_in_hi32 != NULL) *nbytes_in_hi32 = 0;
   if (nbytes_out_lo32 != NULL) *nbytes_out_lo32 = 0;
   if (nbytes_out_hi32 != NULL) *nbytes_out_hi32 = 0;

   if ((!abandon) && hzf->lastErr == HZ_OK) {
      while (True) {
         hzf->strm.avail_out = HZ_MAX_UNUSED;
         hzf->strm.next_out = hzf->buf;
         ret = HZ2_hzCompress ( &(hzf->strm), HZ_FINISH );
         if (ret != HZ_FINISH_OK && ret != HZ_STREAM_END)
            { HZ_SETERR(ret); return; };

         if (hzf->strm.avail_out < HZ_MAX_UNUSED) {
            n = HZ_MAX_UNUSED - hzf->strm.avail_out;
            n2 = fwrite ( (void*)(hzf->buf), sizeof(UChar), 
                          n, hzf->handle );
            if (n != n2 || ferror(hzf->handle))
               { HZ_SETERR(HZ_IO_ERROR); return; };
         }

         if (ret == HZ_STREAM_END) break;
      }
   }

   if ( !abandon && !ferror ( hzf->handle ) ) {
      fflush ( hzf->handle );
      if (ferror(hzf->handle))
         { HZ_SETERR(HZ_IO_ERROR); return; };
   }

   if (nbytes_in_lo32 != NULL)
      *nbytes_in_lo32 = hzf->strm.total_in_lo32;
   if (nbytes_in_hi32 != NULL)
      *nbytes_in_hi32 = hzf->strm.total_in_hi32;
   if (nbytes_out_lo32 != NULL)
      *nbytes_out_lo32 = hzf->strm.total_out_lo32;
   if (nbytes_out_hi32 != NULL)
      *nbytes_out_hi32 = hzf->strm.total_out_hi32;

   HZ_SETERR(HZ_OK);
   HZ2_hzCompressEnd ( &(hzf->strm) );
   free ( hzf );
}


/*---------------------------------------------------*/
HZFILE* HZ_API(HZ2_hzReadOpen) 
                   ( int*  hzerror, 
                     FILE* f, 
                     int   verbosity,
                     int   small,
                     void* unused,
                     int   nUnused )
{
   hzFile* hzf = NULL;
   int     ret;

   HZ_SETERR(HZ_OK);

   if (f == NULL || 
       (small != 0 && small != 1) ||
       (verbosity < 0 || verbosity > 4) ||
       (unused == NULL && nUnused != 0) ||
       (unused != NULL && (nUnused < 0 || nUnused > HZ_MAX_UNUSED)))
      { HZ_SETERR(HZ_PARAM_ERROR); return NULL; };

   if (ferror(f))
      { HZ_SETERR(HZ_IO_ERROR); return NULL; };

   hzf = malloc ( sizeof(hzFile) );
   if (hzf == NULL) 
      { HZ_SETERR(HZ_MEM_ERROR); return NULL; };

   HZ_SETERR(HZ_OK);

   hzf->initialisedOk = False;
   hzf->handle        = f;
   hzf->bufN          = 0;
   hzf->writing       = False;
   hzf->strm.hzalloc  = NULL;
   hzf->strm.hzfree   = NULL;
   hzf->strm.opaque   = NULL;
   
   while (nUnused > 0) {
      hzf->buf[hzf->bufN] = *((UChar*)(unused)); hzf->bufN++;
      unused = ((void*)( 1 + ((UChar*)(unused))  ));
      nUnused--;
   }

   ret = HZ2_hzDecompressInit ( &(hzf->strm), verbosity, small );
   if (ret != HZ_OK)
      { HZ_SETERR(ret); free(hzf); return NULL; };

   hzf->strm.avail_in = hzf->bufN;
   hzf->strm.next_in  = hzf->buf;

   hzf->initialisedOk = True;
   return hzf;   
}


/*---------------------------------------------------*/
void HZ_API(HZ2_hzReadClose) ( int *hzerror, HZFILE *b )
{
   hzFile* hzf = (hzFile*)b;

   HZ_SETERR(HZ_OK);
   if (hzf == NULL)
      { HZ_SETERR(HZ_OK); return; };

   if (hzf->writing)
      { HZ_SETERR(HZ_SEQUENCE_ERROR); return; };

   if (hzf->initialisedOk)
      (void)HZ2_hzDecompressEnd ( &(hzf->strm) );
   free ( hzf );
}


/*---------------------------------------------------*/
int HZ_API(HZ2_hzRead) 
           ( int*    hzerror, 
             HZFILE* b, 
             void*   buf, 
             int     len )
{
   Int32   n, ret;
   hzFile* hzf = (hzFile*)b;

   HZ_SETERR(HZ_OK);

   if (hzf == NULL || buf == NULL || len < 0)
      { HZ_SETERR(HZ_PARAM_ERROR); return 0; };

   if (hzf->writing)
      { HZ_SETERR(HZ_SEQUENCE_ERROR); return 0; };

   if (len == 0)
      { HZ_SETERR(HZ_OK); return 0; };

   hzf->strm.avail_out = len;
   hzf->strm.next_out = buf;

   while (True) {

      if (ferror(hzf->handle)) 
         { HZ_SETERR(HZ_IO_ERROR); return 0; };

      if (hzf->strm.avail_in == 0 && !isEndOfFile(hzf->handle)) {
         n = fread ( hzf->buf, sizeof(UChar), 
                     HZ_MAX_UNUSED, hzf->handle );
         if (ferror(hzf->handle))
            { HZ_SETERR(HZ_IO_ERROR); return 0; };
         hzf->bufN = n;
         hzf->strm.avail_in = hzf->bufN;
         hzf->strm.next_in = hzf->buf;
      }

      ret = HZ2_hzDecompress ( &(hzf->strm) );

      if (ret != HZ_OK && ret != HZ_STREAM_END)
         { HZ_SETERR(ret); return 0; };

      if (ret == HZ_OK && isEndOfFile(hzf->handle) && 
          hzf->strm.avail_in == 0 && hzf->strm.avail_out > 0)
         { HZ_SETERR(HZ_UNEXPECTED_EOF); return 0; };

      if (ret == HZ_STREAM_END)
         { HZ_SETERR(HZ_STREAM_END);
           return len - hzf->strm.avail_out; };
      if (hzf->strm.avail_out == 0)
         { HZ_SETERR(HZ_OK); return len; };
      
   }

   return 0; /*not reached*/
}


/*---------------------------------------------------*/
void HZ_API(HZ2_hzReadGetUnused) 
                     ( int*    hzerror, 
                       HZFILE* b, 
                       void**  unused, 
                       int*    nUnused )
{
   hzFile* hzf = (hzFile*)b;
   if (hzf == NULL)
      { HZ_SETERR(HZ_PARAM_ERROR); return; };
   if (hzf->lastErr != HZ_STREAM_END)
      { HZ_SETERR(HZ_SEQUENCE_ERROR); return; };
   if (unused == NULL || nUnused == NULL)
      { HZ_SETERR(HZ_PARAM_ERROR); return; };

   HZ_SETERR(HZ_OK);
   *nUnused = hzf->strm.avail_in;
   *unused = hzf->strm.next_in;
}
#endif


/*---------------------------------------------------*/
/*--- Misc convenience stuff                      ---*/
/*---------------------------------------------------*/

/*---------------------------------------------------*/
int HZ_API(HZ2_hzBuffToBuffCompress) 
                         ( char*         dest, 
                           unsigned int* destLen,
                           char*         source, 
                           unsigned int  sourceLen,
                           int           blockSize100k, 
                           int           verbosity, 
                           int           workFactor )
{
   hz_stream strm;
   int ret;

   if (dest == NULL || destLen == NULL || 
       source == NULL ||
       blockSize100k < 1 || blockSize100k > 9 ||
       verbosity < 0 || verbosity > 4 ||
       workFactor < 0 || workFactor > 250) 
      return HZ_PARAM_ERROR;

   if (workFactor == 0) workFactor = 30;
   strm.hzalloc = NULL;
   strm.hzfree = NULL;
   strm.opaque = NULL;
   ret = HZ2_hzCompressInit ( &strm, blockSize100k, 
                              verbosity, workFactor );
   if (ret != HZ_OK) return ret;

   strm.next_in = source;
   strm.next_out = dest;
   strm.avail_in = sourceLen;
   strm.avail_out = *destLen;

   ret = HZ2_hzCompress ( &strm, HZ_FINISH );
   if (ret == HZ_FINISH_OK) goto output_overflow;
   if (ret != HZ_STREAM_END) goto errhandler;

   /* normal termination */
   *destLen -= strm.avail_out;   
   HZ2_hzCompressEnd ( &strm );
   return HZ_OK;

   output_overflow:
   HZ2_hzCompressEnd ( &strm );
   return HZ_OUTBUFF_FULL;

   errhandler:
   HZ2_hzCompressEnd ( &strm );
   return ret;
}


/*---------------------------------------------------*/
int HZ_API(HZ2_hzBuffToBuffDecompress) 
                           ( char*         dest, 
                             unsigned int* destLen,
                             char*         source, 
                             unsigned int  sourceLen,
                             int           small,
                             int           verbosity )
{
   hz_stream strm;
   int ret;

   if (dest == NULL || destLen == NULL || 
       source == NULL ||
       (small != 0 && small != 1) ||
       verbosity < 0 || verbosity > 4) 
          return HZ_PARAM_ERROR;

   strm.hzalloc = NULL;
   strm.hzfree = NULL;
   strm.opaque = NULL;
   ret = HZ2_hzDecompressInit ( &strm, verbosity, small );
   if (ret != HZ_OK) return ret;

   strm.next_in = source;
   strm.next_out = dest;
   strm.avail_in = sourceLen;
   strm.avail_out = *destLen;

   ret = HZ2_hzDecompress ( &strm );
   if (ret == HZ_OK) goto output_overflow_or_eof;
   if (ret != HZ_STREAM_END) goto errhandler;

   /* normal termination */
   *destLen -= strm.avail_out;
   HZ2_hzDecompressEnd ( &strm );
   return HZ_OK;

   output_overflow_or_eof:
   if (strm.avail_out > 0) {
      HZ2_hzDecompressEnd ( &strm );
      return HZ_UNEXPECTED_EOF;
   } else {
      HZ2_hzDecompressEnd ( &strm );
      return HZ_OUTBUFF_FULL;
   };      

   errhandler:
   HZ2_hzDecompressEnd ( &strm );
   return ret; 
}


/*---------------------------------------------------*/
/*--
   Code contributed by Yoshioka Tsuneo (tsuneo@rr.iij4u.or.jp)
   to support better zlib compatibility.
   This code is not _officially_ part of libhzip2 (yet);
   I haven't tested it, documented it, or considered the
   threading-safeness of it.
   If this code breaks, please contact both Yoshioka and me.
--*/
/*---------------------------------------------------*/

/*---------------------------------------------------*/
/*--
   return version like "0.9.5d, 4-Sept-1999".
--*/
const char * HZ_API(HZ2_hzlibVersion)(void)
{
   return HZ_VERSION;
}


#ifndef HZ_NO_STDIO
/*---------------------------------------------------*/

#if defined(_WIN32) || defined(OS2) || defined(MSDOS)
#   include <fcntl.h>
#   include <io.h>
#   define SET_BINARY_MODE(file) setmode(fileno(file),O_BINARY)
#else
#   define SET_BINARY_MODE(file)
#endif
static
HZFILE * hzopen_or_hzdopen
               ( const char *path,   /* no use when hzdopen */
                 int fd,             /* no use when hzdopen */
                 const char *mode,
                 int open_mode)      /* hzopen: 0, hzdopen:1 */
{
   int    hzerr;
   char   unused[HZ_MAX_UNUSED];
   int    blockSize100k = 9;
   int    writing       = 0;
   char   mode2[10]     = "";
   FILE   *fp           = NULL;
   HZFILE *hzfp         = NULL;
   int    verbosity     = 0;
   int    workFactor    = 30;
   int    smallMode     = 0;
   int    nUnused       = 0; 

   if (mode == NULL) return NULL;
   while (*mode) {
      switch (*mode) {
      case 'r':
         writing = 0; break;
      case 'w':
         writing = 1; break;
      case 's':
         smallMode = 1; break;
      default:
         if (isdigit((int)(*mode))) {
            blockSize100k = *mode-HZ_HDR_0;
         }
      }
      mode++;
   }
   strcat(mode2, writing ? "w" : "r" );
   strcat(mode2,"b");   /* binary mode */

   if (open_mode==0) {
      if (path==NULL || strcmp(path,"")==0) {
        fp = (writing ? stdout : stdin);
        SET_BINARY_MODE(fp);
      } else {
        fp = fopen(path,mode2);
      }
   } else {
#ifdef HZ_STRICT_ANSI
      fp = NULL;
#else
      fp = fdopen(fd,mode2);
#endif
   }
   if (fp == NULL) return NULL;

   if (writing) {
      /* Guard against total chaos and anarchy -- JRS */
      if (blockSize100k < 1) blockSize100k = 1;
      if (blockSize100k > 9) blockSize100k = 9; 
      hzfp = HZ2_hzWriteOpen(&hzerr,fp,blockSize100k,
                             verbosity,workFactor);
   } else {
      hzfp = HZ2_hzReadOpen(&hzerr,fp,verbosity,smallMode,
                            unused,nUnused);
   }
   if (hzfp == NULL) {
      if (fp != stdin && fp != stdout) fclose(fp);
      return NULL;
   }
   return hzfp;
}


/*---------------------------------------------------*/
/*--
   open file for read or write.
      ex) hzopen("file","w9")
      case path="" or NULL => use stdin or stdout.
--*/
HZFILE * HZ_API(HZ2_hzopen)
               ( const char *path,
                 const char *mode )
{
   return hzopen_or_hzdopen(path,-1,mode,/*hzopen*/0);
}


/*---------------------------------------------------*/
HZFILE * HZ_API(HZ2_hzdopen)
               ( int fd,
                 const char *mode )
{
   return hzopen_or_hzdopen(NULL,fd,mode,/*hzdopen*/1);
}


/*---------------------------------------------------*/
int HZ_API(HZ2_hzread) (HZFILE* b, void* buf, int len )
{
   int hzerr, nread;
   if (((hzFile*)b)->lastErr == HZ_STREAM_END) return 0;
   nread = HZ2_hzRead(&hzerr,b,buf,len);
   if (hzerr == HZ_OK || hzerr == HZ_STREAM_END) {
      return nread;
   } else {
      return -1;
   }
}


/*---------------------------------------------------*/
int HZ_API(HZ2_hzwrite) (HZFILE* b, void* buf, int len )
{
   int hzerr;

   HZ2_hzWrite(&hzerr,b,buf,len);
   if(hzerr == HZ_OK){
      return len;
   }else{
      return -1;
   }
}


/*---------------------------------------------------*/
int HZ_API(HZ2_hzflush) (HZFILE *b)
{
   /* do nothing now... */
   return 0;
}


/*---------------------------------------------------*/
void HZ_API(HZ2_hzclose) (HZFILE* b)
{
   int hzerr;
   FILE *fp;
   
   if (b==NULL) {return;}
   fp = ((hzFile *)b)->handle;
   if(((hzFile*)b)->writing){
      HZ2_hzWriteClose(&hzerr,b,0,NULL,NULL);
      if(hzerr != HZ_OK){
         HZ2_hzWriteClose(NULL,b,1,NULL,NULL);
      }
   }else{
      HZ2_hzReadClose(&hzerr,b);
   }
   if(fp!=stdin && fp!=stdout){
      fclose(fp);
   }
}


/*---------------------------------------------------*/
/*--
   return last error code 
--*/
static const char *hzerrorstrings[] = {
       "OK"
      ,"SEQUENCE_ERROR"
      ,"PARAM_ERROR"
      ,"MEM_ERROR"
      ,"DATA_ERROR"
      ,"DATA_ERROR_MAGIC"
      ,"IO_ERROR"
      ,"UNEXPECTED_EOF"
      ,"OUTBUFF_FULL"
      ,"CONFIG_ERROR"
      ,"???"   /* for future */
      ,"???"   /* for future */
      ,"???"   /* for future */
      ,"???"   /* for future */
      ,"???"   /* for future */
      ,"???"   /* for future */
};


const char * HZ_API(HZ2_hzerror) (HZFILE *b, int *errnum)
{
   int err = ((hzFile *)b)->lastErr;

   if(err>0) err = 0;
   *errnum = err;
   return hzerrorstrings[err*-1];
}
#endif


/*-------------------------------------------------------------*/
/*--- end                                           hzlib.c ---*/
/*-------------------------------------------------------------*/
