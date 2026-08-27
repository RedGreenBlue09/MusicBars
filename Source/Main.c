
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
	pQueue->aSlot = malloc(array_size(pQueue->aSlot, Capacity + 1));
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
	// Welch
	//return -4.0 * X * X + 4.0 * X;
}

// Normalizing factor: Integral from 0 to 1 of the window.
static const double gfDftWindowNorm = 0x1.2DD19DD527867p-1; // Si(pi) / pi

void* RenderCpu_Init(
	SDL_Window* pWindow,
	size_t WindowW,
	size_t WindowH,
	size_t nBar,
	size_t BarWidth,
	size_t BarGap
);
void RenderCpu_Render(void* pStateIn, const float* aOutput);
void RenderCpu_Destroy(void* pStateIn);

void* MatrixMultCpu_Init(
	size_t HistorySize,
	size_t nBar,
	float* DftMatrixCos,
	float* DftMatrixSin
);

void* MatrixMultCpu_Init(
	size_t HistorySize,
	size_t nBar,
	float* DftMatrixCos,
	float* DftMatrixSin
);
void MatrixMultCpu_Compute(void* pStateIn, const float* aSample, float* aOutput);
void MatrixMultCpu_Destroy(void* pStateIn);

int main(int argc, char** argv) {

	int Result = 0;

	// TODO: Read from config
	const size_t FreqMin = 25;
	const size_t FreqMax = 250;
	const size_t nBar = 80;
	const size_t HistorySizeMs = 200;
	const double fSensitivity = 40.0;

	const size_t BarGap = 5;
	const size_t BarWidth = 10;
	const size_t WindowW = nBar * BarWidth + (nBar - 1) * BarGap;
	const size_t WindowH = 400;

	// Create SDL window

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		Result = -1;
		goto End;
	}
	SDL_Window* pWindow = SDL_CreateWindow(
		"MusicBars",
		(int)WindowW,
		(int)WindowH,
		SDL_WINDOW_TRANSPARENT | SDL_WINDOW_BORDERLESS
	);
	if (!pWindow) {
		fprintf(stderr, "SDL_CreateWindow failed: %s", SDL_GetError());
		Result = -1;
		goto CleanupSdl;
	}

	// Create renderer

	void* pRenderState = RenderCpu_Init(
		pWindow,
		WindowW,
		WindowH,
		nBar,
		BarWidth,
		BarGap
	);
	if (pRenderState == NULL) {
		fprintf(stderr, "Unable to create the renderer.\n");
		Result = -1;
		goto CleanupWindow;
	}

	// Build the DFT matrix

	const double fFreqMin = (double)FreqMin;
	const double fFreqMax = (double)FreqMax;
	const size_t SampleRate = FreqMax * 12 / 5; // TODO: Clamp to avoid upsampling
	const double fSampleRate = (double)SampleRate;
	const size_t HistorySize = div_roundup(HistorySizeMs * SampleRate, 1000);
	const double fHistorySize = (double)HistorySize;

	// TODO: Actual arena
	float* aSample;
	float* aSampleTemp; // Buffer for rotation
	float* aOutputHeight;
	float* DftMatrixCos;
	float* DftMatrixSin;
	aSample = malloc(
		array_size(aSample, HistorySize) +
		array_size(aSampleTemp, HistorySize) +
		array_size(aOutputHeight, nBar) +
		array_size(DftMatrixCos, nBar * HistorySize) +
		array_size(DftMatrixSin, nBar * HistorySize)
	);
	if (aSample == NULL) {
		fprintf(stderr, "Unable to allocate DFT matrix.\n");
		Result = -1;
		goto CleanupRenderer;
	}
	aSampleTemp = &aSample[HistorySize];
	aOutputHeight = &aSampleTemp[HistorySize];
	DftMatrixCos = &aOutputHeight[nBar];
	DftMatrixSin = &DftMatrixCos[nBar * HistorySize];

	for (size_t i = 0; i < nBar; ++i) {
		double fFreq = fFreqMin * pow(fFreqMax / fFreqMin, (double)i / (double)(nBar - 1));
		for (size_t ii = 0; ii < HistorySize; ++ii) {
			double fAngle = 2.0 * gfPi * fFreq * (double)ii / fSampleRate;
			double fWindowFactor = DftWindow((double)ii / fHistorySize);
			DftMatrixCos[i * HistorySize + ii] = (float)(cos(fAngle) * fWindowFactor);
			DftMatrixSin[i * HistorySize + ii] = (float)(sin(fAngle) * fWindowFactor);
		}
	}

	// Create matrix multiplication engine

	void* pMatrixMultState = MatrixMultCpu_Init(
		HistorySize,
		nBar,
		DftMatrixCos,
		DftMatrixSin
	);
	if (pMatrixMultState == NULL) {
		fprintf(stderr, "Unable to initialize the matrix multiplication engine.\n");
		Result = -1;
		goto CleanupDftMatrix;
	}

	// Initialize the audio queue

	audio_queue Queue;
	// 1s of buffer to minimize the chance of dropping samples
	if (!AudioQueue_Create(&Queue, SampleRate)) {
		fprintf(stderr, "Unable to initialize audio queue.\n");
		Result = -1;
		goto CleanupMatrixMult;
	}

	// Start recording desktop audio

	ma_device_config MiniAudioConfig = ma_device_config_init(ma_device_type_loopback);
	MiniAudioConfig.capture.format = ma_format_f32;
	MiniAudioConfig.capture.channels = 1;
	MiniAudioConfig.sampleRate = SampleRate;
	MiniAudioConfig.dataCallback = ReceiveAudio;
	MiniAudioConfig.pUserData = &Queue;

	ma_device device;
	if (ma_device_init(NULL, &MiniAudioConfig, &device) != MA_SUCCESS) {
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
	size_t nSampleCollected = 0;
	size_t iTemp = 0;
	while (true) {
		// Add the newest samples to aSampleTemp

		float fSample;
		while (AudioQueue_TryPop(&Queue, &fSample)) {
			aSampleTemp[iTemp++] = fSample;
			iTemp = (iTemp >= HistorySize) ? 0 : iTemp;
			nSampleCollected = (nSampleCollected < HistorySize) ? nSampleCollected + 1 : HistorySize;
		}
		if (nSampleCollected < HistorySize) {
			thrd_yield(); // TODO: Proper backoff
			continue;
		}

		// Rotate the buffer

		for (size_t i = iTemp; i < HistorySize; ++i)
			aSample[i - iTemp] = aSampleTemp[i];
		for (size_t i = 0; i < iTemp; ++i)
			aSample[HistorySize - iTemp + i] = aSampleTemp[i];

		// Matrix multiplication

		MatrixMultCpu_Compute(pMatrixMultState, aSample, aOutputHeight);
		// Normalize
		for (size_t i = 0; i < nBar; ++i)
			aOutputHeight[i] = (double)aOutputHeight[i] * gfDftWindowNorm * fSensitivity;

		// Render

		RenderCpu_Render(pRenderState, aOutputHeight);
	}

	// Cleanup

	CleanupAudioDevice:
	ma_device_uninit(&device);

	CleanupAudioQueue:
	AudioQueue_Destroy(&Queue);

	CleanupMatrixMult:
	MatrixMultCpu_Destroy(pMatrixMultState);

	CleanupDftMatrix:
	free(aSample);

	CleanupRenderer:
	RenderCpu_Destroy(pRenderState);

	CleanupWindow:
	SDL_DestroyWindow(pWindow);

	CleanupSdl:
	SDL_Quit();

	End:
	return Result;
}
