
int has_ssse3 = 0;
int has_pclmul = 0;

void detect_cpu(void) {
#ifdef _MSC_VER
	int cpuInfo[4];
	__cpuid(cpuInfo, 1);
	#ifdef INTEL_SSSE3
	has_ssse3 = (cpuInfo[2] & 0x200);
	#endif
	#ifdef INTEL_SSE4_PCLMUL
	has_pclmul = (cpuInfo[2] & 0x2);
	#endif
#elif defined(_IS_X86)
	/* conveniently stolen from zlib-ng */
	uint32_t flags;

	__asm__ __volatile__ (
		"cpuid"
	: "=c" (flags)
	: "a" (1)
	: "%edx", "%ebx"
	);
	#ifdef INTEL_SSSE3
	has_ssse3 = (flags & 0x200);
	#endif
	#ifdef INTEL_SSE4_PCLMUL
	has_pclmul = (flags & 0x2);
	#endif
#endif
}


#define MWORD_SIZE 16
#define _mword __m128i
#define _MM(f) _mm_ ## f
#define _MMI(f) _mm_ ## f ## i128
#define _FN(f) f ## _sse
#include "gf_w16_split.c"

/*
#ifdef INTEL_AVX2
#define MWORD_SIZE 32
#define _mword __m256i
#define _MM(f) _mm256_ ## f
#define _MMI(f) _mm256_ ## f ## i256
#define _FN(f) f ## _avx2
#-include "gf_w16_split.c"
#endif

#ifdef INTEL_AVX512
#define MWORD_SIZE 64
#define _mword __m512i
#define _MM(f) _mm512_ ## f
#define _MMI(f) _mm512_ ## f ## i512
#define _FN(f) f ## _avx512
#-include "gf_w16_split.c"
#endif
*/


static void gf_w16_split_null(void* src, int bytes, void* dest) {}

