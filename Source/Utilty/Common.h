#pragma once

#include <Utilty/Machine.h>

// Useful macros

#define static_arrlen(X) (sizeof(X) / sizeof(*(X)))
#define static_strlen(X) (static_arrlen(X) - 1)
#define sizeof_member(type, member) (sizeof(((type*)0)->member))
#define array_size(a, n) (sizeof((a)[0]) * (n))
#define div_roundup(A, B) ((A) / (B) + ((A) % (B) != 0))
#define div_round(A, B) ((A) / (B) + ((A) % (B) >= ((B) / 2 + (B) % 2)))
#define min_macro(A, B) ((A) < (B) ? (A) : (B))
#define max_macro(A, B) ((A) > (B) ? (A) : (B))

// Language extensions

#if COMPILER_MSVC
#define ext_forceinline __forceinline
#elif COMPILER_GCC
#define ext_forceinline __attribute__((always_inline))
#else
#define ext_forceinline inline
#endif

#if COMPILER_MSVC
#define ext_noinline __declspec(noinline)
#elif COMPILER_GCC
#define ext_noinline __attribute__((noinline))
#else
#define ext_noinline
#endif

#if COMPILER_MSVC
#define ext_noreturn __declspec(noreturn)
#elif COMPILER_GCC
#define ext_noreturn __attribute__((noreturn))
#else
#define ext_noreturn
#endif

#if COMPILER_MSVC
static ext_noreturn ext_forceinline void ext_unreachable() { __assume(0); }
#elif COMPILER_GCC
static ext_noreturn ext_forceinline void ext_unreachable() { __builtin_unreachable(); }
#else
static ext_noreturn ext_forceinline void ext_unreachable() {}
#endif

#define only_reachable(X) {if (!(X)) ext_unreachable();}
