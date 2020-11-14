#include "platform.h"

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#ifndef UNUSED
# define UNUSED(...) (void)(__VA_ARGS__)
#endif


static HEDLEY_ALWAYS_INLINE void md5_process_block_x2_scalar(uint32_t* state, const char* const* HEDLEY_RESTRICT data, size_t offset) {
	UNUSED(offset);
	uint32_t A1 = state[0];
	uint32_t B1 = state[1];
	uint32_t C1 = state[2];
	uint32_t D1 = state[3];
	uint32_t A2 = state[4];
	uint32_t B2 = state[5];
	uint32_t C2 = state[6];
	uint32_t D2 = state[7];
	
#define ROUND_F(A1, B1, C1, D1, A2, B2, C2, D2, NEXT_IN1, NEXT_IN2, K, TMP1, TMP2, R) \
	"leal " STR(K) "(%k[" STR(A1) "], %%" TMP1 "), %k[" STR(A1) "]\n" \
	"movl %k[" STR(D1) "], %%" TMP1 "\n" \
	"leal " STR(K) "(%k[" STR(A2) "], %%" TMP2 "), %k[" STR(A2) "]\n" \
	"movl %k[" STR(D2) "], %%" TMP2 "\n" \
	"xorl %k[" STR(C1) "], %%" TMP1 "\n" \
	"xorl %k[" STR(C2) "], %%" TMP2 "\n" \
	"andl %k[" STR(B1) "], %%" TMP1 "\n" \
	"andl %k[" STR(B2) "], %%" TMP2 "\n" \
	"xorl %k[" STR(D1) "], %%" TMP1 "\n" \
	"xorl %k[" STR(D2) "], %%" TMP2 "\n" \
	"addl %%" TMP1 ", %k[" STR(A1) "]\n" \
	"addl %%" TMP2 ", %k[" STR(A2) "]\n" \
	"movl " NEXT_IN1 ", %%" TMP1 "\n" \
	"movl " NEXT_IN2 ", %%" TMP2 "\n" \
	"roll $" STR(R) ", %k[" STR(A1) "]\n" \
	"roll $" STR(R) ", %k[" STR(A2) "]\n" \
	"addl %k[" STR(B1) "], %k[" STR(A1) "]\n" \
	"addl %k[" STR(B2) "], %k[" STR(A2) "]\n"
#define ROUND_H(A1, B1, C1, D1, A2, B2, C2, D2, NEXT_IN1, NEXT_IN2, K, TMP1, TMP2, R) \
	"leal " STR(K) "(%k[" STR(A1) "], %%" TMP1 "), %k[" STR(A1) "]\n" \
	"movl %k[" STR(D1) "], %%" TMP1 "\n" \
	"leal " STR(K) "(%k[" STR(A2) "], %%" TMP2 "), %k[" STR(A2) "]\n" \
	"movl %k[" STR(D2) "], %%" TMP2 "\n" \
	"xorl %k[" STR(C1) "], %%" TMP1 "\n" \
	"xorl %k[" STR(C2) "], %%" TMP2 "\n" \
	"xorl %k[" STR(B1) "], %%" TMP1 "\n" \
	"xorl %k[" STR(B2) "], %%" TMP2 "\n" \
	"addl %%" TMP1 ", %k[" STR(A1) "]\n" \
	"addl %%" TMP2 ", %k[" STR(A2) "]\n" \
	"movl " NEXT_IN1 ", %%" TMP1 "\n" \
	"movl " NEXT_IN2 ", %%" TMP2 "\n" \
	"roll $" STR(R) ", %k[" STR(A1) "]\n" \
	"roll $" STR(R) ", %k[" STR(A2) "]\n" \
	"addl %k[" STR(B1) "], %k[" STR(A1) "]\n" \
	"addl %k[" STR(B2) "], %k[" STR(A2) "]\n"
#define ROUND_I(A1, B1, C1, D1, A2, B2, C2, D2, NEXT_IN1, NEXT_IN2, K, TMP1, TMP2, R) \
	"leal " STR(K) "(%k[" STR(A1) "], %%" TMP1 "), %k[" STR(A1) "]\n" \
	"movl %k[" STR(D1) "], %%" TMP1 "\n" \
	"leal " STR(K) "(%k[" STR(A2) "], %%" TMP2 "), %k[" STR(A2) "]\n" \
	"movl %k[" STR(D2) "], %%" TMP2 "\n" \
	"notl %%" TMP1 "\n" \
	"notl %%" TMP2 "\n" \
	"orl %k[" STR(B1) "], %%" TMP1 "\n" \
	"orl %k[" STR(B2) "], %%" TMP2 "\n" \
	"xorl %k[" STR(C1) "], %%" TMP1 "\n" \
	"xorl %k[" STR(C2) "], %%" TMP2 "\n" \
	"addl %%" TMP1 ", %k[" STR(A1) "]\n" \
	"addl %%" TMP2 ", %k[" STR(A2) "]\n" \
	"movl " NEXT_IN1 ", %%" TMP1 "\n" \
	"movl " NEXT_IN2 ", %%" TMP2 "\n" \
	"roll $" STR(R) ", %k[" STR(A1) "]\n" \
	"roll $" STR(R) ", %k[" STR(A2) "]\n" \
	"addl %k[" STR(B1) "], %k[" STR(A1) "]\n" \
	"addl %k[" STR(B2) "], %k[" STR(A2) "]\n"
#define ROUND_I_LAST(A1, B1, C1, D1, A2, B2, C2, D2, K, TMP1, TMP2, R) \
	"leal " STR(K) "(%k[" STR(A1) "], %%" TMP1 "), %k[" STR(A1) "]\n" \
	"movl %k[" STR(D1) "], %%" TMP1 "\n" \
	"leal " STR(K) "(%k[" STR(A2) "], %%" TMP2 "), %k[" STR(A2) "]\n" \
	"movl %k[" STR(D2) "], %%" TMP2 "\n" \
	"notl %%" TMP1 "\n" \
	"notl %%" TMP2 "\n" \
	"orl %k[" STR(B1) "], %%" TMP1 "\n" \
	"orl %k[" STR(B2) "], %%" TMP2 "\n" \
	"xorl %k[" STR(C1) "], %%" TMP1 "\n" \
	"xorl %k[" STR(C2) "], %%" TMP2 "\n" \
	"addl %%" TMP1 ", %k[" STR(A1) "]\n" \
	"addl %%" TMP2 ", %k[" STR(A2) "]\n" \
	"roll $" STR(R) ", %k[" STR(A1) "]\n" \
	"roll $" STR(R) ", %k[" STR(A2) "]\n" \
	"addl %k[" STR(B1) "], %k[" STR(A1) "]\n" \
	"addl %k[" STR(B2) "], %k[" STR(A2) "]\n"

#define ROUND_G(A1, B1, C1, D1, A2, B2, C2, D2, NEXT_IN1, NEXT_IN2, K, TMP1, TMP2, R) \
	"leal " STR(K) "(%k[" STR(A1) "], %%" TMP1 "), %k[" STR(A1) "]\n" \
	"movl %k[" STR(D1) "], %%" TMP1 "\n" \
	"leal " STR(K) "(%k[" STR(A2) "], %%" TMP2 "), %k[" STR(A2) "]\n" \
	"movl %k[" STR(D2) "], %%" TMP2 "\n" \
	"notl %%" TMP1 "\n" \
	"notl %%" TMP2 "\n" \
	"andl %k[" STR(C1) "], %%" TMP1 "\n" \
	"andl %k[" STR(C2) "], %%" TMP2 "\n" \
	"addl %%" TMP1 ", %k[" STR(A1) "]\n" \
	"addl %%" TMP2 ", %k[" STR(A2) "]\n" \
	"movl %k[" STR(D1) "], %%" TMP1 "\n" \
	"movl %k[" STR(D2) "], %%" TMP2 "\n" \
	"andl %k[" STR(B1) "], %%" TMP1 "\n" \
	"andl %k[" STR(B2) "], %%" TMP2 "\n" \
	"addl %%" TMP1 ", %k[" STR(A1) "]\n" \
	"addl %%" TMP2 ", %k[" STR(A2) "]\n" \
	"movl " NEXT_IN1 ", %%" TMP1 "\n" \
	"movl " NEXT_IN2 ", %%" TMP2 "\n" \
	"roll $" STR(R) ", %k[" STR(A1) "]\n" \
	"roll $" STR(R) ", %k[" STR(A2) "]\n" \
	"addl %k[" STR(B1) "], %k[" STR(A1) "]\n" \
	"addl %k[" STR(B2) "], %k[" STR(A2) "]\n"

#define RF4(i0, i1, i2, i3, k0, k1, k2, k3) \
	ROUND_F(A1, B1, C1, D1, A2, B2, C2, D2, STR(i0) "(%[i0])", STR(i0) "(%[i1])", k0, "r14d", "r15d", 7) \
	ROUND_F(D1, A1, B1, C1, D2, A2, B2, C2, STR(i1) "(%[i0])", STR(i1) "(%[i1])", k1, "r14d", "r15d", 12) \
	ROUND_F(C1, D1, A1, B1, C2, D2, A2, B2, STR(i2) "(%[i0])", STR(i2) "(%[i1])", k2, "r14d", "r15d", 17) \
	ROUND_F(B1, C1, D1, A1, B2, C2, D2, A2, STR(i3) "(%[i0])", STR(i3) "(%[i1])", k3, "r14d", "r15d", 22)
	
#define RG4(i0, i1, i2, i3, k0, k1, k2, k3) \
	ROUND_G(A1, B1, C1, D1, A2, B2, C2, D2, STR(i0) "(%[i0])", STR(i0) "(%[i1])", k0, "r14d", "r15d", 5) \
	ROUND_G(D1, A1, B1, C1, D2, A2, B2, C2, STR(i1) "(%[i0])", STR(i1) "(%[i1])", k1, "r14d", "r15d", 9) \
	ROUND_G(C1, D1, A1, B1, C2, D2, A2, B2, STR(i2) "(%[i0])", STR(i2) "(%[i1])", k2, "r14d", "r15d", 14) \
	ROUND_G(B1, C1, D1, A1, B2, C2, D2, A2, STR(i3) "(%[i0])", STR(i3) "(%[i1])", k3, "r14d", "r15d", 20)
	
#define RH4(i0, i1, i2, i3, k0, k1, k2, k3) \
	ROUND_H(A1, B1, C1, D1, A2, B2, C2, D2, STR(i0) "(%[i0])", STR(i0) "(%[i1])", k0, "r14d", "r15d", 4) \
	ROUND_H(D1, A1, B1, C1, D2, A2, B2, C2, STR(i1) "(%[i0])", STR(i1) "(%[i1])", k1, "r14d", "r15d", 11) \
	ROUND_H(C1, D1, A1, B1, C2, D2, A2, B2, STR(i2) "(%[i0])", STR(i2) "(%[i1])", k2, "r14d", "r15d", 16) \
	ROUND_H(B1, C1, D1, A1, B2, C2, D2, A2, STR(i3) "(%[i0])", STR(i3) "(%[i1])", k3, "r14d", "r15d", 23)
	
#define RI4(i0, i1, i2, i3, k0, k1, k2, k3) \
	ROUND_I(A1, B1, C1, D1, A2, B2, C2, D2, STR(i0) "(%[i0])", STR(i0) "(%[i1])", k0, "r14d", "r15d", 6) \
	ROUND_I(D1, A1, B1, C1, D2, A2, B2, C2, STR(i1) "(%[i0])", STR(i1) "(%[i1])", k1, "r14d", "r15d", 10) \
	ROUND_I(C1, D1, A1, B1, C2, D2, A2, B2, STR(i2) "(%[i0])", STR(i2) "(%[i1])", k2, "r14d", "r15d", 15) \
	ROUND_I(B1, C1, D1, A1, B2, C2, D2, A2, STR(i3) "(%[i0])", STR(i3) "(%[i1])", k3, "r14d", "r15d", 21)
	
#ifndef PLATFORM_AMD64
	ALIGN_TO(16, uint32_t scratch[32]);
#endif
	asm(
		"movl (%[i0]), %%r14d\n"
		"movl (%[i1]), %%r15d\n"
		RF4( 4,  8, 12, 16,  -0x28955b88, -0x173848aa, 0x242070db, -0x3e423112)
		RF4(20, 24, 28, 32,  -0x0a83f051, 0x4787c62a, -0x57cfb9ed, -0x02b96aff)
		RF4(36, 40, 44, 48,  0x698098d8, -0x74bb0851, -0x0000a44f, -0x76a32842)
		RF4(52, 56, 60,  4,  0x6b901122, -0x02678e6d, -0x5986bc72, 0x49b40821)
		
		RG4(24, 44,  0, 20,  -0x09e1da9e, -0x3fbf4cc0, 0x265e5a51, -0x16493856)
		RG4(40, 60, 16, 36,  -0x29d0efa3, 0x02441453, -0x275e197f, -0x182c0438)
		RG4(56, 12, 32, 52,  0x21e1cde6, -0x3cc8f82a, -0x0b2af279, 0x455a14ed)
		RG4( 8, 28, 48, 20,  -0x561c16fb, -0x03105c08, 0x676f02d9, -0x72d5b376)
		
		RH4(32, 44, 56,  4,  -0x0005c6be, -0x788e097f, 0x6d9d6122, -0x021ac7f4)
		RH4(16, 28, 40, 52,  -0x5b4115bc, 0x4bdecfa9, -0x0944b4a0, -0x41404390)
		RH4( 0, 12, 24, 36,  0x289b7ec6, -0x155ed806, -0x2b10cf7b, 0x04881d05)
		RH4(48, 60,  8,  0,  -0x262b2fc7, -0x1924661b, 0x1fa27cf8, -0x3b53a99b)
		
		RI4(28, 56, 20, 48,  -0x0bd6ddbc, 0x432aff97, -0x546bdc59, -0x036c5fc7)
		RI4(12, 40,  4, 32,  0x655b59c3, -0x70f3336e, -0x00100b83, -0x7a7ba22f)
		RI4(60, 24, 52, 16,  0x6fa87e4f, -0x01d31920, -0x5cfebcec, 0x4e0811a1)
		
		ROUND_I(A1, B1, C1, D1, A2, B2, C2, D2, "44(%[i0])", "44(%[i1])", -0x08ac817e, "r14d", "r15d", 6)
		ROUND_I(D1, A1, B1, C1, D2, A2, B2, C2,  "8(%[i0])",  "8(%[i1])", -0x42c50dcb, "r14d", "r15d", 10)
		ROUND_I(C1, D1, A1, B1, C2, D2, A2, B2, "36(%[i0])", "36(%[i1])", 0x2ad7d2bb, "r14d", "r15d", 15)
		ROUND_I_LAST(B1, C1, D1, A1, B2, C2, D2, A2, -0x14792c6f, "r14d", "r15d", 21)
	: [A1]"+r"(A1), [B1]"+r"(B1), [C1]"+r"(C1), [D1]"+r"(D1),
	  [A2]"+r"(A2), [B2]"+r"(B2), [C2]"+r"(C2), [D2]"+r"(D2)
	: [i0]"r"(data[0]), [i1]"r"(data[1])
	: "%r14", "%r15"
	);
	state[0] += A1;
	state[1] += B1;
	state[2] += C1;
	state[3] += D1;
	state[4] += A2;
	state[5] += B2;
	state[6] += C2;
	state[7] += D2;
#undef ROUND_F
#undef ROUND_G
#undef ROUND_H
#undef ROUND_I
#undef RF4
#undef RG4
#undef RH4
#undef RI4
}
