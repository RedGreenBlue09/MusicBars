#include <stdint.h>

#include "CpuCommon.h"
#include <Utilty/Machine.h>

#if COMPILER_HAS_SSE

	#if COMPILER_MSVC
#include <intrin.h>
	#elif COMPILER_GCC
#include <cpuid.h>
	#endif

#endif

#ifdef OS_WINDOWS
#include <Windows.h>
#endif

void MatrixMultCpu_Detect(matrix_mult_cpu_state* pState) {
	bool bUseSse = false;
	bool bUseAvx = false;
	bool bUseFma3 = false;
	bool bUseNeon = false;
	bool bUseNeonFma = false;

#if COMPILER_HAS_SSE

	#if COMPILER_MSVC

	int CpuInfo[4];
	__cpuidex(CpuInfo, 1, 0);
	bUseSse = (CpuInfo[3] & (1 << 25)) != 0;

		#if COMPILER_HAS_AVX

	bool bOsHasAvx = (_xgetbv(0) & 6) == 6;
	bUseAvx = (CpuInfo[2] & (1 << 28)) != 0 && bOsHasAvx;

			#if COMPILER_HAS_FMA3

	bUseFma3 = (CpuInfo[2] & (1 << 12)) != 0 && bOsHasAvx;

			#endif

		#endif

	#elif COMPILER_GCC

	int CpuInfo[4];
	__cpuid(1, CpuInfo[0], CpuInfo[1], CpuInfo[2], CpuInfo[3]);
	bUseSse = (CpuInfo[3] & (1 << 25)) != 0;

		#if COMPILER_HAS_AVX

	bUseAvx = (CpuInfo[2] & (1 << 28)) != 0;

			#if COMPILER_HAS_FMA3

	bUseFma3 = (CpuInfo[2] & (1 << 12)) != 0;

			#endif

		#endif

	#endif

#elif COMPILER_HAS_NEON

	#if OS_WINDOWS

	bUseNeon = true; // Windows NT requires NEON for all ARM CPUs.

	#elif OS_LINUX

	bUseNeon = false; // TODO

	#elif OS_MACOS

	bUseNeon = false; // TODO

	#else

	bUseNeon = true; // Assuming true

	#endif

	#if COMPILER_HAS_ARM_FMA

		#if OS_WINDOWS

	bUseNeonFma = IsProcessorFeaturePresent(PF_ARM_FMAC_INSTRUCTIONS_AVAILABLE);

		#elif OS_LINUX

	bUseNeonFma = false; // TODO

		#elif OS_MACOS

	bUseNeonFma = false; // TODO

		#else

	bUseNeonFma = true; // Assuming true

		#endif

	#endif

#endif
	if (bUseFma3)
		pState->pCompute = MatrixMultCpu_ComputeFma3;
	else if (bUseAvx)
		pState->pCompute = MatrixMultCpu_ComputeAvx;
	else if (bUseSse)
		pState->pCompute = MatrixMultCpu_ComputeSse;
	else if (bUseNeonFma)
		pState->pCompute = MatrixMultCpu_ComputeNeonFma;
	else if (bUseNeon)
		pState->pCompute = MatrixMultCpu_ComputeNeon;
	else
		pState->pCompute = MatrixMultCpu_ComputeScalar;
	
}
