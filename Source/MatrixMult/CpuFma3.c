
#include <math.h>
#include <stdint.h>

#include "CpuCommon.h"
#include <Utilty/Machine.h>

#if COMPILER_HAS_FMA3

#include <immintrin.h>

static inline float SseReduceAdd(__m128 v) {
	v = _mm_add_ps(v, _mm_shuffle_ps(v, v, _MM_SHUFFLE(2, 3, 0, 1)));
	v = _mm_add_ps(v, _mm_shuffle_ps(v, v, _MM_SHUFFLE(1, 0, 3, 2)));
	return _mm_cvtss_f32(v);
}

static inline float AvxReduceAdd(__m256 v) {
	__m128 vLow = _mm256_castps256_ps128(v);
	__m128 vHigh = _mm256_extractf128_ps(v, 1);
	return SseReduceAdd(_mm_add_ps(vLow, vHigh));
}

void MatrixMultCpu_ComputeFma3(void* pStateVoid, const float* aSample, float* aOutput) {
	matrix_mult_cpu_state* pState = (matrix_mult_cpu_state*)pStateVoid;
	for (size_t i = 0; i < pState->nBar; ++i) {
		__m256 vResultCos = _mm256_set1_ps(0.0f);
		__m256 vResultSin = _mm256_set1_ps(0.0f);
		for (size_t ii = 0; ii < pState->VectorSize / 8 * 8; ii += 8) {
			__m256 vSample = _mm256_loadu_ps(&aSample[ii]);
			__m256 vCos = _mm256_loadu_ps(&pState->DftMatrixCos[i * pState->VectorSize + ii]);
			__m256 vSin = _mm256_loadu_ps(&pState->DftMatrixSin[i * pState->VectorSize + ii]);
			vResultCos = _mm256_fmadd_ps(vSample, vCos, vResultCos);
			vResultSin = _mm256_fmadd_ps(vSample, vSin, vResultSin);
		}
		float ResultCos = AvxReduceAdd(vResultCos);
		float ResultSin = AvxReduceAdd(vResultSin);
		for (size_t ii = pState->VectorSize / 8 * 8; ii < pState->VectorSize; ++ii) {
			ResultCos = fmaf(aSample[ii], pState->DftMatrixCos[i * pState->VectorSize + ii], ResultCos);
			ResultSin = fmaf(aSample[ii], pState->DftMatrixSin[i * pState->VectorSize + ii], ResultSin);
		}
		aOutput[i] = sqrtf(ResultCos * ResultCos + ResultSin * ResultSin);
	}
}

#else

void MatrixMultCpu_ComputeFma3(void* pStateVoid, const float* aSample, float* aOutput) {
	// Stub - should not be called when FMA3 is disabled
	(void)pStateVoid;
	(void)aSample;
	(void)aOutput;
}

#endif
