
#include <math.h>
#include <stdlib.h>

#include "CpuCommon.h"

void* MatrixMultCpu_Init(
	size_t VectorSize,
	size_t nBar,
	float* DftMatrixCos,
	float* DftMatrixSin
) {
	matrix_mult_cpu_state* pState = malloc(sizeof(matrix_mult_cpu_state));
	if (pState == NULL)
		return NULL;
	pState->VectorSize = VectorSize;
	pState->nBar = nBar;
	pState->DftMatrixCos = DftMatrixCos;
	pState->DftMatrixSin = DftMatrixSin;
	MatrixMultCpu_Detect(pState);
	return pState;
}

void MatrixMultCpu_Compute(void* pStateVoid, const float* aSample, float* aOutput) {
	matrix_mult_cpu_state* pState = (matrix_mult_cpu_state*)pStateVoid;
	pState->pCompute(pStateVoid, aSample, aOutput);
}

void MatrixMultCpu_ComputeScalar(void* pStateVoid, const float* aSample, float* aOutput) {
	matrix_mult_cpu_state* pState = (matrix_mult_cpu_state*)pStateVoid;
	for (size_t i = 0; i < pState->nBar; ++i) {
		// For consistency with GPU, use single-precision accumulators.
		float ResultCos = 0.0f;
		float ResultSin = 0.0f;
		for (size_t ii = 0; ii < pState->VectorSize; ++ii) {
			ResultCos += aSample[ii] * pState->DftMatrixCos[i * pState->VectorSize + ii];
			ResultSin += aSample[ii] * pState->DftMatrixSin[i * pState->VectorSize + ii];
		}
		aOutput[i] = sqrtf(ResultCos * ResultCos + ResultSin * ResultSin);
	}
}

void MatrixMultCpu_Destroy(void* pStateVoid) {
	free(pStateVoid);
}
