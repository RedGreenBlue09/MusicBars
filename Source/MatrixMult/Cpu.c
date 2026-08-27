
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <Common.h>
#include <Machine.h>

#if COMPILER_HAS_SSE
#include <immintrin.h>
	#if COMPILER_MSVC
#include <intrin.h>
	#elif COMPILER_GCC
#include <cpuid.h>
#include <x86intrin.h>
	#endif
#endif

#if OS_WINDOWS
#include <Windows.h>
#endif

// TODO: Prevent autovectorization of scalar code

typedef struct {
	size_t HistorySize;
	size_t nBar;
	float* DftMatrixCos;
	float* DftMatrixSin;

	// Runtime detection flags
#if (MACHINE_X86 || MACHINE_AMD64)

	#if COMPILER_HAS_SSE
	bool bUseSse;
	#endif

	#if COMPILER_HAS_AVX
	bool bUseAvx;
	#endif

	#if COMPILER_HAS_FMA3
	bool bUseFma3;
	#endif

#elif MACHINE_ARM

	#if COMPILER_HAS_NEON

	bool bUseNeon;

		#if COMPILER_HAS_ARM_FMA
	bool bUseNeonFma;
		#endif

	#endif

#endif
} matrix_mult_cpu_state;

void* MatrixMultCpu_Init(
	size_t HistorySize,
	size_t nBar,
	float* DftMatrixCos,
	float* DftMatrixSin
) {
	matrix_mult_cpu_state* pState = malloc(sizeof(matrix_mult_cpu_state));
	if (pState == NULL)
		return NULL;
	pState->HistorySize = HistorySize;
	pState->nBar = nBar;
	pState->DftMatrixCos = DftMatrixCos;
	pState->DftMatrixSin = DftMatrixSin;

#if COMPILER_HAS_SSE

	int CpuInfo[4];
	__cpuidex(CpuInfo, 1, 0);
	pState->bUseSse = (CpuInfo[3] & (1 << 25)) != 0;

	#if COMPILER_HAS_AVX

	bool bOsHasAvx = (_xgetbv(0) & 6) == 6;
	pState->bUseAvx = (CpuInfo[2] & (1 << 28)) != 0 && bOsHasAvx;

		#if COMPILER_HAS_FMA3

	pState->bUseFma3 = (CpuInfo[2] & (1 << 12)) != 0 && bOsHasAvx;

		#endif

	#endif

#elif COMPILER_HAS_NEON

	#if OS_WINDOWS

	pState->bUseNeon = true; // Win NT requires NEON for all ARM CPUs.

	#elif OS_LINUX

	pState->bUseNeon = false; // TODO

	#elif OS_MACOS

	pState->bUseNeon = false; // TODO

	#else

	pState->bUseNeon = true; // Assuming true

	#endif

	#if COMPILER_HAS_ARM_FMA

		#if OS_WINDOWS

	pState->bUseNeonFma = IsProcessorFeaturePresent(PF_ARM_FMAC_INSTRUCTIONS_AVAILABLE);

		#elif OS_LINUX

	pState->bUseNeonFma = false; // TODO

		#elif OS_MACOS

	pState->bUseNeonFma = false; // TODO

		#else

	pState->bUseNeonFma = true; // Assuming true

		#endif

	#endif

#endif

	return pState;
}

#if COMPILER_HAS_SSE

float SseReduceAdd(__m128 v) {
	v = _mm_add_ps(v, _mm_shuffle_ps(v, v, _MM_SHUFFLE(2, 3, 0, 1)));
	v = _mm_add_ps(v, _mm_shuffle_ps(v, v, _MM_SHUFFLE(1, 0, 3, 2)));
	return _mm_cvtss_f32(v);
}
	#if COMPILER_HAS_AVX

float AvxReduceAdd(__m256 v) {
	__m128 vLow = _mm256_castps256_ps128(v);
	__m128 vHigh = _mm256_extractf128_ps(v, 1);
	return SseReduceAdd(_mm_add_ps(vLow, vHigh));
}

	#endif

#endif

void MatrixMultCpu_Compute(void* pStateIn, const float* aSample, float* aOutput) {
	matrix_mult_cpu_state* pState = (matrix_mult_cpu_state*)pStateIn;

#if COMPILER_HAS_SSE

	#if COMPILER_HAS_AVX

		#if COMPILER_HAS_FMA3

	if (pState->bUseFma3) {
		for (size_t i = 0; i < pState->nBar; ++i) {
			__m256 vResultCos = _mm256_set1_ps(0.0f);
			__m256 vResultSin = _mm256_set1_ps(0.0f);
			for (size_t ii = 0; ii < pState->HistorySize / 8 * 8; ii += 8) {
				__m256 vSample = _mm256_loadu_ps(&aSample[ii]);
				__m256 vCos = _mm256_loadu_ps(&pState->DftMatrixCos[i * pState->HistorySize + ii]);
				__m256 vSin = _mm256_loadu_ps(&pState->DftMatrixSin[i * pState->HistorySize + ii]);
				vResultCos = _mm256_fmadd_ps(vSample, vCos, vResultCos);
				vResultSin = _mm256_fmadd_ps(vSample, vSin, vResultSin);
			}
			float ResultCos = AvxReduceAdd(vResultCos);
			float ResultSin = AvxReduceAdd(vResultSin);
			for (size_t ii = pState->HistorySize / 8 * 8; ii < pState->HistorySize; ++ii) {
				ResultCos += aSample[ii] * pState->DftMatrixCos[i * pState->HistorySize + ii];
				ResultSin += aSample[ii] * pState->DftMatrixSin[i * pState->HistorySize + ii];
			}
			aOutput[i] = sqrtf(ResultCos * ResultCos + ResultSin * ResultSin);
		}
		return;
	}

		#endif

	if (pState->bUseAvx) {
		for (size_t i = 0; i < pState->nBar; ++i) {
			__m256 vResultCos = _mm256_set1_ps(0.0f);
			__m256 vResultSin = _mm256_set1_ps(0.0f);
			for (size_t ii = 0; ii < pState->HistorySize / 8 * 8; ii += 8) {
				__m256 vSample = _mm256_loadu_ps(&aSample[ii]);
				__m256 vCos = _mm256_loadu_ps(&pState->DftMatrixCos[i * pState->HistorySize + ii]);
				__m256 vSin = _mm256_loadu_ps(&pState->DftMatrixSin[i * pState->HistorySize + ii]);
				vResultCos = _mm256_add_ps(vResultCos, _mm256_mul_ps(vSample, vCos));
				vResultSin = _mm256_add_ps(vResultSin, _mm256_mul_ps(vSample, vSin));
			}
			float ResultCos = AvxReduceAdd(vResultCos);
			float ResultSin = AvxReduceAdd(vResultSin);
			for (size_t ii = pState->HistorySize / 8 * 8; ii < pState->HistorySize; ++ii) {
				ResultCos += aSample[ii] * pState->DftMatrixCos[i * pState->HistorySize + ii];
				ResultSin += aSample[ii] * pState->DftMatrixSin[i * pState->HistorySize + ii];
			}
			aOutput[i] = sqrtf(ResultCos * ResultCos + ResultSin * ResultSin);
		}
		return;
	}

	#endif

	if (pState->bUseSse) {
		for (size_t i = 0; i < pState->nBar; ++i) {
			__m128 vResultCos = _mm_set1_ps(0.0f);
			__m128 vResultSin = _mm_set1_ps(0.0f);
			for (size_t ii = 0; ii < pState->HistorySize / 4 * 4; ii += 4) {
				__m128 vSample = _mm_loadu_ps(&aSample[ii]);
				__m128 vCos = _mm_loadu_ps(&pState->DftMatrixCos[i * pState->HistorySize + ii]);
				__m128 vSin = _mm_loadu_ps(&pState->DftMatrixSin[i * pState->HistorySize + ii]);
				vResultCos = _mm_add_ps(vResultCos, _mm_mul_ps(vSample, vCos));
				vResultSin = _mm_add_ps(vResultSin, _mm_mul_ps(vSample, vSin));
			}
			float ResultCos = SseReduceAdd(vResultCos);
			float ResultSin = SseReduceAdd(vResultSin);
			for (size_t ii = pState->HistorySize / 4 * 4; ii < pState->HistorySize; ++ii) {
				ResultCos += aSample[ii] * pState->DftMatrixCos[i * pState->HistorySize + ii];
				ResultSin += aSample[ii] * pState->DftMatrixSin[i * pState->HistorySize + ii];
			}
			aOutput[i] = sqrtf(ResultCos * ResultCos + ResultSin * ResultSin);
		}
		return;
	}

#elif COMPILER_HAS_NEON

	// TODO

#endif

	// Default scalar ver
	for (size_t i = 0; i < pState->nBar; ++i) {
		// For consitency with GPU, use single-precision accumulators.
		float ResultCos = 0.0f;
		float ResultSin = 0.0f;
		for (size_t ii = 0; ii < pState->HistorySize; ++ii) {
			ResultCos += aSample[ii] * pState->DftMatrixCos[i * pState->HistorySize + ii];
			ResultSin += aSample[ii] * pState->DftMatrixSin[i * pState->HistorySize + ii];
		}
		aOutput[i] = sqrtf(ResultCos * ResultCos + ResultSin * ResultSin);
	}

}

void MatrixMultCpu_Destroy(void* pStateIn) {
	free(pStateIn);
}
