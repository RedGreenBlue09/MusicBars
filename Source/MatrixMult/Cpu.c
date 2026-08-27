
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <Common.h>

typedef struct {
	size_t HistorySize;
	size_t nBar;
	float* DftMatrixCos;
	float* DftMatrixSin;
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
	return pState;
}

void MatrixMultCpu_Compute(void* pStateIn, const float* aSample, float* aOutput) {
	// TODO: AVX2
	matrix_mult_cpu_state* pState = (matrix_mult_cpu_state*)pStateIn;
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
