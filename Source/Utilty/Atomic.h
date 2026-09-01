#pragma once
#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <assert.h>

#include <Utilty/Machine.h>

#define atomic _Atomic
#define non_atomic(X) alignas(atomic X) X

// Light fence to prevent full fence when applicable (eg: ARM64)
// Get maximum optimizations while still compatible with C memory model.

#if defined(MACHINE_ARM64)

#define atomic_thread_fence_light(pAtomicVar, Order) {       \
	switch (Order) {                                         \
		case memory_order_acquire:                           \
			atomic_load_explicit(pAtomicVar, Order);         \
			break;                                           \
		case memory_order_release:                           \
		case memory_order_acq_rel:                           \
			atomic_fetch_add_explicit(pAtomicVar, 0, Order); \
			break;                                           \
		default:                                             \
			assert(false);                                   \
	}                                                        \
}

#else

#define atomic_thread_fence_light(pAtomicVar, Order) \
	atomic_thread_fence(Order)

#endif

// Store-release, compatible with atomic_thread_fence_light()

#if defined(MACHINE_ARM64)

#define atomic_store_fence_light(pAtomicVar, Value) \
	atomic_store_explicit(pAtomicVar, Value, memory_order_release)

#else

#define atomic_store_fence_light(pAtomicVar, Value) {               \
	atomic_thread_fence_light(pAtomicVar, memory_order_release);    \
	atomic_store_explicit(pAtomicVar, Value, memory_order_relaxed); \
}

#endif

// Read-modify-write, compatible with atomic_thread_fence_light()
// Return value can be supported with pointer, but we don't need that yet.

#if defined(MACHINE_ARM64)

#define atomic_rmw_fence_light(Operation, pAtomicVar, Value, Order) \
	Operation(pAtomicVar, Value, Order)

#else

#define atomic_rmw_fence_light(Operation, pAtomicVar, Value, Order) {   \
	if (Order == memory_order_release || Order == memory_order_acq_rel) \
		atomic_thread_fence_light(pAtomicVar, memory_order_release);    \
	Operation(pAtomicVar, Value, memory_order_relaxed);                 \
	if (Order == memory_order_acquire || Order == memory_order_acq_rel) \
		atomic_thread_fence_light(pAtomicVar, memory_order_acquire);    \
}

#endif

#define atomic_add_fence_light(pAtomicVar, Value, Order) \
	atomic_rmw_fence_light(atomic_fetch_add_explicit, pAtomicVar, Value, Order)
#define atomic_sub_fence_light(pAtomicVar, Value, Order) \
	atomic_rmw_fence_light(atomic_fetch_sub_explicit, pAtomicVar, Value, Order)
#define atomic_and_fence_light(pAtomicVar, Value, Order) \
	atomic_rmw_fence_light(atomic_fetch_and_explicit, pAtomicVar, Value, Order)
