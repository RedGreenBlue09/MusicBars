
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
	static bool b = true;
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
	// Rectangular window
	//return 1.0;
	
	// Parabolic window (norm = 2 / 3)
	//return -4.0 * X * X + 4.0 * X;
	
	// Sine window (norm = 2 / pi)
	return sin(gfPi * X);
	
	// Sinc window (norm = Si(pi) / pi)
	/*
	X = 2.0 * X - 1.0;
	if (X == 0.0)
		return 1.0;
	else
		return sin(gfPi * X) / (gfPi * X);
	*/

	// Hann window (norm = 0.5)
	/*
	double Result = sin(gfPi * X);
	return Result * Result;
	*/
}

// Normalizing factor: Integral from 0 to 1 of the window.
static const double gfDftWindowNorm = 2.0 / 0x1.921FB54442D18p1;//0x1.2DD19DD527867p-1;

typedef enum {
	RendererId_Legacy,
	RendererId_Modern
} renderer_id;

void* RenderLegacy_Init(
	SDL_Window* pWindow,
	size_t WindowW,
	size_t WindowH,
	size_t nBar,
	size_t BarWidth,
	size_t BarGap,
	uint32_t BackgroundColor,
	uint32_t BarColor
);
void RenderLegacy_Render(void* pStateIn, const float* aOutput);
void RenderLegacy_Destroy(void* pStateIn);

void* RenderModern_Init(
	SDL_Window* pWindow,
	size_t WindowW,
	size_t WindowH,
	size_t nBar,
	size_t BarWidth,
	size_t BarGap,
	uint32_t BackgroundColor,
	uint32_t BarColor
);
void RenderModern_Render(void* pStateIn, const float* aOutput);
void RenderModern_Destroy(void* pStateIn);

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

SDL_HitTestResult HitTestCallback(SDL_Window* Window, const SDL_Point* Point, void* Data) {
	(void)Window;
	(void)Data;

	float MouseX;
	float MouseY;
	SDL_MouseButtonFlags ButtonFlag = SDL_GetGlobalMouseState(&MouseX, &MouseY);

	if (ButtonFlag & SDL_BUTTON_RMASK) {
		// TODO: Context menu here
		return SDL_HITTEST_NORMAL;
	}

	return SDL_HITTEST_DRAGGABLE;
}

int main(int argc, char** argv) {

	int Result = 0;

	// TODO: Read from config
	const size_t FreqMin = 25;
	const size_t FreqMax = 250;
	const size_t nBar = 80;
	const size_t HistorySizeMs = 160;
	const double fSensitivity = 16.0;

	const size_t BarGap = 5;
	const size_t BarWidth = 10;
	const size_t WindowW = nBar * BarWidth + (nBar - 1) * BarGap;
	const size_t WindowH = 400;
	const uint32_t BackgroundColor = 0x00000000;
	const uint32_t BarColor = 0xFFFFFFFF;

	// Create SDL window

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		fprintf(stderr, "Unable to initialize SDL: %s\n", SDL_GetError());
		Result = -1;
		goto End;
	}
	SDL_Window* pWindow = SDL_CreateWindow(
		"MusicBars",
		(int)WindowW,
		(int)WindowH,
		SDL_WINDOW_TRANSPARENT | SDL_WINDOW_BORDERLESS
	); // DX12 GPU renderer does not work with TRANSPARENT yet. Waiting on SDL...
	if (!pWindow) {
		fprintf(stderr, "Unable to create the window: %s", SDL_GetError());
		Result = -1;
		goto CleanupSdl;
	}
	if (!SDL_SetWindowHitTest(pWindow, HitTestCallback, NULL)) {
		fprintf(stderr, "Unable to set window hit test callback: %s", SDL_GetError());
	}

	// Create renderer
	void* pRenderState;
	renderer_id RendererId;
	do {
		pRenderState = RenderModern_Init(
			pWindow,
			WindowW,
			WindowH,
			nBar,
			BarWidth,
			BarGap,
			BackgroundColor,
			BarColor
		);
		if (pRenderState != NULL) {
			RendererId = RendererId_Modern;
			break;
		}

		pRenderState = RenderLegacy_Init(
			pWindow,
			WindowW,
			WindowH,
			nBar,
			BarWidth,
			BarGap,
			BackgroundColor,
			BarColor
		);
		if (pRenderState != NULL) {
			RendererId = RendererId_Legacy;
			break;
		}

		fprintf(stderr, "Unable to create the renderer.\n");
		Result = -1;
		goto CleanupWindow;
	} while (false);

	// Build the DFT matrix

	const double fFreqMin = (double)FreqMin;
	const double fFreqMax = (double)FreqMax;
	const size_t SampleRate = 48000;//FreqMax * 12 / 5; // TODO: Clamp to avoid upsampling
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

	ma_result MiniAudioResult;

#if OS_LINUX
	// Find desktop audio capture device on Linux
	ma_context AudioContext;

	MiniAudioResult = ma_context_init(NULL, 0, NULL, &AudioContext);
	if (MiniAudioResult != MA_SUCCESS) {
		fprintf(stderr, "Failed to initialize audio AudioContext. Error code: %i\n", MiniAudioResult);
		Result = -1;
		goto CleanupAudioQueue;
	}

	ma_device_info* aPlaybackInfo;
	ma_uint32 nPlayback;
	ma_device_info* aCaptureInfo;
	ma_uint32 nCapture;

	MiniAudioResult = ma_context_get_devices(&AudioContext, &aPlaybackInfo, &nPlayback, &aCaptureInfo, &nCapture);
	if (MiniAudioResult != MA_SUCCESS) {
		printf("Failed to enumerate devices. Error code: %i\n", MiniAudioResult);
		Result = -1;
		goto CleanupAudioContext;
	}

	ma_device_id* pAudioDeviceId = NULL;
	for (ma_uint32 i = 0; i < nCapture; i++) {
		if (strcasestr(aCaptureInfo[i].name, "monitor") != NULL) {
			pAudioDeviceId = &aCaptureInfo[i].id;
			break;
		}
	}

	if (pAudioDeviceId == NULL) {
		fprintf(stderr, "Warning: No explicit monitor device found. Falling back to default system input.\n");
	}
#endif

#if OS_LINUX
	ma_device_config MiniAudioConfig = ma_device_config_init(ma_device_type_capture);
	MiniAudioConfig.capture.pDeviceID = pAudioDeviceId;
#else
	ma_device_config MiniAudioConfig = ma_device_config_init(ma_device_type_loopback);
#endif

	MiniAudioConfig.capture.format = ma_format_f32;
	MiniAudioConfig.capture.channels = 1;
	MiniAudioConfig.sampleRate = SampleRate;
	MiniAudioConfig.dataCallback = ReceiveAudio;
	MiniAudioConfig.pUserData = &Queue;
	MiniAudioConfig.noFixedSizedCallback = true;

	ma_device AudioDevice;
	MiniAudioResult = ma_device_init(NULL, &MiniAudioConfig, &AudioDevice);
	if (MiniAudioResult != MA_SUCCESS) {
		fprintf(stderr, "Unable to initialize audio device. Error code: %i\n", MiniAudioResult);
		Result = -1;
		goto CleanupAudioQueue;
	}

	MiniAudioResult = ma_device_start(&AudioDevice);
	if (MiniAudioResult != MA_SUCCESS) {
		fprintf(stderr, "Unable to start audio device. Error code: %i\n", MiniAudioResult);
		Result = -1;
		goto CleanupAudioDevice;
	}

	// Render thread

	size_t nSampleCollected = 0;
	size_t iTemp = 0;
	while (true) {
		SDL_Event Event;
		while (SDL_PollEvent(&Event)) {
			if (Event.type == SDL_EVENT_QUIT)
				goto RenderEnd;
			if (Event.type == SDL_EVENT_KEY_DOWN && Event.key.key == SDLK_ESCAPE)
				goto RenderEnd;
		}

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

		// Rotate the buffer & normalize before matrix mult

		for (size_t i = iTemp; i < HistorySize; ++i)
			aSample[i - iTemp] = aSampleTemp[i] / fHistorySize;
		for (size_t i = 0; i < iTemp; ++i)
			aSample[HistorySize - iTemp + i] = aSampleTemp[i] / fHistorySize;

		// Matrix multiplication

		MatrixMultCpu_Compute(pMatrixMultState, aSample, aOutputHeight);
		// Normalize
		for (size_t i = 0; i < nBar; ++i) {
			aOutputHeight[i] = (double)aOutputHeight[i] * gfDftWindowNorm * fSensitivity;
			aOutputHeight[i] = (aOutputHeight[i] > 1.0) ? 1.0 : aOutputHeight[i];
		}

		// Render
		if (RendererId == RendererId_Legacy)
			RenderLegacy_Render(pRenderState, aOutputHeight);
		else if (RendererId == RendererId_Modern)
			RenderModern_Render(pRenderState, aOutputHeight);
	}
	RenderEnd:

	// Cleanup

#if OS_LINUX
	CleanupAudioContext:
	ma_context_uninit(&AudioContext);
#endif

	CleanupAudioDevice:
	ma_device_uninit(&AudioDevice);

	CleanupAudioQueue:
	AudioQueue_Destroy(&Queue);

	CleanupMatrixMult:
	MatrixMultCpu_Destroy(pMatrixMultState);

	CleanupDftMatrix:
	free(aSample);

	CleanupRenderer:
	if (RendererId == RendererId_Legacy)
		RenderLegacy_Destroy(pRenderState);
	else if (RendererId == RendererId_Modern)
		RenderModern_Destroy(pRenderState);

	CleanupWindow:
	SDL_DestroyWindow(pWindow);

	CleanupSdl:
	SDL_Quit();

	End:
	return Result;
}
