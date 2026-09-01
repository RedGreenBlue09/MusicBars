#pragma once

#include <stdint.h>

// Compiler detection

#if _MSC_VER
#define COMPILER_MSVC 1
#endif

#if __GNUC__
#define COMPILER_GCC 1
#endif

#if __clang__
#define COMPILER_CLANG 1
#endif

// Machine pointer size detection

#if (UINTPTR_MAX == UINT32_MAX)
#define MACHINE_PTR32 1
#elif (UINTPTR_MAX == UINT64_MAX)
#define MACHINE_PTR64 1
#endif

// Machine architecture detection

#ifdef COMPILER_MSVC

	#ifdef _M_IX86

#define MACHINE_IA32 1

	#elif _M_X64

#define MACHINE_AMD64 1

	#elif _M_ARM

#define MACHINE_ARM32 1

	#elif _M_ARM64

#define MACHINE_ARM64 1

		#if __ARM_ARCH >= 801

#define MACHINE_ARM64_ATOMICS 1

		#endif

	#endif

#elif COMPILER_GCC

	#ifdef __i386__

#define MACHINE_IA32 1

	#elif __x86_64__

#define MACHINE_AMD64 1

	#elif __arm__

#define MACHINE_ARM32 1

	#elif __aarch64__

#define MACHINE_ARM64 1

		#if defined(__ARM_FEATURE_ATOMICS)

#define MACHINE_ARM64_ATOMICS 1

		#endif

	#endif

#endif

#define MACHINE_LLSC_ATOMICS (MACHINE_ARM32 || (MACHINE_ARM64 && !MACHINE_ARM64_ATOMICS))

#if COMPILER_GCC
	#if defined(__FMA__)
		#define COMPILER_HAS_FMA3 1
	#endif
	#if defined(__AVX2__)
		#define COMPILER_HAS_AVX2 1
	#endif
	#if defined(__AVX__)
		#define COMPILER_HAS_AVX 1
	#endif
	#if defined(__SSE4_2__)
		#define COMPILER_HAS_SSE4_2 1
	#endif
	#if defined(__SSE4_1__)
		#define COMPILER_HAS_SSE4_1 1
	#endif
	#if defined(__SSSE3__)
		#define COMPILER_HAS_SSSE3 1
	#endif
	#if defined(__SSE3__)
		#define COMPILER_HAS_SSE3 1
	#endif
	#if defined(__SSE2__) || defined(__x86_64__)
		#define COMPILER_HAS_SSE2 1
	#endif
	#if defined(__SSE__) || defined(__x86_64__)
		#define COMPILER_HAS_SSE 1
	#endif

#elif COMPILER_MSVC

	#if MACHINE_AMD64
		#define COMPILER_HAS_SSE 1
		#define COMPILER_HAS_SSE2 1
	#endif

	#if defined(_M_IX86_FP)
		#if _M_IX86_FP >= 1
			#define COMPILER_HAS_SSE 1
		#endif
		#if _M_IX86_FP >= 2
			#define COMPILER_HAS_SSE2 1
		#endif
	#endif

	#if defined(__AVX__)
		#define COMPILER_HAS_SSE3 1
		#define COMPILER_HAS_SSSE3 1
		#define COMPILER_HAS_SSE4_1 1
		#define COMPILER_HAS_SSE4_2 1
		#define COMPILER_HAS_AVX 1
	#endif

	#if defined(__AVX2__)
		#define COMPILER_HAS_AVX2 1
		#define COMPILER_HAS_FMA3 1
	#endif

#endif

#if defined(__ARM_NEON__) || defined(__ARM_NEON)
#define COMPILER_HAS_NEON 1
#endif

#if defined(__ARM_FEATURE_FMA)
#define COMPILER_HAS_ARM_FMA 1
#endif

#ifdef _WIN32
#define OS_WINDOWS 1
#endif

#ifdef __unix__
#define OS_UNIX 1
#endif

#ifdef __linux__
#define OS_LINUX 1
#endif
