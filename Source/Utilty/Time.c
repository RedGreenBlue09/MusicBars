
#include <stdint.h>

#include "Utilty/Atomic.h"
#include "Utilty/Machine.h"
#include "Utilty/IntMath.h"

#if OS_WINDOWS

#include <Windows.h>

static atomic uint64_t gClockRes = 0;

uint64_t clock64() {
	LARGE_INTEGER TimeStruct;
	QueryPerformanceCounter(&TimeStruct);
	return TimeStruct.QuadPart;
}

uint64_t clock64_resolution() {
	uint64_t ClockRes = atomic_load_explicit(&gClockRes, memory_order_relaxed);
	if (ClockRes == 0) {
		LARGE_INTEGER TimeStruct;
		QueryPerformanceFrequency(&TimeStruct);
		ClockRes = TimeStruct.QuadPart;
		atomic_store_explicit(&gClockRes, ClockRes, memory_order_relaxed);
	}
	return ClockRes;
}

NTSYSAPI NTSTATUS NTAPI NtQueryTimerResolution(
	OUT PULONG MinimumResolution,
	OUT PULONG MaximumResolution,
	OUT PULONG CurrentResolution
);

static atomic uint32_t gTimerResPeriod = 0;

static void WaitableTimerSleep(int64_t Duration) {
	if (Duration <= 0)
		return; // Avoid overhead
	HANDLE hTimer = CreateWaitableTimerW(NULL, TRUE, NULL);
	if (!hTimer)
		return;

	LARGE_INTEGER DurationStruct = { .QuadPart = -Duration };
	SetWaitableTimer(hTimer, &DurationStruct, 0, NULL, NULL, FALSE);

	WaitForSingleObject(hTimer, INFINITE);
	CloseHandle(hTimer);
	return;
}

// Implementation assumes the clock resolution is at least
// better than the waitable timer resolution.
void sleep64(uint64_t Duration) {
	if (Duration == 0)
		return; // Avoid calculation overhead

	uint64_t StartClockTime = clock64();
	uint64_t ClockRes = clock64_resolution();

	uint32_t TimerResPeriod =
		atomic_load_explicit(&gTimerResPeriod, memory_order_relaxed);
	if (TimerResPeriod == 0) {
		ULONG Unused;
		ULONG Unused2;
		NtQueryTimerResolution(&TimerResPeriod, &Unused, &Unused2);
		atomic_store_explicit(&gTimerResPeriod, TimerResPeriod, memory_order_relaxed);
	}

	int64_t TimerDuration;
	uint64_t Rem;
	if (ClockRes == 10000000) // Skip expensive division.
		TimerDuration = (int64_t)Duration;
	else
		TimerDuration = (int64_t)div_u64(Duration * 10000000, ClockRes, &Rem);

	// Most of the times, waitable timer will sleep more than
	// the specified time by 1 TimerResPeriod or less.
	WaitableTimerSleep(TimerDuration - TimerResPeriod);

	uint64_t TargetClockTime = StartClockTime + Duration;
	while (clock64() < TargetClockTime);

	return;
}

#elif OS_UNIX

#include <time.h>
#include <errno.h>

uint64_t clock64() {
	struct timespec TimeSpec;
	clock_gettime(CLOCK_MONOTONIC, &TimeSpec);
	return (uint64_t)TimeSpec.tv_sec * 1000000000 + TimeSpec.tv_nsec;
}

uint64_t clock64_resolution() {
	// If we use clock_getres() here and convert to CLOCKS_PER_SEC,
	// it would add division overhead to clock64() and sleep64()
	// and also precision loss, due to the design of the API.
	return 1000000000;
}

// Implementation assumes nanosleep precision with busy-wait spinlock
// for higher precision than nanosleep can provide.
void sleep64(uint64_t Duration) {
	if (Duration == 0)
		return; // Avoid calculation overhead

	uint64_t StartClockTime = clock64();

	uint64_t Nanoseconds;
	uint64_t Seconds = div_u64(Duration, 1000000000, &Nanoseconds);
	struct timespec ReqTime = {
		.tv_sec = Seconds,
		.tv_nsec = Nanoseconds
	};

	struct timespec RemTime;
	int Result;
	do {
		Result = clock_nanosleep(CLOCK_MONOTONIC, 0, &ReqTime, &RemTime);
		ReqTime = RemTime;
		// Retry clock_nanosleep if interrupted by signal
	} while (Result == EINTR);

	uint64_t TargetClockTime = StartClockTime + Duration;
	while (clock64() < TargetClockTime);

	return;
}

#endif
