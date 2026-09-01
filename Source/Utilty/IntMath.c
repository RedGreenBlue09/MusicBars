
#include <Utilty/IntMath.h>

// 64-bit

// Divide

#define NATIVE_DIV_LOW_64_32 (MACHINE_PTR64 || (MACHINE_IA32 && (COMPILER_MSVC || COMPILER_GCC)))

#if MACHINE_PTR32

	#if !NATIVE_DIV_LOW_64_32

uint64_t div_u64_u16(uint64_t A, uint16_t B, uint16_t* pRem) {
	uint64_t Res64 = 0;
	uint32_t Rem32 = 0; // Can be uint16_t but use uint32_t to avoid casts.
	for (uint8_t i = 0; i < 4; ++i) {
		uint32_t A32 = (Rem32 << 16) | (uint32_t)(A >> 48);
		Res64 = (Res64 << 16) | (A32 / B);
		Rem32 = A32 % B;
		A <<= 16;
	};
	*pRem = (uint16_t)Rem32;
	return Res64;
}

	#endif

	#if NATIVE_DIV_LOW_64_32

		#if COMPILER_MSVC && !COMPILER_CLANG

// Clang-Cl doesn't support this
#pragma intrinsic(_udiv64)

uint32_t div_low_u64_u32(uint64_t A, uint32_t B, uint32_t* pRem) {
	return _udiv64(A, B, pRem);
}
		#else

// GCC extended inline assembly
uint32_t div_low_u64_u32(uint64_t A, uint32_t B, uint32_t* pRem) {
	uint32_t A0 = (uint32_t)A;
	uint32_t A1 = (uint32_t)(A >> 32);
	uint32_t Res;
	__asm__("divl %4" : "=d" (*pRem), "=a" (Res) : "0" (A1), "1" (A0), "rm" (B));
	return Res;
}

		#endif

	#else

// Based on: https://github.com/ridiculousfish/libdivide/blob/5dc2d36676e7de41a44c6e86c921eb40fe7af3ee/libdivide.h#L556

uint32_t div_low_u64_u32(uint64_t A, uint32_t B, uint32_t *pRem) {
	uint8_t Shift = 31 - log2_u32(B);
	only_reachable(Shift < 64); // Help the compiler
	A <<= Shift;
	B <<= Shift;

	uint16_t B0 = (uint16_t)B;
	uint16_t B1 = (uint16_t)(B >> 16);
	uint16_t A00 = (uint16_t)A;
	uint16_t A01 = (uint16_t)((A >> 16) & UINT16_MAX);
	uint32_t A1 = (uint32_t)(A >> 32);
	uint32_t Q2;
	uint32_t R2;
	uint32_t C2;
	uint32_t C3;
	
	Q2 = A1 / B1;
	R2 = A1 % B1;
	C2 = Q2 * B0;
	C3 = (R2 << 16) | A01;
	if (C2 > C3)
		Q2 -= (C2 - C3 > B) ? 2 : 1;
	uint16_t Q1 = (uint16_t)Q2;

	uint32_t A10 = (A1 << 16) + A01 - Q1 * B;
	
	Q2 = A10 / B1;
	R2 = A10 % B1;
	C2 = Q2 * B0;
	C3 = (R2 << 16) | A00;
	if (C2 > C3)
		Q2 -= (C2 - C3 > B) ? 2 : 1;
	uint16_t Q0 = (uint16_t)Q2;

	*pRem = ((A10 << 16) + A00 - Q0 * B) >> Shift;
	return ((uint32_t)Q1 << 16) | Q0;
}

	#endif

// Based on: Hacker's Delight, Chapter 9-5

uint64_t div_u64(uint64_t A, uint64_t B, uint64_t* pRem) {
	if ((B >> 32) == 0) {
		uint32_t B0 = (uint32_t)B;
		uint32_t A0 = (uint32_t)A;
		uint32_t A1 = (uint32_t)(A >> 32);
		if (A1 == 0) {
			*pRem = A0 % B0;
			return A0 / B0;
		}
	#if !NATIVE_DIV_LOW_64_32
		if (B0 <= UINT16_MAX){
			uint16_t Rem16;
			uint64_t Res = div_u64_u16(A, (uint16_t)B0, &Rem16);
			*pRem = Rem16;
			return Res;
		}
	#endif
		uint64_t Res;
		uint32_t Rem0;
		if (A1 < B0) {
			Res = div_low_u64_u32(A, B0, &Rem0);
		} else {
			uint64_t Res2 = (uint64_t)(A1 / B0) << 32;
			uint64_t Rem2 = (uint64_t)(A1 % B0) << 32;
			Res = div_low_u64_u32(A0 | Rem2, B0, &Rem0) | Res2;
		}
		*pRem = Rem0;
		return Res;
	}
	
	if (A < B) {
		*pRem = A;
		return 0;
	}
	
	uint8_t Shift = log2_u32((uint32_t)(B >> 32));
	uint32_t Rem0;
	uint64_t Res = div_low_u64_u32(A >> 1, (uint32_t)(B << (31 - Shift) >> 32), &Rem0) >> Shift;
	*pRem = A - ((Res - 1) * B);
	if (*pRem < B) {
		return Res - 1;
	} else {
		*pRem -= B;
		return Res;
	}
}

#else

uint64_t div_u64(uint64_t A, uint64_t B, uint64_t* pRem) {
	*pRem = A % B;
	return A / B;
}

#endif

// 128-bit

// Multiply two 64-bit integers to get 128-bit Result

#if NATIVE_INT128

uint128_split mul_full_u64(uint64_t A, uint64_t B) {
	return (uint128_split) { .Full = (uint128_t)A * B };
}

#elif COMPILER_MSVC && MACHINE_AMD64

#pragma intrinsic(_umul128)

uint128_split mul_full_u64(uint64_t A, uint64_t B) {
	uint128_split Result;
	Result.Low = _umul128(A, B, &Result.High);
	return Result;
}

#else

uint128_split mul_full_u64(uint64_t A, uint64_t B) {
	uint32_t A0 = (uint32_t)A;
	uint32_t A1 = (uint32_t)(A >> 32);
	uint32_t B0 = (uint32_t)B;
	uint32_t B1 = (uint32_t)(B >> 32);

	uint64_t M00 = (uint64_t)A0 * B0;
	uint64_t M01 = (uint64_t)A0 * B1;
	uint64_t M10 = (uint64_t)A1 * B0;
	uint64_t M11 = (uint64_t)A1 * B1;

	uint8_t Carry = 0;
	uint64_t Mid = (M00 >> 32);
	Mid += M01;
	Carry += (Mid < M01);
	Mid += M10;
	Carry += (Mid < M10);

	return merge_u64_u128(
		(Mid << 32) | (uint32_t)M00,
		(((uint64_t)Carry << 32) | (Mid >> 32)) + M11
	);
}

#endif

// Compare

#if NATIVE_INT128

bool cmp_gt_u128(uint128_split A, uint128_split B) {
	return A.Full > B.Full;
}

bool cmp_ge_u128(uint128_split A, uint128_split B) {
	return A.Full >= B.Full;
}

bool cmp_eq_u128(uint128_split A, uint128_split B) {
	return A.Full == B.Full;
}

bool cmp_ne_u128(uint128_split A, uint128_split B) {
	return A.Full != B.Full;
}

bool cmp_le_u128(uint128_split A, uint128_split B) {
	return A.Full <= B.Full;
}

bool cmp_ls_u128(uint128_split A, uint128_split B) {
	return A.Full < B.Full;
}

#else

// Logic:
// If operations don't underflow (wrap around):
// cmp(A, B) = any qsort-style compare function
// (A > B) = cmp(A.High, B.High) - (A.Low <= B.Low) >= 0
// (A >= B) = cmp(A.High, B.High) - (A.Low < B.Low) >= 0
// cmp(A, B) = (A > B) - (A < B) is the simplest that doesn't underflow.
//
// Alternative method:
// To avoid underflow, we need to check the carry bit:
// carry(A, B) = (A < B)
// (A > B) = !carry(A.High, B.High) & !carry(cmp(A.High, B.High), A.Low <= B.Low)
// (A >= B) = !carry(A.High, B.High) & !carry(cmp(A.High, B.High), A.Low < B.Low)
// cmp(A, B) = (A - B) is the best choice since carry(A, B) is usually a side effect of it.

bool cmp_gt_u128(uint128_split A, uint128_split B) {
	return (int)(A.High > B.High) - (A.High < B.High) - (A.Low <= B.Low) >= 0;
}

bool cmp_ge_u128(uint128_split A, uint128_split B) {
	return (int)(A.High > B.High) - (A.High < B.High) - (A.Low < B.Low) >= 0;
}

bool cmp_eq_u128(uint128_split A, uint128_split B) {
	return (A.High == B.High) & (A.Low == B.Low);
}

bool cmp_ne_u128(uint128_split A, uint128_split B) {
	return (A.High != B.High) | (A.Low != B.Low);
}

bool cmp_le_u128(uint128_split A, uint128_split B) {
	return (int)(A.High > B.High) - (A.High < B.High) - (A.Low <= B.Low) < 0;
}

bool cmp_ls_u128(uint128_split A, uint128_split B) {
	return (int)(A.High > B.High) - (A.High < B.High) - (A.Low < B.Low) < 0;
}

#endif

// Compare

#if NATIVE_INT128

bool cmp_gt_i128(int128_split A, int128_split B) {
	return A.Full > B.Full;
}

bool cmp_ge_i128(int128_split A, int128_split B) {
	return A.Full >= B.Full;
}

bool cmp_eq_i128(int128_split A, int128_split B) {
	return A.Full == B.Full;
}

bool cmp_ne_i128(int128_split A, int128_split B) {
	return A.Full != B.Full;
}

bool cmp_le_i128(int128_split A, int128_split B) {
	return A.Full <= B.Full;
}

bool cmp_ls_i128(int128_split A, int128_split B) {
	return A.Full < B.Full;
}

#else

// Logic:
// Same as unsigned comparision but the low values doesn't make sense
// when A and B has different signs. Fortunately, cmp(A.High, B.High) guards these cases,
// so the result of the low comparision only matters otherwise.
//
// Alternative method:
// We increase both values by (1 << 127) (flip the last bit)
// to increase all negative values to non-negative. Then, do an unsigned comparision.
// The order doesn't change so the comparision is still correct.

bool cmp_gt_i128(int128_split A, int128_split B) {
	return (int)(A.High > B.High) - (A.High < B.High) - (A.Low <= B.Low) >= 0;
}

bool cmp_ge_i128(int128_split A, int128_split B) {
	return (int)(A.High > B.High) - (A.High < B.High) - (A.Low < B.Low) >= 0;
}

bool cmp_eq_i128(int128_split A, int128_split B) {
	return (A.High == B.High) & (A.Low == B.Low);
}

bool cmp_ne_i128(int128_split A, int128_split B) {
	return (A.High != B.High) | (A.Low != B.Low);
}

bool cmp_le_i128(int128_split A, int128_split B) {
	return (int)(A.High > B.High) - (A.High < B.High) - (A.Low <= B.Low) < 0;
}

bool cmp_ls_i128(int128_split A, int128_split B) {
	return (int)(A.High > B.High) - (A.High < B.High) - (A.Low < B.Low) < 0;
}

#endif

// Add & Subtract

#if NATIVE_INT128

uint128_split add_u128(uint128_split A, uint128_split B) {
	return (uint128_split){.Full = A.Full + B.Full};
}

uint128_split sub_u128(uint128_split A, uint128_split B) {
	return (uint128_split){.Full = A.Full - B.Full};
}

#else

uint128_split add_u128(uint128_split A, uint128_split B) {
	uint128_split Result;
	Result.Low  = A.Low  + B.Low;
	Result.High = A.High + B.High + (Result.Low < A.Low);
	return Result;
}

uint128_split sub_u128(uint128_split A, uint128_split B) {
	uint128_split Result;
	Result.Low  = A.Low  - B.Low;
	Result.High = A.High - B.High - (Result.Low > A.Low);
	return Result;
}

#endif

// Bitwise

uint128_split or_u128(uint128_split A, uint128_split B) {
	uint128_split Result;
	Result.Low  = A.Low  | B.Low;
	Result.High = A.High | B.High;
	return Result;
}

uint128_split and_u128(uint128_split A, uint128_split B) {
	uint128_split Result;
	Result.Low  = A.Low  & B.Low;
	Result.High = A.High & B.High;
	return Result;
}

uint128_split xor_u128(uint128_split A, uint128_split B) {
	uint128_split Result;
	Result.Low  = A.Low  ^ B.Low;
	Result.High = A.High ^ B.High;
	return Result;
}

uint128_split not_u128(uint128_split A) {
	return (uint128_split){~A.Low, ~A.High};
}

// Logical Shift left

#if NATIVE_INT128

uint128_split lshl_u128(uint128_split A, uint8_t B) {
	return (uint128_split){.Full = A.Full << B};
}

#else

uint128_split lshl_u128(uint128_split A, uint8_t B) {
	uint128_split Result;
	if (B == 0)
		Result = A;
	else if (B < 64) {
		Result.High  = A.High << B;
		Result.High |= A.Low  >> (64 - B);
		Result.Low   = A.Low  << B;
	} else {
		Result.High  = A.Low  << (B - 64);
		Result.Low   = 0;
	}
	return Result;
}

#endif

// Logical Shift right

#if NATIVE_INT128

uint128_split lshr_u128(uint128_split A, uint8_t B) {
	return (uint128_split){.Full = A.Full >> B};
}

#else

uint128_split lshr_u128(uint128_split A, uint8_t B) {
	uint128_split Result;
	if (B == 0)
		Result = A;
	else if (B < 64) {
		Result.Low  = A.Low  >> B;
		Result.Low |= A.High << (64 - B);
		Result.High = A.High >> B;
	} else {
		Result.Low  = A.High >> (B - 64);
		Result.High = 0;
	}
	return Result;
}

#endif

// Arithmetic Shift right

#if NATIVE_INT128

int128_split ashr_i128(int128_split A, uint8_t B) {
	return (int128_split){.Full = A.Full >> B};
}

#else

int128_split ashr_i128(int128_split A, uint8_t B) {
	int128_split Result;
	if (B == 0)
		Result = A;
	else if (B < 64) {
		Result.Low  = A.Low  >> B;
		Result.Low |= A.High << (64 - B);
		Result.High = A.High >> B;
	} else {
		Result.Low  = A.High >> (B - 64);
		Result.High = A.High >> 63;
	}
	return Result;
}

#endif

// Fast log2

uint8_t log2_u128(uint128_split X) {
	if (X.High == 0)
		return log2_u64(X.Low);
	else
		return log2_u64(X.High) + 64;
}

// Multiply
// Low part of signed multiplication is the same as unsigned multiplication.

#if NATIVE_INT128

uint128_split mul_u128(uint128_split A, uint128_split B) {
	return (uint128_split){.Full = A.Full * B.Full};
}

#else

uint128_split mul_u128(uint128_split A, uint128_split B) {
	uint128_split M00 = mul_full_u64(A.Low, B.Low);
	uint64_t M01L = A.Low * B.High;
	uint64_t M10L = A.High * B.Low;
	return add_u128(M00, (uint128_split){.High = M01L + M10L});
}

#endif

// Divide
	
#define NATIVE_DIV_LOW_128_64 (MACHINE_AMD64 && (COMPILER_MSVC || COMPILER_GCC))

#if !NATIVE_DIV_LOW_64_32

uint128_split div_u128_u16(uint128_split A, uint16_t B, uint16_t* pRem) {
	uint128_split Res = {0};
	uint32_t Rem32 = 0; // Can be uint16_t but use uint32_t to avoid casts.
	for (uint8_t i = 0; i < 8; ++i) {
		uint32_t A32 = (Rem32 << 16) | (uint32_t)(A.High >> 48);
		Res = lshl_u128(Res, 16);
		Res.Low |= A32 / B;
		Rem32 = A32 % B;
		A = lshl_u128(A, 16);
	}
	*pRem = (uint16_t)Rem32;
	return Res;
}

#endif

#if !NATIVE_DIV_LOW_128_64

uint128_split div_u128_u32(uint128_split A, uint32_t B, uint32_t* pRem) {
	uint128_split Res = {0};
	uint64_t Rem64 = 0; // Can be uint32_t but use uint64_t to avoid casts.
	for (uint8_t i = 0; i < 4; ++i) {
		uint64_t A64 = (Rem64 << 32) | (uint64_t)(A.High >> 32);
		Res = lshl_u128(Res, 32);
		Res.Low |= div_u64(A64, B, &Rem64);
		A = lshl_u128(A, 32);
	}
	*pRem = (uint32_t)Rem64;
	return Res;
}

#endif

#if NATIVE_DIV_LOW_128_64

	#if COMPILER_MSVC && !COMPILER_CLANG

// Clang-Cl doesn't support this
#pragma intrinsic(_udiv128)

uint64_t div_low_u128_u64(uint128_split A, uint64_t B, uint64_t* pRem) {
	return _udiv128(A.High, A.Low, B, pRem);
}

	#else

// GCC extended assembly
uint64_t div_low_u128_u64(uint128_split A, uint64_t B, uint64_t* pRem) {
	uint64_t Res;
	__asm__("divq %4" : "=d" (*pRem), "=a" (Res) : "0" (A.High), "1" (A.Low), "rm" (B));
	return Res;
}

	#endif
	
#else

// Based on: https://github.com/ridiculousfish/libdivide/blob/5dc2d36676e7de41a44c6e86c921eb40fe7af3ee/libdivide.h#L556

uint64_t div_low_u128_u64(uint128_split A, uint64_t B, uint64_t *pRem) {
	uint8_t Shift = 63 - log2_u64(B);
	only_reachable(Shift < 64); // Help the compiler
	A = lshl_u128(A, Shift);
	B <<= Shift;

	uint32_t B0 = (uint32_t)B;
	uint32_t B1 = (uint32_t)(B >> 32);
	uint32_t A00 = (uint32_t)A.Low;
	uint32_t A01 = (uint32_t)(A.Low >> 32);
	uint64_t Q2;
	uint64_t R2;
	uint64_t C2;
	uint64_t C3;
	
	Q2 = div_u64(A.High, B1, &R2);
	C2 = Q2 * B0;
	C3 = (R2 << 32) | A01;
	if (C2 > C3)
		Q2 -= (C2 - C3 > B) ? 2 : 1;
	uint32_t Q1 = (uint32_t)Q2;

	uint64_t A10 = (A.High << 32) + A01 - Q1 * B;
	
	Q2 = div_u64(A10, B1, &R2);
	C2 = Q2 * B0;
	C3 = (R2 << 32) | A00;
	if (C2 > C3)
		Q2 -= (C2 - C3 > B) ? 2 : 1;
	uint32_t Q0 = (uint32_t)Q2;

	*pRem = ((A10 << 32) + A00 - Q0 * B) >> Shift;
	return ((uint64_t)Q1 << 32) | Q0;
}

#endif

// Based on: Hacker's Delight, Chapter 9-5

uint128_split div_u128(uint128_split A, uint128_split B, uint128_split* pRem) {
	if (B.High == 0) {
		pRem->High = 0;
		if (A.High == 0)
			return cast_u64_u128(div_u64(A.Low, B.Low, &pRem->Low));
#if !NATIVE_DIV_LOW_64_32
		if (B.Low <= UINT16_MAX) {
			uint16_t Rem16;
			uint128_split Res = div_u128_u16(A, (uint16_t)B.Low, &Rem16);
			pRem->Low = Rem16;
			return Res;
		}
#endif
#if !NATIVE_DIV_LOW_128_64
		if (B.Low >> 32 == 0) { // B.Low <= UINT32_MAX
			uint32_t Rem32;
			uint128_split Res = div_u128_u32(A, (uint32_t)B.Low, &Rem32);
			pRem->Low = Rem32;
			return Res;
		}
#endif
		if (A.High < B.Low)
			return cast_u64_u128(div_low_u128_u64(A, B.Low, &pRem->Low));
		
		uint64_t Rem1;
		uint64_t Res1 = div_u64(A.High, B.Low, &Rem1);
		uint128_split Rem2 = merge_u64_u128(A.Low, Rem1);
		return merge_u64_u128(div_low_u128_u64(Rem2, B.Low, &pRem->Low), Res1);
	}
	
	if (cmp_ls_u128(A, B)) {
		*pRem = A;
		return cast_u64_u128(0);
	}
		
	uint8_t Shift = log2_u64(B.High);
	
	// Help the compiler
	only_reachable(Shift < 64);
	
	uint64_t Rem64;
	uint64_t Res64 = div_low_u128_u64(lshr_u128(A, 1), lshl_u128(B, 63 - Shift).High, &Rem64) >> Shift;
	
	*pRem = sub_u128(A, mul_u128(cast_u64_u128(Res64 - 1), B));
	if (cmp_ls_u128(*pRem, B)) {
		return cast_u64_u128(Res64 - 1);
	} else {
		*pRem = sub_u128(*pRem, B);
		return cast_u64_u128(Res64);
	}
}

int128_split div_i128(int128_split A, int128_split B, uint128_split* pRem) {
	uint128_split MaskA = cast_i128_u128(ashr_i128(A, 127));
	uint128_split MaskB = cast_i128_u128(ashr_i128(B, 127));
	uint128_split UA = xor_u128(add_u128(cast_i128_u128(A), MaskA), MaskA);
	uint128_split UB = xor_u128(add_u128(cast_i128_u128(B), MaskB), MaskB);
	uint128_split URem;
	uint128_split URes = div_u128(UA, UB, &URem);
	*pRem = xor_u128(add_u128(URem, MaskB), MaskB);
	uint128_split MaskRes = xor_u128(MaskA, MaskB);
	return cast_u128_i128(xor_u128(add_u128(URes, MaskRes), MaskRes));
}
