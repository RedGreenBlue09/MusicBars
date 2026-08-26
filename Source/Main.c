
#define MINIAUDIO_IMPLEMENTATION

#include <math.h>
#include <stdbool.h>
#include <threads.h>

#include <miniaudio.h>
#include <SDL3/SDL.h>

#include <Common.h>
#include <Atomic.h>

#define CACHE_LINE_SIZE 128

// Based on https://github.com/rigtorp/SPSCQueue

typedef struct {
	float* aSlot;
	size_t Capacity;
	alignas(CACHE_LINE_SIZE) atomic size_t iWrite;
	alignas(CACHE_LINE_SIZE) size_t iWriteCached;
	alignas(CACHE_LINE_SIZE) atomic size_t iRead;
	alignas(CACHE_LINE_SIZE) size_t iReadCached;
} audio_queue;

static bool AudioQueue_Create(audio_queue* pQueue, size_t Capacity) {
	if (Capacity < 1)
		return false;
	pQueue->aSlot = malloc(array_size(pQueue->aSlot, Capacity));
	if (pQueue->aSlot == NULL)
		return false;
	pQueue->Capacity = Capacity + 1; // Needs 1 extra
	atomic_init(&pQueue->iWrite, 0);
	pQueue->iWriteCached = 0;
	atomic_init(&pQueue->iRead, 0);
	pQueue->iReadCached = 0;
	return true;
}

static void AudioQueue_Destroy(audio_queue* pQueue) {
	free(pQueue->aSlot);
}

static bool AudioQueue_TryPush(audio_queue* pQueue, float Sample) {
	const size_t iWrite = atomic_load_explicit(&pQueue->iWrite, memory_order_relaxed);
	size_t iWriteNext = iWrite + 1;
	iWriteNext = (iWriteNext >= pQueue->Capacity) ? 0 : iWriteNext;

	if (iWriteNext == pQueue->iReadCached) {
		pQueue->iReadCached = atomic_load_explicit(&pQueue->iRead, memory_order_acquire);
		if (iWriteNext == pQueue->iReadCached)
			return false;
	}

	pQueue->aSlot[iWrite] = Sample;
	atomic_store_explicit(&pQueue->iWrite, iWriteNext, memory_order_release);
	return true;
}

static bool AudioQueue_TryPop(audio_queue* pQueue, float* pSample) {
	size_t iRead = atomic_load_explicit(&pQueue->iRead, memory_order_relaxed);
	if (iRead == pQueue->iWriteCached) {
		pQueue->iWriteCached = atomic_load_explicit(&pQueue->iWrite, memory_order_acquire);
		if (iRead == pQueue->iWriteCached) {
			return false;
		}
	}
	*pSample = pQueue->aSlot[iRead];
	size_t iReadNext = iRead + 1;
	iReadNext = (iReadNext >= pQueue->Capacity) ? 0 : iReadNext;

	atomic_store_explicit(&pQueue->iRead, iReadNext, memory_order_release);
	return true;
}

static void ReceiveAudio(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 nFrame) {
	const float* aInputSample = (const float*)pInput;
	audio_queue* pQueue = (audio_queue*)pDevice->pUserData;
	for (ma_uint32 i = 0; i < nFrame; i++) {
		if (!AudioQueue_TryPush(pQueue, aInputSample[i])) {
			// Drop in bulk so that the output has a clear cut off
			// instead of a mess of randomly dropped samples.
			break;
		}
	}
}

static const double gfPi = 0x1.921FB54442D18p1;

static inline double DftWindow(double X) {
	// Sinc window
	X = 2.0 * X - 1.0;
	if (X == 0.0)
		return 1.0;
	else
		return sin(gfPi * X) / (gfPi * X);
}

// Normalizing factor: Integral from 0 to 1 of the window.
static const double gDftWindowNorm = 0x1.2DD19DD527867p-1; // Si(pi) / pi

int main(int argc, char** argv) {

	int Result = 0;

	// TODO: Read from config
	const size_t FreqMin = 25;
	const size_t FreqMax = 250;
	const size_t nBar = 80;
	const size_t HistorySizeMs = 250;

	const size_t FreqRange = FreqMax - FreqMin;

	// Build the DFT matrix

	const double fFreqMin = (double)FreqMin;
	const double fFreqMax = (double)FreqMax;
	const size_t SampleRate = FreqMax * 12 / 5; // TODO: Clamp to avoid upsampling
	const double fSampleRate = (double)SampleRate;
	const size_t HistorySize = div_roundup(HistorySizeMs * SampleRate, 1000);
	const double fHistorySize = (double)HistorySize;
	const double fHistorySizeFactor = (double)HistorySize / (double)SampleRate;

	// TODO: Actual arena
	float* aSample;
	float* DftMatrixCos;
	float* DftMatrixSin;
	aSample = malloc(
		array_size(aSample, HistorySize) +
		array_size(DftMatrixCos, nBar * HistorySize) +
		array_size(DftMatrixSin, nBar * HistorySize)
	);
	if (aSample == NULL) {
		fprintf(stderr, "Unable to allocate DFT matrix.\n");
		Result = -1;
		goto End;
	}
	DftMatrixCos = &aSample[HistorySize];
	DftMatrixSin = &DftMatrixCos[nBar * HistorySize];

	for (size_t i = 0; i < nBar; ++i) {
		double Freq = fFreqMin * pow(fFreqMax / fFreqMin, (double)i / (double)(nBar - 1));
		for (size_t ii = 0; ii < HistorySize; ++ii) {
			double fPosition = (double)ii / fHistorySize;
			double fAngle = 2.0 * gfPi * Freq * fPosition;
			double fWindowFactor = DftWindow(fPosition / fHistorySizeFactor);
			DftMatrixCos[i * nBar + ii] = (float)(cos(fAngle) * fWindowFactor);
			DftMatrixSin[i * nBar + ii] = (float)(sin(fAngle) * fWindowFactor);
		}
	}

	// Initialize the audio queue

	audio_queue Queue;
	// 1s of buffer to minimize the chance of dropping samples
	if (!AudioQueue_Create(&Queue, SampleRate)) {
		fprintf(stderr, "Unable to initialize audio queue.\n");
		Result = -1;
		goto CleanupDftMatrix;
	}

	// Start recording desktop audio

	ma_device_config config = ma_device_config_init(ma_device_type_loopback);
	config.capture.format = ma_format_f32;
	config.capture.channels = 1;
	config.sampleRate = SampleRate;
	config.dataCallback = ReceiveAudio;

	ma_device device;
	if (ma_device_init(NULL, &config, &device) != MA_SUCCESS) {
		fprintf(stderr, "Unable to initialize audio device.\n");
		Result = -1;
		goto CleanupAudioQueue;
	}

	if (ma_device_start(&device) != MA_SUCCESS) {
		fprintf(stderr, "Unable to start audio device.\n");
		Result = -1;
		goto CleanupAudioDevice;
	}

	// Render thread

	while (true) {
		break;
	}

	// Cleanup

	CleanupAudioDevice:
	ma_device_uninit(&device);

	CleanupAudioQueue:
	AudioQueue_Destroy(&Queue);

	CleanupDftMatrix:
	free(aSample);

	End:
	return Result;
}
