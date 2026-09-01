#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <Utilty/Common.h>
#include <Utilty/Machine.h>

// 32-bit

// Fast log2

#if COMPILER_MSVC

#include <intrin.h>

#pragma intrinsic(_BitScanReverse)

static inline uint8_t log2_u32(uint32_t X) {
	unsigned long Result;
	_BitScanReverse(&Result, X);
	return (uint8_t)Result;
}

#elif COMPILER_GCC

static inline uint8_t log2_u32(uint32_t X) {
	return 31 - (uint8_t)__builtin_clz(X);
}

#else

static const uint8_t aLogTable32[32] = {
	 0,  9,  1, 10, 13, 21,  2, 29,
	11, 14, 16, 18, 22, 25,  3, 30,
	 8, 12, 20, 28, 15, 17, 24,  7,
	19, 27, 23,  6, 26,  5,  4, 31
};

static inline uint8_t log2_u32(uint32_t X) {
	X |= X >> 1;
	X |= X >> 2;
	X |= X >> 4;
	X |= X >> 8;
	X |= X >> 16;
	return aLogTable32[(X * 0x07C4ACDD) >> 27];
}

#endif

// Bit scan forward

#if COMPILER_MSVC

#pragma intrinsic(_BitScanForward)

static inline uint8_t bsf_u32(uint32_t X) {
	unsigned long Result;
	_BitScanForward(&Result, X);
	return (uint8_t)Result;
}

#elif COMPILER_GCC

static inline uint8_t bsf_u32(uint32_t X) {
	return (uint8_t)__builtin_ctz(X);
}

#else

// Source: https://www.chessprogramming.org/Kim_Walisch#Bitscan

static inline uint8_t bsf_u32(uint32_t X) {
	return aLogTable32[((X ^ (X - 1)) * 0x07C4ACDD) >> 27];
}

#endif

// 64-bit

// Fast log2

#if COMPILER_MSVC && MACHINE_PTR64

#include <intrin.h>

#pragma intrinsic(_BitScanReverse64)

static inline uint8_t log2_u64(uint64_t X) {
	unsigned long Result;
	_BitScanReverse64(&Result, X);
	return (uint8_t)Result;
}

#elif COMPILER_GCC

static inline uint8_t log2_u64(uint64_t X) {
	return 63 - (uint8_t)__builtin_clzll(X);
}

#else

	#if MACHINE_PTR32
	
static inline uint8_t log2_u64(uint64_t X) {
	uint32_t High = (uint32_t)(X >> 32);
	if (High == 0)
		return log2_u32((uint32_t)X);
	else
		return log2_u32(High) + 32;
}
	
	#else

static const uint8_t aLogTable64[64] = {
	 0, 47,  1, 56, 48, 27,  2, 60,
	57, 49, 41, 37, 28, 16,  3, 61,
	54, 58, 35, 52, 50, 42, 21, 44,
	38, 32, 29, 23, 17, 11,  4, 62,
	46, 55, 26, 59, 40, 36, 15, 53,
	34, 51, 20, 43, 31, 22, 10, 45,
	25, 39, 14, 33, 19, 30,  9, 24,
	13, 18,  8, 12,  7,  6,  5, 63
};

static inline uint8_t log2_u64(uint64_t X) {
	X |= X >> 1;
	X |= X >> 2;
	X |= X >> 4;
	X |= X >> 8;
	X |= X >> 16;
	X |= X >> 32;
	return aLogTable64[(X * 0x03F79D71B4CB0A89) >> 58];
}

	#endif

#endif

// Bit scan forward

#if COMPILER_MSVC && MACHINE_PTR64

#pragma intrinsic(_BitScanForward64)

static inline uint8_t bsf_u64(uint64_t X) {
	unsigned long Result;
	_BitScanForward64(&Result, X);
	return (uint8_t)Result;
}

#elif COMPILER_GCC

static inline uint8_t bsf_u64(uint64_t X) {
	return (uint8_t)__builtin_ctzll(X);
}

#else

	#if MACHINE_PTR32
	
static inline uint8_t bsf_u64(uint64_t X) {
	uint32_t Low = (uint32_t)X;
	if (Low == 0)
		return bsf_u32((uint32_t)(X >> 32)) + 32;
	else
		return bsf_u32(Low);
}
	
	#else

// Source: https://www.chessprogramming.org/BitScan#With_separated_LS1B

static inline uint8_t bsf_u64(uint64_t X) {
   return aLogTable64[((X ^ (X - 1)) * 0x03F79D71B4CB0A89) >> 58];
}

	#endif

#endif

// Divide

uint64_t div_u64(uint64_t A, uint64_t B, uint64_t* pRem);

// 128-bit

#define NATIVE_INT128 (MACHINE_PTR64 && (COMPILER_GCC || COMPILER_CLANG))

#if NATIVE_INT128
typedef __int128 int128_t;
typedef unsigned __int128 uint128_t;
#endif

// For little endian
typedef union {
	struct {
		uint64_t Low;
		uint64_t High;
	};
#if NATIVE_INT128
	uint128_t Full;
#endif
} uint128_split;

typedef union {
	struct {
		uint64_t Low;
		int64_t High;
	};
#if NATIVE_INT128
	int128_t Full;
#endif
} int128_split;

#define cast_u64_u128(X) ((uint128_split){.Low = (uint64_t)(X)})
#define merge_u64_u128(X, Y) ((uint128_split){.Low = (uint64_t)(X), .High = (uint64_t)(Y)})

static inline uint128_split cast_i128_u128(int128_split X) {
	return (uint128_split){.Low = X.Low, .High = (uint64_t)X.High};
}
static inline int128_split cast_u128_i128(uint128_split X) {
	return (int128_split){.Low = X.Low, .High = (int64_t)X.High};
}

// Multiply two 64-bit integers to get 128-bit Result

uint128_split mul_full_u64(uint64_t A, uint64_t B);

// Compare

bool cmp_gt_u128(uint128_split A, uint128_split B);
bool cmp_ge_u128(uint128_split A, uint128_split B);
bool cmp_eq_u128(uint128_split A, uint128_split B);
bool cmp_ne_u128(uint128_split A, uint128_split B);
bool cmp_le_u128(uint128_split A, uint128_split B);

// Add & Subtract

uint128_split add_u128(uint128_split A, uint128_split B);
uint128_split sub_u128(uint128_split A, uint128_split B);

// Bitwise

uint128_split or_u128(uint128_split A, uint128_split B);
uint128_split and_u128(uint128_split A, uint128_split B);
uint128_split xor_u128(uint128_split A, uint128_split B);
uint128_split not_u128(uint128_split A);

// Logical Shift left

uint128_split lshl_u128(uint128_split A, uint8_t B);

// Logical Shift right

uint128_split lshr_u128(uint128_split A, uint8_t B);

// Arithmetic Shift right

int128_split ashr_i128(int128_split A, uint8_t B);

// Multiplication

uint128_split mul_u128(uint128_split A, uint128_split B);

// Fast log2

uint8_t log2_u128(uint128_split X);

// Divide

uint128_split div_u128(uint128_split A, uint128_split B, uint128_split* pRem);

// Pointer

// Fast log2

#if MACHINE_PTR64

static inline uint8_t log2_uptr(uintptr_t X) {
	return log2_u64(X);
}

#elif MACHINE_PTR32

static inline uint8_t log2_uptr(uintptr_t X) {
	return log2_u32(X);
}

#endif

// Bit scan forward

#if MACHINE_PTR64

static inline uint8_t bsf_uptr(uintptr_t X) {
	return bsf_u64(X);
}

#elif MACHINE_PTR32

static inline uint8_t bsf_uptr(uintptr_t X) {
	return bsf_u32(X);
}

#endif