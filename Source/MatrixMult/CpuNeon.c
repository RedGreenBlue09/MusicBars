
#include <math.h>
#include <stdint.h>

#include "CpuCommon.h"
#include <Utilty/Machine.h>

#if COMPILER_HAS_NEON

#include <arm_neon.h>

static inline float NeonReduceAdd(float32x4_t v) {
	float32x2_t v2 = vadd_f32(vget_low_f32(v), vget_high_f32(v));
	return vget_lane_f32(vpadd_f32(v2, v2), 0);
}

void MatrixMultCpu_ComputeNeon(void* pStateVoid, const float* aSample, float* aOutput) {
	matrix_mult_cpu_state* pState = (matrix_mult_cpu_state*)pStateVoid;
	for (size_t i = 0; i < pState->nBar; ++i) {
		float32x4_t vResultCos = vdupq_n_f32(0.0f);
		float32x4_t vResultSin = vdupq_n_f32(0.0f);
		for (size_t ii = 0; ii < pState->VectorSize / 4 * 4; ii += 4) {
			float32x4_t vSample = vld1q_f32(&aSample[ii]);
			float32x4_t vCos = vld1q_f32(&pState->DftMatrixCos[i * pState->VectorSize + ii]);
			float32x4_t vSin = vld1q_f32(&pState->DftMatrixSin[i * pState->VectorSize + ii]);
			vResultCos = vmlaq_f32(vResultCos, vSample, vCos);
			vResultSin = vmlaq_f32(vResultSin, vSample, vSin);
		}
		float ResultCos = NeonReduceAdd(vResultCos);
		float ResultSin = NeonReduceAdd(vResultSin);
		for (size_t ii = pState->VectorSize / 4 * 4; ii < pState->VectorSize; ++ii) {
			ResultCos += aSample[ii] * pState->DftMatrixCos[i * pState->VectorSize + ii];
			ResultSin += aSample[ii] * pState->DftMatrixSin[i * pState->VectorSize + ii];
		}
		aOutput[i] = sqrtf(ResultCos * ResultCos + ResultSin * ResultSin);
	}
}

#else

void MatrixMultCpu_ComputeNeon(void* pStateVoid, const float* aSample, float* aOutput) {
	// Stub - should not be called when NEON is disabled
	(void)pStateVoid;
	(void)aSample;
	(void)aOutput;
}

#endif
