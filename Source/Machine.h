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
