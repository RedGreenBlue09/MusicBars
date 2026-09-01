
#include <math.h>
#include <stdint.h>

#include "CpuCommon.h"
#include <Utilty/Machine.h>

#if COMPILER_HAS_SSE

#include <immintrin.h>

static inline float SseReduceAdd(__m128 v) {
	v = _mm_add_ps(v, _mm_shuffle_ps(v, v, _MM_SHUFFLE(2, 3, 0, 1)));
	v = _mm_add_ps(v, _mm_shuffle_ps(v, v, _MM_SHUFFLE(1, 0, 3, 2)));
	return _mm_cvtss_f32(v);
}

void MatrixMultCpu_ComputeSse(void* pStateVoid, const float* aSample, float* aOutput) {
	matrix_mult_cpu_state* pState = (matrix_mult_cpu_state*)pStateVoid;
	for (size_t i = 0; i < pState->nBar; ++i) {
		__m128 vResultCos = _mm_set1_ps(0.0f);
		__m128 vResultSin = _mm_set1_ps(0.0f);
		for (size_t ii = 0; ii < pState->VectorSize / 4 * 4; ii += 4) {
			__m128 vSample = _mm_loadu_ps(&aSample[ii]);
			__m128 vCos = _mm_loadu_ps(&pState->DftMatrixCos[i * pState->VectorSize + ii]);
			__m128 vSin = _mm_loadu_ps(&pState->DftMatrixSin[i * pState->VectorSize + ii]);
			vResultCos = _mm_add_ps(vResultCos, _mm_mul_ps(vSample, vCos));
			vResultSin = _mm_add_ps(vResultSin, _mm_mul_ps(vSample, vSin));
		}
		float ResultCos = SseReduceAdd(vResultCos);
		float ResultSin = SseReduceAdd(vResultSin);
		for (size_t ii = pState->VectorSize / 4 * 4; ii < pState->VectorSize; ++ii) {
			ResultCos += aSample[ii] * pState->DftMatrixCos[i * pState->VectorSize + ii];
			ResultSin += aSample[ii] * pState->DftMatrixSin[i * pState->VectorSize + ii];
		}
		aOutput[i] = sqrtf(ResultCos * ResultCos + ResultSin * ResultSin);
	}
}

#else

void MatrixMultCpu_ComputeSse(void* pStateVoid, const float* aSample, float* aOutput) {
	// Stub - should not be called when SSE is disabled
	(void)pStateVoid;
	(void)aSample;
	(void)aOutput;
}

#endif
