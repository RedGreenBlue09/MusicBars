
#include <stdbool.h>

void* AudioCapture_Init(
	size_t SampleRate,
	size_t HistorySize,
	const char* sCaptureDevice
);
void AudioCapture_GetSamples(void* pState, float* aSample);
void AudioCapture_Uninit(void* pState);
