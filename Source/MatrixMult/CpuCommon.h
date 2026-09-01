#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void matrix_mult_cpu_compute(void*, const float*, float*);

typedef struct {
	size_t VectorSize;
	size_t nBar;
	float* DftMatrixCos;
	float* DftMatrixSin;
	matrix_mult_cpu_compute* pCompute;
} matrix_mult_cpu_state;

void MatrixMultCpu_Detect(matrix_mult_cpu_state* pState);

void MatrixMultCpu_ComputeSse(void* pState, const float* aSample, float* aOutput);
void MatrixMultCpu_ComputeAvx(void* pState, const float* aSample, float* aOutput);
void MatrixMultCpu_ComputeFma3(void* pState, const float* aSample, float* aOutput);
void MatrixMultCpu_ComputeNeon(void* pState, const float* aSample, float* aOutput);
void MatrixMultCpu_ComputeNeonFma(void* pState, const float* aSample, float* aOutput);
void MatrixMultCpu_ComputeScalar(void* pState, const float* aSample, float* aOutput);
