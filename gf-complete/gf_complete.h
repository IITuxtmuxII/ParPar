/* 
 * GF-Complete: A Comprehensive Open Source Library for Galois Field Arithmetic
 * James S. Plank, Ethan L. Miller, Kevin M. Greenan,
 * Benjamin A. Arnold, John A. Burnum, Adam W. Disney, Allen C. McBride.
 *
 * gf_complete.h
 *
 * The main include file for gf_complete. 
 */

#ifndef _GF_COMPLETE_H_
#define _GF_COMPLETE_H_
#include "../src/stdint.h"
#include <stddef.h>

#include "platform.h"

#ifdef INTEL_SSSE3
  #include <tmmintrin.h>
#endif

#ifdef INTEL_SSE2
  #include <emmintrin.h>
#endif

#ifdef INTEL_SSE4_PCLMUL
  #include <smmintrin.h>
  #include <wmmintrin.h>
#endif

#ifdef INTEL_AVX2
  #include <immintrin.h>
#endif

#ifdef INTEL_GFNI
  #include <immintrin.h>
#endif

#ifdef ARM_NEON
  #include <arm_neon.h>
#endif
#ifdef _M_ARM64 /* MSVC header */
  #include <arm64_neon.h>
#endif


/* These are the different ways to perform multiplication.
   Not all are implemented for all values of w.
   See the paper for an explanation of how they work. */

typedef enum {GF_MULT_DEFAULT,
              GF_COPY, /* memcpy: only for benchmarking */
              /* GF_MULT_CARRY_FREE,
              GF_MULT_CARRY_FREE_GK,
              GF_MULT_LOG_TABLE, */
              GF_MULT_SPLIT_TABLE,
              GF_MULT_XOR_DEPENDS,
              GF_MULT_AFFINE } gf_mult_type_t;

typedef enum {
	GF_SPLIT8,
	GF_SPLIT4,
	GF_SPLIT4_NEON,
	GF_SPLIT4_SSSE3,
	GF_SPLIT4_AVX2,
	GF_SPLIT4_AVX512,
	GF_XOR_SSE2,
	GF_XOR_JIT_SSE2,
	GF_XOR_JIT_AVX2,
	GF_XOR_JIT_AVX512,
	GF_AFFINE_GFNI,
	GF_AFFINE_AVX512
} gf_mult_method_used;

/* These are the different ways to optimize region 
   operations.  They are bits because you can compose them.
   Certain optimizations only apply to certain gf_mult_type_t's.  
   Again, please see documentation for how to use these */
   
#define GF_REGION_DEFAULT      (0x0)
#define GF_REGION_ALTMAP       (0x20)

typedef uint32_t gf_region_type_t;

/* These are different ways to implement division.
   Once again, it's best to use "DEFAULT".  However,
   there are times when you may want to experiment
   with the others. */

typedef enum { GF_DIVIDE_DEFAULT,
               GF_DIVIDE_MATRIX,
               GF_DIVIDE_EUCLID } gf_division_type_t;

/* We support w=4,8,16,32,64 and 128 with their own data types and
   operations for multiplication, division, etc.  We also support
   a "gen" type so that you can do general gf arithmetic for any 
   value of w from 1 to 32.  You can perform a "region" operation
   on these if you use "CAUCHY" as the mapping. 
 */

typedef uint32_t    gf_val_32_t;

extern int _gf_errno;
extern void gf_error();

typedef struct gf *GFP;

typedef union gf_func_a_b {
    gf_val_32_t  (*w32) (GFP gf, gf_val_32_t a,  gf_val_32_t b);
} gf_func_a_b;
  
typedef union {
  gf_val_32_t  (*w32) (GFP gf, gf_val_32_t a);
} gf_func_a;
  
typedef union {
  void  (*w32) (GFP gf, void *src, void *dest, gf_val_32_t val,  int bytes, int add);
} gf_region;

typedef union {
  void (*w16) (GFP gf, unsigned int numSrc, uintptr_t offset, void **src, void *dest, gf_val_32_t *val, int bytes, int add);
} gf_regionX;

typedef union {
  gf_val_32_t  (*w32) (GFP gf, void *start, int bytes, int index);
} gf_extract;

typedef void (*gf_altmap) (void *src, int bytes, void* dest);

typedef struct gf {
  gf_func_a_b    multiply;
  gf_func_a_b    divide;
  gf_func_a      inverse;
  gf_region      multiply_region;
  gf_regionX     multiply_regionX;
  gf_extract     extract_word;
  gf_altmap      altmap_region;
  gf_altmap      unaltmap_region;
  int            using_altmap;
  int            alignment;
  int            walignment;
  gf_mult_method_used mult_method;
  void           *scratch;
} gf_t;
    
/* Initializes the GF to defaults.  Pass it a pointer to a gf_t.
   Returns 0 on failure, 1 on success. */

extern int gf_init_easy(GFP gf, int w);

/* Initializes the GF changing the defaults.
   Returns 0 on failure, 1 on success.
   Pass it a pointer to a gf_t.
   For mult_type and divide_type, use one of gf_mult_type_t gf_divide_type_t .  
   For region_type, OR together the GF_REGION_xxx's defined above.  
   Use 0 as prim_poly for defaults.  Otherwise, the leading 1 is optional.
   Use NULL for scratch_memory to have init_hard allocate memory.  Otherwise,
   use gf_scratch_size() to determine how big scratch_memory has to be.
 */

extern int gf_init_hard(GFP gf, 
                        int w, 
                        int mult_type, 
                        int region_type, 
                        int divide_type, 
                        uint64_t prim_poly,
                        int arg1, 
                        int arg2,
                        size_t size_hint,
                        unsigned int thCount_hint,
                        int wordsize,
                        GFP base_gf,
                        void *scratch_memory);

/* Determines the size for scratch_memory.  
   Returns 0 on failure and non-zero on success. */

extern int gf_scratch_size(int w, 
                           int mult_type, 
                           int region_type, 
                           int divide_type, 
                           int arg1, 
                           int arg2);

/* This reports the gf_scratch_size of a gf_t that has already been created */

extern int gf_size(GFP gf);

/* Frees scratch memory if gf_init_easy/gf_init_hard called malloc.
   If recursive = 1, then it calls itself recursively on base_gf. */

extern int gf_free(GFP gf, int recursive);

/* This is support for inline single multiplications and divisions.
   I know it's yucky, but if you've got to be fast, you've got to be fast.
   We support inlining for w=4, w=8 and w=16.  

   To use inline multiplication and division with w=4 or 8, you should use the 
   default gf_t, or one with a single table.  Otherwise, gf_w4/8_get_mult_table()
   will return NULL. Similarly, with w=16, the gf_t must be LOG */

uint16_t *gf_w16_get_log_table(GFP gf);
uint16_t *gf_w16_get_mult_alog_table(GFP gf);
uint16_t *gf_w16_get_div_alog_table(GFP gf);

#define GF_W16_INLINE_MULT(log, alog, a, b) ((a) == 0 || (b) == 0) ? 0 : (alog[(uint32_t)log[a]+(uint32_t)log[b]])
#define GF_W16_INLINE_DIV(log, alog, a, b) ((a) == 0 || (b) == 0) ? 0 : (alog[(int)log[a]-(int)log[b]])
#endif
