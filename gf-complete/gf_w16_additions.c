
int has_ssse3 = 0;
int has_pclmul = 0;
int has_avx2 = 0;
int has_avx512bw = 0;
int has_htt = 0;

#include <assert.h>

#if !defined(_MSC_VER) && defined(INTEL_SSE2)
#include <cpuid.h>
#endif

#ifdef _MSC_VER
	#define _cpuid __cpuid
	#define _cpuidX __cpuidex
#else
	/* GCC seems to support this, I assume everyone else does too? */
	#define _cpuid(ar, eax) __cpuid(eax, ar[0], ar[1], ar[2], ar[3])
	#define _cpuidX(ar, eax, ecx) __cpuid_count(eax, ecx, ar[0], ar[1], ar[2], ar[3])
#endif


void detect_cpu(void) {
#ifdef INTEL_SSE2 /* if we can't compile SSE, there's not much point in checking CPU capabilities; we use this to eliminate ARM :P */
	int cpuInfo[4];
	int family, model, hasMulticore;
	_cpuid(cpuInfo, 1);
	hasMulticore = (cpuInfo[3] & (1<<28));
	#ifdef INTEL_SSSE3
	has_ssse3 = (cpuInfo[2] & 0x200);
	#endif
	#ifdef INTEL_SSE4_PCLMUL
	has_pclmul = (cpuInfo[2] & 0x2);
	#endif
	
	family = ((cpuInfo[0]>>8) & 0xf) + ((cpuInfo[0]>>20) & 0xff);
	model = ((cpuInfo[0]>>4) & 0xf) + ((cpuInfo[0]>>12) & 0xf0);
	
	if(family == 6) {
		/* from handy table at http://a4lg.com/tech/x86/database/x86-families-and-models.en.html */
		if(model == 0x1C || model == 0x26 || model == 0x27 || model == 0x35 || model == 0x36 || model == 0x37 || model == 0x4A || model == 0x4D) {
			/* we have a Bonnell/Silvermont CPU with a really slow pshufb instruction; pretend SSSE3 doesn't exist, as XOR_DEPENDS is much faster */
			has_ssse3 = 0;
		}
		if(model == 0x0F || model == 0x16) {
			/* Conroe CPU with relatively slow pshufb; pretend SSSE3 doesn't exist, as XOR_DEPENDS is generally faster */
			/* TODO: SPLIT4 is still faster for small blocksizes, so should prefer it then */
			has_ssse3 = 0;
		}
	}

#if !defined(_MSC_VER) || _MSC_VER >= 1600
	_cpuidX(cpuInfo, 7, 0);
	
	#ifdef INTEL_AVX2
	has_avx2 = (cpuInfo[1] & 0x20);
	#endif
	#ifdef INTEL_AVX512BW
	has_avx512bw = (cpuInfo[1] & 0x40010000) == 0x40010000;
	#endif
#endif
#endif /* INTEL_SSE2 */

	/* try to detect hyper-threading */
	has_htt = 0;
	if(hasMulticore) {
		/* only Intel CPUs have HT (VMs which obscure CPUID -> too bad) */
		_cpuid(cpuInfo, 0);
		if(cpuInfo[1] == 0x756E6547 && cpuInfo[2] == 0x6C65746E && cpuInfo[3] == 0x49656E69 && cpuInfo[0] >= 11) {
			_cpuidX(cpuInfo, 11, 0);
			if((cpuInfo[2] >> 8) & 0xFF == 1 // SMT level
			&& cpuInfo[1] & 0xFFFF > 1) // multiple threads per core
				has_htt = 1;
		}
	}
	
}


static
void gf_w16_log_region_alignment(gf_region_data *rd,
  gf_t *gf,
  void *src,
  void *dest,
  int bytes,
  uint64_t val,
  int xor,
  int align,
  int walign)
{
  unsigned long uls;
  struct gf_w16_logtable_data *ltd = (struct gf_w16_logtable_data *) ((gf_internal_t *) gf->scratch)->private;
  int log_val = ltd->log_tbl[val];
  uint16_t *sEnd = ((uint16_t*)src) + (bytes>>1);
  
/* never used, so don't bother setting them
  rd->gf = gf;
  rd->src = src;
  rd->dest = dest;
  rd->bytes = bytes;
  rd->val = val;
  rd->xor = xor;
*/

  uls = ((unsigned long) src) & (align-1);

  if (uls != (((unsigned long) dest) & (align-1)))
    assert(0);
  if ((bytes & 1) != 0)
    assert(0);

  if (uls != 0) uls = (align-uls);
  rd->s_start = (uint8_t *)src + uls;
  rd->d_start = (uint8_t *)dest + uls;
  bytes -= uls;
  bytes -= (bytes & (walign-1));
  rd->s_top = (uint8_t *)rd->s_start + bytes;
  rd->d_top = (uint8_t *)rd->d_start + bytes;

  /* slow multiply for init/end area */
  #define MUL_LOOP(op, src, dest, srcto) { \
    uint16_t *s16 = (uint16_t *)src, *d16 = (uint16_t *)dest; \
    while (s16 < (uint16_t *)(srcto)) { \
      *d16 op (*s16 == 0) ? 0 : GF_ANTILOG((int) ltd->log_tbl[*s16] + log_val); \
      s16++; \
      d16++; \
    } \
  }
  if (xor) {
    MUL_LOOP(^=, src, dest, rd->s_start)
    MUL_LOOP(^=, rd->s_top, rd->d_top, sEnd)
  } else {
    MUL_LOOP(=, src, dest, rd->s_start)
    MUL_LOOP(=, rd->s_top, rd->d_top, sEnd)
  }
  #undef MUL_LOOP
}


/* refers to log_val, ltd and xor */
/* TODO: are we going to make use of these? */
#define _GF_W16_LOG_MULTIPLY_REGION(op, src, dest, srcto) { \
  uint16_t *s16 = (uint16_t *)src, *d16 = (uint16_t *)dest; \
  while (s16 < (uint16_t *)(srcto)) { \
    *d16 op (*s16 == 0) ? 0 : GF_ANTILOG((int) ltd->log_tbl[*s16] + log_val); \
    s16++; \
    d16++; \
  } \
}
#define GF_W16_LOG_MULTIPLY_REGION(src, dest, srcto) \
  if(xor) _GF_W16_LOG_MULTIPLY_REGION(^=, src, dest, srcto) \
  else _GF_W16_LOG_MULTIPLY_REGION(=, src, dest, srcto)



#ifdef INTEL_SSE2
static void gf_w16_xor_start(void* src, int bytes, void* dest) {
	gf_region_data rd;
	__m128i *sW;
	uint16_t *d16, *top16;
	uint16_t dtmp[128];
	__m128i ta, tb, lmask, th, tl;
	int i, j;
	
	lmask = _mm_set1_epi16(0xff);
	
	if(((uintptr_t)src & 0xF) != ((uintptr_t)dest & 0xF)) {
		// unaligned version, note that we go by destination alignment
		gf_set_region_data(&rd, NULL, dest, dest, bytes, 0, 0, 16, 256);
		
		memcpy(rd.d_top, (void*)((uintptr_t)src + (uintptr_t)rd.d_top - (uintptr_t)rd.dest), (uintptr_t)rd.dest + rd.bytes - (uintptr_t)rd.d_top);
		memcpy(rd.dest, src, (uintptr_t)rd.d_start - (uintptr_t)rd.dest);
		
		sW = (__m128i*)((uintptr_t)src + (uintptr_t)rd.d_start - (uintptr_t)rd.dest);
		d16 = (uint16_t*)rd.d_start;
		top16 = (uint16_t*)rd.d_top;
		
		while(d16 != top16) {
			for(j=0; j<8; j++) {
				ta = _mm_loadu_si128( sW);
				tb = _mm_loadu_si128(sW+1);
				
				/* split to high/low parts */
				th = _mm_packus_epi16(
					_mm_srli_epi16(tb, 8),
					_mm_srli_epi16(ta, 8)
				);
				tl = _mm_packus_epi16(
					_mm_and_si128(tb, lmask),
					_mm_and_si128(ta, lmask)
				);
				
				/* save to dest by extracting 16-bit masks */
				dtmp[0+j] = _mm_movemask_epi8(th);
				for(i=1; i<8; i++) {
					th = _mm_slli_epi16(th, 1); // byte shift would be nicer, but ultimately doesn't matter here
					dtmp[i*8+j] = _mm_movemask_epi8(th);
				}
				dtmp[64+j] = _mm_movemask_epi8(tl);
				for(i=1; i<8; i++) {
					tl = _mm_slli_epi16(tl, 1);
					dtmp[64+i*8+j] = _mm_movemask_epi8(tl);
				}
				sW += 2;
			}
			memcpy(d16, dtmp, sizeof(dtmp));
			d16 += 128; /*==15*8*/
		}
	} else {
		// standard, aligned version
		gf_set_region_data(&rd, NULL, src, dest, bytes, 0, 0, 16, 256);
		
		
		if(src != dest) {
			/* copy end and initial parts */
			memcpy(rd.d_top, rd.s_top, (uintptr_t)rd.src + rd.bytes - (uintptr_t)rd.s_top);
			memcpy(rd.dest, rd.src, (uintptr_t)rd.s_start - (uintptr_t)rd.src);
		}
		
		sW = (__m128i*)rd.s_start;
		d16 = (uint16_t*)rd.d_start;
		top16 = (uint16_t*)rd.d_top;
		
		while(d16 != top16) {
			for(j=0; j<8; j++) {
				ta = _mm_load_si128( sW);
				tb = _mm_load_si128(sW+1);
				
				/* split to high/low parts */
				th = _mm_packus_epi16(
					_mm_srli_epi16(tb, 8),
					_mm_srli_epi16(ta, 8)
				);
				tl = _mm_packus_epi16(
					_mm_and_si128(tb, lmask),
					_mm_and_si128(ta, lmask)
				);
				
				/* save to dest by extracting 16-bit masks */
				dtmp[0+j] = _mm_movemask_epi8(th);
				for(i=1; i<8; i++) {
					th = _mm_slli_epi16(th, 1); // byte shift would be nicer, but ultimately doesn't matter here
					dtmp[i*8+j] = _mm_movemask_epi8(th);
				}
				dtmp[64+j] = _mm_movemask_epi8(tl);
				for(i=1; i<8; i++) {
					tl = _mm_slli_epi16(tl, 1);
					dtmp[64+i*8+j] = _mm_movemask_epi8(tl);
				}
				sW += 2;
			}
			/* we only really need to copy temp -> dest if src==dest */
			memcpy(d16, dtmp, sizeof(dtmp));
			d16 += 128;
		}
	}
}


static void gf_w16_xor_final(void* src, int bytes, void* dest) {
	gf_region_data rd;
	uint16_t *s16, *d16, *top16;
	__m128i ta, tb, lmask, th, tl;
	uint16_t dtmp[128];
	int i, j;
	
	/*shut up compiler warning*/
	th = _mm_setzero_si128();
	tl = _mm_setzero_si128();
	
	if(((uintptr_t)src & 0xF) != ((uintptr_t)dest & 0xF)) {
		// unaligned version, note that we go by src alignment
		gf_set_region_data(&rd, NULL, src, src, bytes, 0, 0, 16, 256);
		
		memcpy((void*)((uintptr_t)dest + (uintptr_t)rd.s_top - (uintptr_t)rd.src), rd.s_top, (uintptr_t)rd.src + rd.bytes - (uintptr_t)rd.s_top);
		memcpy(dest, rd.src, (uintptr_t)rd.s_start - (uintptr_t)rd.src);
		
		d16 = (uint16_t*)((uintptr_t)dest + (uintptr_t)rd.s_start - (uintptr_t)rd.src);
	} else {
		// standard, aligned version
		gf_set_region_data(&rd, NULL, src, dest, bytes, 0, 0, 16, 256);
		
		
		if(src != dest) {
			/* copy end and initial parts */
			memcpy(rd.d_top, rd.s_top, (uintptr_t)rd.src + rd.bytes - (uintptr_t)rd.s_top);
			memcpy(rd.dest, rd.src, (uintptr_t)rd.s_start - (uintptr_t)rd.src);
		}
		
		d16 = (uint16_t*)rd.d_start;
	}
	
	lmask = _mm_set1_epi16(0xff);
	s16 = (uint16_t*)rd.s_start;
	top16 = (uint16_t*)rd.s_top;
	while(s16 != top16) {
		for(j=0; j<8; j++) {
			/* load in pattern: [0011223344556677] [8899AABBCCDDEEFF] */
			/* MSVC _requires_ a constant so we have to manually unroll this loop */
			#define MM_INSERT(i) \
				tl = _mm_insert_epi16(tl, s16[120 - i*8], i); \
				th = _mm_insert_epi16(th, s16[ 56 - i*8], i)
			MM_INSERT(0);
			MM_INSERT(1);
			MM_INSERT(2);
			MM_INSERT(3);
			MM_INSERT(4);
			MM_INSERT(5);
			MM_INSERT(6);
			MM_INSERT(7);
			#undef MM_INSERT
			
			/* swizzle to [0123456789ABCDEF] [0123456789ABCDEF] */
			ta = _mm_packus_epi16(
				_mm_srli_epi16(tl, 8),
				_mm_srli_epi16(th, 8)
			);
			tb = _mm_packus_epi16(
				_mm_and_si128(tl, lmask),
				_mm_and_si128(th, lmask)
			);
			
			/* extract top bits */
			dtmp[j*16 + 7] = _mm_movemask_epi8(ta);
			dtmp[j*16 + 15] = _mm_movemask_epi8(tb);
			for(i=1; i<8; i++) {
				ta = _mm_slli_epi16(ta, 1);
				tb = _mm_slli_epi16(tb, 1);
				dtmp[j*16 + 7-i] = _mm_movemask_epi8(ta);
				dtmp[j*16 + 15-i] = _mm_movemask_epi8(tb);
			}
			s16++;
		}
		/* we only really need to copy temp -> dest if src==dest */
		memcpy(d16, dtmp, sizeof(dtmp));
		d16 += 128;
		s16 += 128 - 8; /*==15*8*/
	}
}
#endif /*INTEL_SSE2*/

static gf_val_32_t
#ifdef __GNUC__
__attribute__ ((unused))
#endif
gf_w16_xor_extract_word(gf_t *gf, void *start, int bytes, int index)
{
  uint16_t *r16, rv = 0;
  uint8_t *r8;
  int i;
  gf_region_data rd;

  gf_set_region_data(&rd, gf, start, start, bytes, 0, 0, 16, 256);
  r16 = (uint16_t *) start;
  if (r16 + index < (uint16_t *) rd.d_start) return r16[index];
  if (r16 + index >= (uint16_t *) rd.d_top) return r16[index];
  
  index -= (((uint16_t *) rd.d_start) - r16);
  r8 = (uint8_t *) rd.d_start;
  r8 += (index & ~0x7f)*2; /* advance pointer to correct group */
  r8 += (index >> 3) & 0xF; /* advance to correct byte */
  for (i=0; i<16; i++) {
    rv <<= 1;
    rv |= (*r8 >> (7-(index & 7)) & 1);
    r8 += 16;
  }
  return rv;
}


#ifdef INTEL_SSE2
static void gf_w16_xor_lazy_sse_altmap_multiply_region(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor)
{
  FAST_U32 i, bit;
  FAST_U32 counts[16];
  uintptr_t deptable[16][16];
  __m128i depmask1, depmask2, polymask1, polymask2, addvals1, addvals2;
  uint16_t tmp_depmask[16];
  gf_region_data rd;
  gf_internal_t *h;
  __m128i *dW, *topW;
  uintptr_t sP;

  if (val == 0) { gf_multby_zero(dest, bytes, xor); return; }
  if (val == 1) { gf_multby_one(src, dest, bytes, xor); return; }

  h = (gf_internal_t *) gf->scratch;
  gf_w16_log_region_alignment(&rd, gf, src, dest, bytes, val, xor, 16, 256);
  
  /* calculate dependent bits */
  addvals1 = _mm_set_epi16(1<< 7, 1<< 6, 1<< 5, 1<< 4, 1<< 3, 1<< 2, 1<<1, 1<<0);
  addvals2 = _mm_set_epi16(1<<15, 1<<14, 1<<13, 1<<12, 1<<11, 1<<10, 1<<9, 1<<8);
  
  /* duplicate each bit in the polynomial 16 times */
  polymask2 = _mm_set1_epi16(h->prim_poly & 0xFFFF); /* chop off top bit, although not really necessary */
  polymask1 = _mm_and_si128(polymask2, _mm_set_epi16(1<< 8, 1<< 9, 1<<10, 1<<11, 1<<12, 1<<13, 1<<14, 1<<15));
  polymask2 = _mm_and_si128(polymask2, _mm_set_epi16(1<< 0, 1<< 1, 1<< 2, 1<< 3, 1<< 4, 1<< 5, 1<< 6, 1<< 7));
  polymask1 = _mm_cmpeq_epi16(_mm_setzero_si128(), polymask1);
  polymask2 = _mm_cmpeq_epi16(_mm_setzero_si128(), polymask2);
  
  if(val & (1<<15)) {
    /* XOR */
    depmask1 = addvals1;
    depmask2 = addvals2;
  } else {
    depmask1 = _mm_setzero_si128();
    depmask2 = _mm_setzero_si128();
  }
  for(i=(1<<14); i; i>>=1) {
    /* rotate */
    __m128i last = _mm_shuffle_epi32(_mm_shufflelo_epi16(depmask1, 0), 0);
    depmask1 = _mm_insert_epi16(
      _mm_srli_si128(depmask1, 2),
      _mm_extract_epi16(depmask2, 0),
      7
    );
    depmask2 = _mm_srli_si128(depmask2, 2);
    
    /* XOR poly */
    depmask1 = _mm_xor_si128(depmask1, _mm_andnot_si128(polymask1, last));
    depmask2 = _mm_xor_si128(depmask2, _mm_andnot_si128(polymask2, last));
    
    if(val & i) {
      /* XOR */
      depmask1 = _mm_xor_si128(depmask1, addvals1);
      depmask2 = _mm_xor_si128(depmask2, addvals2);
    }
  }
  
  /* generate needed tables */
  _mm_storeu_si128((__m128i*)(tmp_depmask), depmask1);
  _mm_storeu_si128((__m128i*)(tmp_depmask + 8), depmask2);
  for(bit=0; bit<16; bit++) {
    FAST_U32 cnt = 0;
    for(i=0; i<16; i++) {
      if(tmp_depmask[bit] & (1<<i)) {
        deptable[bit][cnt++] = i<<4; /* pre-multiply because x86 addressing can't do a x16; this saves a shift operation later */
      }
    }
    counts[bit] = cnt;
  }
  
  
  sP = (uintptr_t) rd.s_start;
  dW = (__m128i *) rd.d_start;
  topW = (__m128i *) rd.d_top;
  
  if ((sP - (uintptr_t)dW + 256) < 512) {
    /* urgh, src and dest are in the same block, so we need to store results to a temp location */
    __m128i dest[16];
    if (xor)
      while (dW != topW) {
        #define STEP(bit, type, typev, typed) { \
          uintptr_t* deps = deptable[bit]; \
          dest[bit] = _mm_load_ ## type((typed*)(dW + bit)); \
          switch(counts[bit]) { \
            case 16: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[15])); \
            case 15: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[14])); \
            case 14: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[13])); \
            case 13: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[12])); \
            case 12: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[11])); \
            case 11: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[10])); \
            case 10: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 9])); \
            case  9: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 8])); \
            case  8: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 7])); \
            case  7: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 6])); \
            case  6: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 5])); \
            case  5: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 4])); \
            case  4: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 3])); \
            case  3: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 2])); \
            case  2: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 1])); \
            case  1: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 0])); \
          } \
        }
        STEP( 0, si128, __m128i, __m128i)
        STEP( 1, si128, __m128i, __m128i)
        STEP( 2, si128, __m128i, __m128i)
        STEP( 3, si128, __m128i, __m128i)
        STEP( 4, si128, __m128i, __m128i)
        STEP( 5, si128, __m128i, __m128i)
        STEP( 6, si128, __m128i, __m128i)
        STEP( 7, si128, __m128i, __m128i)
        STEP( 8, si128, __m128i, __m128i)
        STEP( 9, si128, __m128i, __m128i)
        STEP(10, si128, __m128i, __m128i)
        STEP(11, si128, __m128i, __m128i)
        STEP(12, si128, __m128i, __m128i)
        STEP(13, si128, __m128i, __m128i)
        STEP(14, si128, __m128i, __m128i)
        STEP(15, si128, __m128i, __m128i)
        #undef STEP
        /* copy to dest */
        for(i=0; i<16; i++)
          _mm_store_si128(dW+i, dest[i]);
        dW += 16;
        sP += 256;
      }
    else
      while (dW != topW) {
        /* Note that we assume that all counts are at least 1; I don't think it's possible for that to be false */
        #define STEP(bit, type, typev, typed) { \
          uintptr_t* deps = deptable[bit]; \
          dest[bit] = _mm_load_ ## type((typed*)(sP + deps[ 0])); \
          switch(counts[bit]) { \
            case 16: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[15])); \
            case 15: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[14])); \
            case 14: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[13])); \
            case 13: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[12])); \
            case 12: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[11])); \
            case 11: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[10])); \
            case 10: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 9])); \
            case  9: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 8])); \
            case  8: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 7])); \
            case  7: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 6])); \
            case  6: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 5])); \
            case  5: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 4])); \
            case  4: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 3])); \
            case  3: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 2])); \
            case  2: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 1])); \
          } \
        }
        STEP( 0, si128, __m128i, __m128i)
        STEP( 1, si128, __m128i, __m128i)
        STEP( 2, si128, __m128i, __m128i)
        STEP( 3, si128, __m128i, __m128i)
        STEP( 4, si128, __m128i, __m128i)
        STEP( 5, si128, __m128i, __m128i)
        STEP( 6, si128, __m128i, __m128i)
        STEP( 7, si128, __m128i, __m128i)
        STEP( 8, si128, __m128i, __m128i)
        STEP( 9, si128, __m128i, __m128i)
        STEP(10, si128, __m128i, __m128i)
        STEP(11, si128, __m128i, __m128i)
        STEP(12, si128, __m128i, __m128i)
        STEP(13, si128, __m128i, __m128i)
        STEP(14, si128, __m128i, __m128i)
        STEP(15, si128, __m128i, __m128i)
        #undef STEP
        /* copy to dest */
        for(i=0; i<16; i++)
          _mm_store_si128(dW+i, dest[i]);
        dW += 16;
        sP += 256;
      }
  } else {
    if (xor)
      while (dW != topW) {
        #define STEP(bit, type, typev, typed) { \
          uintptr_t* deps = deptable[bit]; \
          typev tmp = _mm_load_ ## type((typed*)(dW + bit)); \
          switch(counts[bit]) { \
            case 16: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[15])); \
            case 15: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[14])); \
            case 14: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[13])); \
            case 13: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[12])); \
            case 12: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[11])); \
            case 11: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[10])); \
            case 10: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 9])); \
            case  9: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 8])); \
            case  8: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 7])); \
            case  7: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 6])); \
            case  6: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 5])); \
            case  5: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 4])); \
            case  4: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 3])); \
            case  3: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 2])); \
            case  2: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 1])); \
            case  1: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 0])); \
          } \
          _mm_store_ ## type((typed*)(dW + bit), tmp); \
        }
        STEP( 0, si128, __m128i, __m128i)
        STEP( 1, si128, __m128i, __m128i)
        STEP( 2, si128, __m128i, __m128i)
        STEP( 3, si128, __m128i, __m128i)
        STEP( 4, si128, __m128i, __m128i)
        STEP( 5, si128, __m128i, __m128i)
        STEP( 6, si128, __m128i, __m128i)
        STEP( 7, si128, __m128i, __m128i)
        STEP( 8, si128, __m128i, __m128i)
        STEP( 9, si128, __m128i, __m128i)
        STEP(10, si128, __m128i, __m128i)
        STEP(11, si128, __m128i, __m128i)
        STEP(12, si128, __m128i, __m128i)
        STEP(13, si128, __m128i, __m128i)
        STEP(14, si128, __m128i, __m128i)
        STEP(15, si128, __m128i, __m128i)
        #undef STEP
        dW += 16;
        sP += 256;
      }
    else
      while (dW != topW) {
        /* Note that we assume that all counts are at least 1; I don't think it's possible for that to be false */
        #define STEP(bit, type, typev, typed) { \
          uintptr_t* deps = deptable[bit]; \
          typev tmp = _mm_load_ ## type((typed*)(sP + deps[ 0])); \
          switch(counts[bit]) { \
            case 16: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[15])); \
            case 15: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[14])); \
            case 14: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[13])); \
            case 13: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[12])); \
            case 12: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[11])); \
            case 11: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[10])); \
            case 10: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 9])); \
            case  9: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 8])); \
            case  8: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 7])); \
            case  7: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 6])); \
            case  6: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 5])); \
            case  5: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 4])); \
            case  4: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 3])); \
            case  3: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 2])); \
            case  2: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 1])); \
          } \
          _mm_store_ ## type((typed*)(dW + bit), tmp); \
        }
        STEP( 0, si128, __m128i, __m128i)
        STEP( 1, si128, __m128i, __m128i)
        STEP( 2, si128, __m128i, __m128i)
        STEP( 3, si128, __m128i, __m128i)
        STEP( 4, si128, __m128i, __m128i)
        STEP( 5, si128, __m128i, __m128i)
        STEP( 6, si128, __m128i, __m128i)
        STEP( 7, si128, __m128i, __m128i)
        STEP( 8, si128, __m128i, __m128i)
        STEP( 9, si128, __m128i, __m128i)
        STEP(10, si128, __m128i, __m128i)
        STEP(11, si128, __m128i, __m128i)
        STEP(12, si128, __m128i, __m128i)
        STEP(13, si128, __m128i, __m128i)
        STEP(14, si128, __m128i, __m128i)
        STEP(15, si128, __m128i, __m128i)
        #undef STEP
        dW += 16;
        sP += 256;
      }
  }
  
}

#include "x86_jit.c"

static void gf_w16_xor_lazy_sse_jit_altmap_multiply_region(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor)
{
  FAST_U32 i, bit;
  long inBit;
  __m128i depmask1, depmask2, polymask1, polymask2, addvals1, addvals2;
  __m128i common_mask;
  uint16_t tmp_depmask[16], common_depmask[8];
  gf_region_data rd;
  gf_internal_t *h;
  jit_t* jit;
  uint8_t* pos_startloop;
  
  if (val == 0) { gf_multby_zero(dest, bytes, xor); return; }
  if (val == 1) { gf_multby_one(src, dest, bytes, xor); return; }

  h = (gf_internal_t *) gf->scratch;
  jit = &(h->jit);
  gf_w16_log_region_alignment(&rd, gf, src, dest, bytes, val, xor, 16, 256);
  
  if(rd.d_start != rd.d_top) {
    int use_temp = ((uintptr_t)rd.s_start - (uintptr_t)rd.d_start + 256) < 512;
    int setup_stack = 0;
    
    /* calculate dependent bits */
    addvals1 = _mm_set_epi16(1<< 7, 1<< 6, 1<< 5, 1<< 4, 1<< 3, 1<< 2, 1<<1, 1<<0);
    addvals2 = _mm_set_epi16(1<<15, 1<<14, 1<<13, 1<<12, 1<<11, 1<<10, 1<<9, 1<<8);
    
    /* duplicate each bit in the polynomial 16 times */
    polymask2 = _mm_set1_epi16(h->prim_poly & 0xFFFF); /* chop off top bit, although not really necessary */
    polymask1 = _mm_and_si128(polymask2, _mm_set_epi16(1<< 8, 1<< 9, 1<<10, 1<<11, 1<<12, 1<<13, 1<<14, 1<<15));
    polymask2 = _mm_and_si128(polymask2, _mm_set_epi16(1<< 0, 1<< 1, 1<< 2, 1<< 3, 1<< 4, 1<< 5, 1<< 6, 1<< 7));
    polymask1 = _mm_cmpeq_epi16(_mm_setzero_si128(), polymask1);
    polymask2 = _mm_cmpeq_epi16(_mm_setzero_si128(), polymask2);
    
    if(val & (1<<15)) {
      /* XOR */
      depmask1 = addvals1;
      depmask2 = addvals2;
    } else {
      depmask1 = _mm_setzero_si128();
      depmask2 = _mm_setzero_si128();
    }
    for(i=(1<<14); i; i>>=1) {
      /* rotate */
      __m128i last = _mm_shuffle_epi32(_mm_shufflelo_epi16(depmask1, 0), 0);
      depmask1 = _mm_insert_epi16(
        _mm_srli_si128(depmask1, 2),
        _mm_extract_epi16(depmask2, 0),
        7
      );
      depmask2 = _mm_srli_si128(depmask2, 2);
      
      /* XOR poly */
      depmask1 = _mm_xor_si128(depmask1, _mm_andnot_si128(polymask1, last));
      depmask2 = _mm_xor_si128(depmask2, _mm_andnot_si128(polymask2, last));
      
      if(val & i) {
        /* XOR */
        depmask1 = _mm_xor_si128(depmask1, addvals1);
        depmask2 = _mm_xor_si128(depmask2, addvals2);
      }
    }
    
    
    /* attempt to remove some redundant XOR ops with a simple heuristic */
    /* heuristic: we just find common XOR elements between bit pairs */
    
    if (!use_temp) {
      __m128i tmp1, tmp2;
      /* first, we need to re-arrange words so that we can perform bitwise AND on neighbouring pairs */
      /* unfortunately, PACKUSDW is SSE4.1 only, so emulate it with shuffles */
      /* 01234567 -> 02461357 */
      tmp1 = _mm_shuffle_epi32(
        _mm_shufflelo_epi16(
          _mm_shufflehi_epi16(depmask1, 0xD8), /* 0xD8 == 0b11011000 */
          0xD8
        ),
        0xD8
      );
      tmp2 = _mm_shuffle_epi32(
        _mm_shufflelo_epi16(
          _mm_shufflehi_epi16(depmask2, 0xD8),
          0xD8
        ),
        0xD8
      );
      common_mask = _mm_and_si128(
        /* [02461357, 8ACE9BDF] -> [02468ACE, 13579BDF]*/
        _mm_unpacklo_epi64(tmp1, tmp2),
        _mm_unpackhi_epi64(tmp1, tmp2)
      );
      /* we have the common elements between pairs, but it doesn't make sense to process a separate queue if there's only one common element (0 XORs), so eliminate those */
      common_mask = _mm_andnot_si128(_mm_cmpeq_epi16(
        _mm_setzero_si128(),
        /* "(v & (v-1)) == 0" is true if only zero/one bit is set in each word */
        _mm_and_si128(common_mask, _mm_sub_epi16(common_mask, _mm_set1_epi16(1)))
      ), common_mask);
      /* we now have a common elements mask without 1-bit words, just simply merge stuff in */
      depmask1 = _mm_xor_si128(depmask1, _mm_unpacklo_epi16(common_mask, common_mask));
      depmask2 = _mm_xor_si128(depmask2, _mm_unpackhi_epi16(common_mask, common_mask));
      _mm_storeu_si128((__m128i*)common_depmask, common_mask);
    } else {
      /* for now, don't bother with element elimination if we're using temp storage, as it's a little finnicky to implement */
      /*
      for(i=0; i<8; i++)
        common_depmask[i] = 0;
      */
    }
    
    _mm_storeu_si128((__m128i*)(tmp_depmask), depmask1);
    _mm_storeu_si128((__m128i*)(tmp_depmask + 8), depmask2);
    
    jit->ptr = jit->code;
    
#if defined(AMD64) && defined(_WINDOWS) || defined(__WINDOWS__) || defined(_WIN32) || defined(_WIN64)
    #define SAVE_XMM 1
    setup_stack = 1;
#elif !defined(AMD64)
    setup_stack = use_temp;
#endif

    if(setup_stack) {
      _jit_push(jit, BP);
      _jit_mov_r(jit, BP, SP);
      /* align pointer (avoid SP because stuff is encoded differently with it) */
      _jit_mov_r(jit, AX, SP);
      _jit_and_i(jit, AX, 0xF);
      _jit_sub_r(jit, BP, AX);
      
#ifdef SAVE_XMM
      /* make Windows happy and save XMM6-15 registers */
      /* ideally should be done by this function, not JIT code, but MSVC has a convenient policy of no inline ASM */
      for(i=6; i<16; i++)
        _jit_movaps_store(jit, BP, -((int32_t)i-5)*16, i);
#endif
    }
    
    /* adding 128 to the destination pointer allows the register offset to be coded in 1 byte
     * eg: 'movdqa xmm0, [rdx+0x90]' is 8 bytes, whilst 'movdqa xmm0, [rdx-0x60]' is 5 bytes */
    _jit_mov_i(jit, AX, (intptr_t)rd.s_start + 128);
    _jit_mov_i(jit, DX, (intptr_t)rd.d_start + 128);
    _jit_mov_i(jit, CX, (intptr_t)rd.d_top + 128);
    
    _jit_align32(jit);
    pos_startloop = jit->ptr;
    
    
    //_jit_movaps_load(jit, reg, xreg, offs)
    // (we just save a conditional by hardcoding this)
    #define _LD_APS(xreg, mreg, offs) \
        *(int32_t*)(jit->ptr) = 0x40280F + ((xreg) <<19) + ((mreg) <<16) + (((offs)&0xFF) <<24); \
        jit->ptr += 4
    #define _ST_APS(mreg, offs, xreg) \
        *(int32_t*)(jit->ptr) = 0x40290F + ((xreg) <<19) + ((mreg) <<16) + (((offs)&0xFF) <<24); \
        jit->ptr += 4
    #define _LD_APS64(xreg, mreg, offs) \
        *(int64_t*)(jit->ptr) = 0x40280F44 + ((xreg-8) <<27) + ((mreg) <<24) + ((int64_t)((offs)&0xFF) <<32); \
        jit->ptr += 5
    #define _ST_APS64(mreg, offs, xreg) \
        *(int64_t*)(jit->ptr) = 0x40290F44 + ((xreg-8) <<27) + ((mreg) <<24) + ((int64_t)((offs)&0xFF) <<32); \
        jit->ptr += 5

#ifdef AMD64
    #define _LD_DQA(xreg, mreg, offs) \
        *(int64_t*)(jit->ptr) = 0x406F0F66 + ((xreg) <<27) + ((mreg) <<24) + ((int64_t)((offs)&0xFF) <<32); \
        jit->ptr += 5
    #define _ST_DQA(mreg, offs, xreg) \
        *(int64_t*)(jit->ptr) = 0x407F0F66 + ((xreg) <<27) + ((mreg) <<24) + ((int64_t)((offs)&0xFF) <<32); \
        jit->ptr += 5
#else
    #define _LD_DQA(xreg, mreg, offs) \
        *(int32_t*)(jit->ptr) = 0x406F0F66 + ((xreg) <<27) + ((mreg) <<24); \
        *(jit->ptr +4) = (uint8_t)((offs)&0xFF); \
        jit->ptr += 5
    #define _ST_DQA(mreg, offs, xreg) \
        *(int32_t*)(jit->ptr) = 0x407F0F66 + ((xreg) <<27) + ((mreg) <<24); \
        *(jit->ptr +4) = (uint8_t)((offs)&0xFF); \
        jit->ptr += 5
#endif
    #define _LD_DQA64(xreg, mreg, offs) \
        *(int64_t*)(jit->ptr) = 0x406F0F4466 + ((int64_t)(xreg-8) <<35) + ((int64_t)(mreg) <<32) + ((int64_t)((offs)&0xFF) <<40); \
        jit->ptr += 6
    #define _ST_DQA64(mreg, offs, xreg) \
        *(int64_t*)(jit->ptr) = 0x407F0F4466 + ((int64_t)(xreg-8) <<35) + ((int64_t)(mreg) <<32) + ((int64_t)((offs)&0xFF) <<40); \
        jit->ptr += 6
    
    
    //_jit_xorps_m(jit, reg, AX, offs<<4);
    #define _XORPS_M_(reg, offs, tr) \
        *(int32_t*)(jit->ptr) = (0x40570F + ((reg) << 19) + (((offs)&0xFF) <<28)) ^ (tr)
    #define _C_XORPS_M(reg, offs, c) \
        _XORPS_M_(reg, offs, 0); \
        jit->ptr += (c)<<2
    #define _XORPS_M64_(reg, offs, tr) \
        *(int64_t*)(jit->ptr) = (0x40570F44 + (((reg)-8) << 27) + ((int64_t)((offs)&0xFF) <<36)) ^ ((tr)<<8)
    #define _C_XORPS_M64(reg, offs, c) \
        _XORPS_M64_(reg, offs, 0); \
        jit->ptr += ((c)<<2)+(c)
    
    //_jit_pxor_m(jit, 1, AX, offs<<4);
#ifdef AMD64
    #define _PXOR_M_(reg, offs, tr) \
        *(int64_t*)(jit->ptr) = (0x40EF0F66 + ((reg) << 27) + ((int64_t)((offs)&0xFF) << 36)) ^ (tr)
#else
    #define _PXOR_M_(reg, offs, tr) \
        *(int32_t*)(jit->ptr) = (0x40EF0F66 + ((reg) << 27)) ^ (tr); \
        *(jit->ptr +4) = (uint8_t)(((offs)&0xFF) << 4)
#endif
    #define _PXOR_M(reg, offs) \
        _PXOR_M_(reg, offs, 0); \
        jit->ptr += 5
    #define _C_PXOR_M(reg, offs, c) \
        _PXOR_M_(reg, offs, 0); \
        jit->ptr += ((c)<<2)+(c)
    #define _PXOR_M64_(reg, offs, tr) \
        *(int64_t*)(jit->ptr) = (0x40EF0F4466 + ((int64_t)((reg)-8) << 35) + ((int64_t)((offs)&0xFF) << 44)) ^ ((tr)<<8)
    #define _C_PXOR_M64(reg, offs, c) \
        _PXOR_M64_(reg, offs, 0); \
        jit->ptr += ((c)<<2)+((c)<<1)
    
    //_jit_xorps_r(jit, r2, r1)
    #define _XORPS_R_(r2, r1, tr) \
        *(int32_t*)(jit->ptr) = (0xC0570F + ((r2) <<19) + ((r1) <<16)) ^ (tr)
    #define _XORPS_R(r2, r1) \
        _XORPS_R_(r2, r1, 0); \
        jit->ptr += 3
    #define _C_XORPS_R(r2, r1, c) \
        _XORPS_R_(r2, r1, 0); \
        jit->ptr += ((c)<<1)+(c)
    // r2 is always < 8, r1 here is >= 8
    #define _XORPS_R64_(r2, r1, tr) \
        *(int32_t*)(jit->ptr) = (0xC0570F41 + ((r2) <<27) + ((r1) <<24)) ^ ((tr)<<8)
    #define _C_XORPS_R64(r2, r1, c) \
        _XORPS_R64_(r2, r1, 0); \
        jit->ptr += (c)<<2
    
    //_jit_pxor_r(jit, r2, r1)
    #define _PXOR_R_(r2, r1, tr) \
        *(int32_t*)(jit->ptr) = (0xC0EF0F66 + ((r2) <<27) + ((r1) <<24)) ^ (tr)
    #define _PXOR_R(r2, r1) \
        _PXOR_R_(r2, r1, 0); \
        jit->ptr += 4
    #define _C_PXOR_R(r2, r1, c) \
        _PXOR_R_(r2, r1, 0); \
        jit->ptr += (c)<<2
    #define _PXOR_R64_(r2, r1, tr) \
        *(int64_t*)(jit->ptr) = (0xC0EF0F4166 + ((int64_t)(r2) <<35) + ((int64_t)(r1) <<32)) ^ (((int64_t)tr)<<8)
    #define _C_PXOR_R64(r2, r1, c) \
        _PXOR_R64_(r2, r1, 0); \
        jit->ptr += ((c)<<2)+(c)
    
    /* optimised mix of xor/mov operations */
    #define _MOV_OR_XOR_FP_M(reg, offs, flag, c) \
        _XORPS_M_(reg, offs, flag); \
        flag &= (c)-1; \
        jit->ptr += (c)<<2
    #define _MOV_OR_XOR_FP_M64(reg, offs, flag, c) \
        _XORPS_M64_(reg, offs, flag); \
        flag &= (c)-1; \
        jit->ptr += ((c)<<2)+(c)
    #define _MOV_OR_XOR_FP_INIT (0x570F ^ 0x280F)
    
    #define _MOV_OR_XOR_INT_M(reg, offs, flag, c) \
        _PXOR_M_(reg, offs, flag); \
        flag &= (c)-1; \
        jit->ptr += ((c)<<2)+(c)
    #define _MOV_OR_XOR_INT_M64(reg, offs, flag, c) \
        _PXOR_M64_(reg, offs, flag); \
        flag &= (c)-1; \
        jit->ptr += ((c)<<2)+((c)<<1)
    #define _MOV_OR_XOR_INT_INIT (0xEF0F00 ^ 0x6F0F00)
    
    #define _MOV_OR_XOR_R_FP(r2, r1, flag, c) \
        _XORPS_R_(r2, r1, flag); \
        flag &= (c)-1; \
        jit->ptr += ((c)<<1)+(c)
    #define _MOV_OR_XOR_R64_FP(r2, r1, flag, c) \
        _XORPS_R64_(r2, r1, flag); \
        flag &= (c)-1; \
        jit->ptr += (c)<<2
    
    #define _MOV_OR_XOR_R_INT(r2, r1, flag, c) \
        _PXOR_R_(r2, r1, flag); \
        flag &= (c)-1; \
        jit->ptr += (c)<<2
    #define _MOV_OR_XOR_R64_INT(r2, r1, flag, c) \
        _PXOR_R64_(r2, r1, flag); \
        flag &= (c)-1; \
        jit->ptr += ((c)<<2)+(c)
    
    /* generate code */
    if (use_temp) {
      if(xor) {
        /* can fit everything in registers on 64-bit, otherwise, load half */
        for(bit=0; bit<8; bit+=2) {
          int destOffs = (bit<<4)-128;
          _LD_APS(bit, DX, destOffs);
          _LD_DQA(bit+1, DX, destOffs+16);
        }
#ifdef AMD64
        for(; bit<16; bit+=2) {
          int destOffs = (bit<<4)-128;
          _LD_APS64(bit, DX, destOffs);
          _LD_DQA64(bit+1, DX, destOffs+16);
        }
#endif
        for(bit=0; bit<8; bit+=2) {
          FAST_U16 mask1 = tmp_depmask[bit], mask2 = tmp_depmask[bit+1];
          for(inBit=-8; inBit<8; inBit++) {
            _C_XORPS_M(bit, inBit, mask1 & 1);
            _C_PXOR_M(bit+1, inBit, mask2 & 1);
            mask1 >>= 1;
            mask2 >>= 1;
          }
        }
#ifndef AMD64
        /*temp storage*/
        for(bit=0; bit<8; bit+=2) {
          _jit_movaps_store(jit, BP, -(bit<<4) -16, bit);
          _jit_movdqa_store(jit, BP, -((bit+1)<<4) -16, bit+1);
        }
        for(; bit<16; bit+=2) {
          int destOffs = (bit<<4)-128;
          _LD_APS(bit-8, DX, destOffs);
          _LD_DQA(bit-7, DX, destOffs+16);
        }
#endif
        for(bit=8; bit<16; bit+=2) {
          FAST_U16 mask1 = tmp_depmask[bit], mask2 = tmp_depmask[bit+1];
          for(inBit=-8; inBit<8; inBit++) {
#ifdef AMD64
            _C_XORPS_M64(bit, inBit, mask1 & 1);
            _C_PXOR_M64(bit+1, inBit, mask2 & 1);
#else
            _C_XORPS_M(bit-8, inBit, mask1 & 1);
            _C_PXOR_M(bit-7, inBit, mask2 & 1);
#endif
            mask1 >>= 1;
            mask2 >>= 1;
          }
        }
      } else {
        for(bit=0; bit<8; bit+=2) {
          FAST_U32 mov1 = _MOV_OR_XOR_FP_INIT, mov2 = _MOV_OR_XOR_INT_INIT;
          FAST_U16 mask1 = tmp_depmask[bit], mask2 = tmp_depmask[bit+1];
          for(inBit=-8; inBit<8; inBit++) {
            _MOV_OR_XOR_FP_M(bit, inBit, mov1, mask1 & 1);
            _MOV_OR_XOR_INT_M(bit+1, inBit, mov2, mask2 & 1);
            mask1 >>= 1;
            mask2 >>= 1;
          }
        }
#ifndef AMD64
        /*temp storage*/
        for(bit=0; bit<8; bit+=2) {
          _jit_movaps_store(jit, BP, -((int32_t)bit<<4) -16, bit);
          _jit_movdqa_store(jit, BP, -(((int32_t)bit+1)<<4) -16, bit+1);
        }
#endif
        for(bit=8; bit<16; bit+=2) {
          FAST_U32 mov1 = _MOV_OR_XOR_FP_INIT, mov2 = _MOV_OR_XOR_INT_INIT;
          FAST_U16 mask1 = tmp_depmask[bit], mask2 = tmp_depmask[bit+1];
          for(inBit=-8; inBit<8; inBit++) {
#ifdef AMD64
            _MOV_OR_XOR_FP_M64(bit, inBit, mov1, mask1 & 1);
            _MOV_OR_XOR_INT_M64(bit+1, inBit, mov2, mask2 & 1);
#else
            _MOV_OR_XOR_FP_M(bit-8, inBit, mov1, mask1 & 1);
            _MOV_OR_XOR_INT_M(bit-7, inBit, mov2, mask2 & 1);
#endif
            mask1 >>= 1;
            mask2 >>= 1;
          }
        }
      }
      
#ifdef AMD64
      for(bit=0; bit<8; bit+=2) {
        int destOffs = (bit<<4)-128;
        _ST_APS(DX, destOffs, bit);
        _ST_DQA(DX, destOffs+16, bit+1);
      }
      for(; bit<16; bit+=2) {
        int destOffs = (bit<<4)-128;
        _ST_APS64(DX, destOffs, bit);
        _ST_DQA64(DX, destOffs+16, bit+1);
      }
#else
      for(bit=8; bit<16; bit+=2) {
        int destOffs = (bit<<4)-128;
        _ST_APS(DX, destOffs, bit -8);
        _ST_DQA(DX, destOffs+16, bit -7);
      }
      /* copy temp */
      for(bit=0; bit<8; bit++) {
        _jit_movaps_load(jit, 0, BP, -((int32_t)bit<<4) -16);
        _ST_APS(DX, (bit<<4)-128, 0);
      }
#endif
      
    } else {
#ifdef AMD64
      /* preload upper 13 inputs into registers */
      #define _XORS_FROM_MEMORY 3
      for(inBit=3; inBit<8; inBit++) {
        _LD_APS(inBit, AX, (inBit-8)<<4);
      }
      for(; inBit<16; inBit++) {
        _LD_APS64(inBit, AX, (inBit-8)<<4);
      }
#else
      /* can only fit 5 in 32-bit mode :( */
      #define _XORS_FROM_MEMORY 11
      for(inBit=3; inBit<8; inBit++) { /* despite appearances, we're actually loading the top 5, not mid 5 */
        _LD_APS(inBit, AX, inBit<<4);
      }
#endif
      if(xor) {
        for(bit=0; bit<16; bit+=2) {
          int destOffs = (bit<<4)-128;
          int destOffs2 = destOffs+16;
          FAST_U32 movC = _MOV_OR_XOR_INT_INIT;
          FAST_U16 mask1 = tmp_depmask[bit], mask2 = tmp_depmask[bit+1],
                   maskC = common_depmask[bit>>1];
          _LD_APS(0, DX, destOffs);
          _LD_DQA(1, DX, destOffs2);
          
          for(inBit=-8; inBit<(_XORS_FROM_MEMORY-8); inBit++) {
            _MOV_OR_XOR_INT_M(2, inBit, movC, maskC & 1);
            _C_XORPS_M(0, inBit, mask1 & 1);
            _C_PXOR_M(1, inBit, mask2 & 1);
            mask1 >>= 1;
            mask2 >>= 1;
            maskC >>= 1;
          }
          /* at least 5 can come from registers */
          for(inBit=3; inBit<8; inBit++) {
            _MOV_OR_XOR_R_INT(2, inBit, movC, maskC & 1);
            _C_XORPS_R(0, inBit, mask1 & 1);
            _C_PXOR_R(1, inBit, mask2 & 1);
            mask1 >>= 1;
            mask2 >>= 1;
            maskC >>= 1;
          }
#ifdef AMD64
          /* more XORs can come from 64-bit registers */
          for(inBit=0; inBit<8; inBit++) {
            _MOV_OR_XOR_R64_INT(2, inBit, movC, maskC & 1);
            _C_XORPS_R64(0, inBit, mask1 & 1);
            _C_PXOR_R64(1, inBit, mask2 & 1);
            mask1 >>= 1;
            mask2 >>= 1;
            maskC >>= 1;
          }
#endif
          if(!movC) {
            _XORPS_R(0, 2);
            _PXOR_R(1, 2); /*penalty?*/
          }
          _ST_APS(DX, destOffs, 0);
          _ST_DQA(DX, destOffs2, 1);
        }
      } else {
        for(bit=0; bit<16; bit+=2) {
          int destOffs = (bit<<4)-128;
          int destOffs2 = destOffs+16;
          FAST_U32 mov1 = _MOV_OR_XOR_FP_INIT, mov2 = _MOV_OR_XOR_INT_INIT,
                   movC = _MOV_OR_XOR_INT_INIT;
          FAST_U16 mask1 = tmp_depmask[bit], mask2 = tmp_depmask[bit+1],
                   maskC = common_depmask[bit>>1];
          for(inBit=-8; inBit<(_XORS_FROM_MEMORY-8); inBit++) {
            _MOV_OR_XOR_INT_M(2, inBit, movC, maskC & 1);
            _MOV_OR_XOR_FP_M(0, inBit, mov1, mask1 & 1);
            _MOV_OR_XOR_INT_M(1, inBit, mov2, mask2 & 1);
            mask1 >>= 1;
            mask2 >>= 1;
            maskC >>= 1;
          }
          for(inBit=3; inBit<8; inBit++) {
            _MOV_OR_XOR_R_INT(2, inBit, movC, maskC & 1);
            _MOV_OR_XOR_R_FP(0, inBit, mov1, mask1 & 1);
            _MOV_OR_XOR_R_INT(1, inBit, mov2, mask2 & 1);
            mask1 >>= 1;
            mask2 >>= 1;
            maskC >>= 1;
          }
#ifdef AMD64
          for(inBit=0; inBit<8; inBit++) {
            _MOV_OR_XOR_R64_INT(2, inBit, movC, maskC & 1);
            _MOV_OR_XOR_R64_FP(0, inBit, mov1, mask1 & 1);
            _MOV_OR_XOR_R64_INT(1, inBit, mov2, mask2 & 1);
            mask1 >>= 1;
            mask2 >>= 1;
            maskC >>= 1;
          }
#endif
          if(!movC) {
            if(mov1) { /* no additional XORs were made? */
              _ST_DQA(DX, destOffs, 2);
            } else {
              _XORPS_R(0, 2);
            }
            if(mov2) {
              _ST_DQA(DX, destOffs2, 2);
            } else {
              _PXOR_R(1, 2); /*penalty?*/
            }
          }
          if(!mov1) {
            _ST_APS(DX, destOffs, 0);
          }
          if(!mov2) {
            _ST_DQA(DX, destOffs2, 1);
          }
        }
      }
      #undef _XORS_FROM_MEMORY
    }
    
    _jit_add_i(jit, AX, 256);
    _jit_add_i(jit, DX, 256);
    
    _jit_cmp_r(jit, DX, CX);
    _jit_jcc(jit, JL, pos_startloop);
    
#ifdef SAVE_XMM
    for(i=6; i<16; i++)
      _jit_movaps_load(jit, i, BP, -((int32_t)i-5)*16);
#endif
#undef SAVE_XMM
    if(setup_stack)
      _jit_pop(jit, BP);
    
    _jit_ret(jit);
    
    // exec
    (*(void(*)(void))jit->code)();
    
  }
  
}
#endif /* INTEL_SSE2 */




#ifdef INTEL_AVX512BW

#define MWORD_SIZE 64
#define _mword __m512i
#define _MM(f) _mm512_ ## f
#define _MMI(f) _mm512_ ## f ## _si512
#define _FN(f) f ## _avx512
/* still called "mm256" even in AVX512? */
#define _MM_END _mm256_zeroupper();

#include "gf_w16_split.c"

#undef MWORD_SIZE
#undef _mword
#undef _MM
#undef _MMI
#undef _FN
#undef _MM_END

#define FUNC_ASSIGN(v, f) { \
	if(has_avx512bw) { \
		v = f ## _avx512; \
	} else if(has_avx2) { \
		v = f ## _avx2; \
	} else { \
		v = f ## _sse; \
	} \
}
#endif /*INTEL_AVX512BW*/

#ifdef INTEL_AVX2
#define MWORD_SIZE 32
#define _mword __m256i
#define _MM(f) _mm256_ ## f
#define _MMI(f) _mm256_ ## f ## _si256
#define _FN(f) f ## _avx2
#define _MM_END _mm256_zeroupper();

#include "gf_w16_split.c"

#undef MWORD_SIZE
#undef _mword
#undef _MM
#undef _MMI
#undef _FN
#undef _MM_END

#ifndef FUNC_ASSIGN
#define FUNC_ASSIGN(v, f) { \
	if(has_avx2) { \
		v = f ## _avx2; \
	} else { \
		v = f ## _sse; \
	} \
}
#endif
#endif /*INTEL_AVX2*/

#ifdef INTEL_SSSE3
#define MWORD_SIZE 16
#define _mword __m128i
#define _MM(f) _mm_ ## f
#define _MMI(f) _mm_ ## f ## _si128
#define _FN(f) f ## _sse
#define _MM_END

#include "gf_w16_split.c"

#undef MWORD_SIZE
#undef _mword
#undef _MM
#undef _MMI
#undef _FN
#undef _MM_END

#ifndef FUNC_ASSIGN
#define FUNC_ASSIGN(v, f) { \
	v = f ## _sse; \
}
#endif
#endif /*INTEL_SSSE3*/


static void gf_w16_split_null(void* src, int bytes, void* dest) {
  if(src != dest) memcpy(dest, src, bytes);
}

