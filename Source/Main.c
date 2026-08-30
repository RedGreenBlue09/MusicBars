
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
static const double gfSincIntegral = 0x1.2DD19DD527867p-1; // Si(pi) / pi

static inline double DftWindow(double X) {
	// Rectangular window
	// Norm = 1
	// Main lobe = 2
	
	//return 1.0;
	
	// Parabolic (Welch) window
	// Norm = 2 / 3
	// Main lobe ~ 2.8606
	
	//return -4.0 * X * X + 4.0 * X;
	
	// Sine window
	// Norm = 2 / pi = 0x1.45F306DC9C883p-1
	// Main lobe = 3
	
	//return sin(gfPi * X);
	
	// Sinc window
	// Norm = Si(pi) / pi = 0x1.2DD19DD527867p-1
	// Main lobe ~ 3.277
	
	//X = 2.0 * X - 1.0;
	//return (X == 0.0) ? 1.0 : (sin(gfPi * X) / (gfPi * X));

	// Hann window
	// Norm = 0.5
	// Main lobe = 4

	//double Result = sin(gfPi * X);
	//return Result * Result;

	// Hann-Poisson window
	// Norm = (1 - 1 / e) / 2 + (1 + 1 / e) / (2 * (1 + pi ^ 2)) = 0x1.8413FD8338362p-2
	// Main lobe = inf, but we use from Hann

	double Sin = sin(gfPi * X);
	return exp(-2.0 * fabs(X - 0.5)) * Sin * Sin;
}

// Normalizing factor: Integral from 0 to 1 of the window.
static const double gfDftWindowNorm = 0x1.8413FD8338362p-2;
static const double gfDftWindowMainLobe = 4;

typedef enum {
	RendererId_Legacy,
	RendererId_Modern
} renderer_id;

void* RenderLegacy_Init(
	SDL_Window* pWindow,
	size_t WindowW,
	size_t WindowH,
	size_t nBar,
	float fBarWidth,
	float fBarGap,
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
	float fBarWidth,
	float fBarGap,
	uint32_t BackgroundColor,
	uint32_t BarColor
);
void RenderModern_Render(void* pStateIn, const float* aOutput);
void RenderModern_Destroy(void* pStateIn);

void* MatrixMultCpu_Init(
	size_t VectorSize,
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
	// Trap Nation
	//const double fFreqMin = 25;
	//const double fFreqMax = 140;
	//const size_t HistorySizeMs = 150;
	const double fFreqMin = 25;
	const double fFreqMax = 20000;
	const size_t HistorySizeMs = 150;
	const double fSensitivity = 5.0;
	const bool bLogScale = true;
	const bool bUniformMainLobe = true;

	// Stress test
	//const size_t nBar = 1200;
	//const float fBarGap = 0.0f;
	//const float fBarWidth = 1.0f;

	const size_t nBar = 80;
	const float fBarGap = 5.0f;
	const float fBarWidth = 10.0f;
	const double fnBar = (double)nBar;
	const size_t WindowW =
		(size_t)(fnBar * fBarWidth + (fnBar - 1.0) * fBarGap);
	const size_t WindowH = 400;
	const uint32_t BackgroundColor = 0x0000007F;
	const uint32_t BarColor = 0xFFFFFFFF;

	// Create SDL window

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		fprintf(stderr, "Unable to initialize SDL: %s\n", SDL_GetError());
		Result = -1;
		goto End;
	}
	SDL_Window* pWindow = SDL_CreateWindow(
		"MusicBars",
		(int)WindowW, // TODO: set size to 1 and let the renderer cook
		(int)WindowH,
		SDL_WINDOW_BORDERLESS | SDL_WINDOW_TRANSPARENT
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
			fBarWidth,
			fBarGap,
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
			fBarWidth,
			fBarGap,
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

	// TODO: Calculate minimum sample rate required.
	const size_t SampleRate = 48000;//FreqMax * 12 / 5;
	const double fSampleRate = (double)SampleRate;
	const size_t HistorySize = div_roundup(HistorySizeMs * SampleRate, 1000);
	const double fHistorySize = (double)HistorySize;
	const double fHistorySizeSec = fHistorySize / fSampleRate;

	// TODO: Actual arena
	float* aSample;
	float* aSampleTemp; // Buffer for rotation
	float* aOutputHeight;
	float* aOutputHeightOld;
	float* DftMatrixCos;
	float* DftMatrixSin;
	aSample = malloc(
		array_size(aSample, HistorySize) +
		array_size(aSampleTemp, HistorySize) +
		array_size(aOutputHeight, nBar) +
		array_size(aOutputHeightOld, nBar) +
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
	aOutputHeightOld = &aOutputHeight[nBar];
	DftMatrixCos = &aOutputHeightOld[nBar];
	DftMatrixSin = &DftMatrixCos[nBar * HistorySize];

	for (size_t i = 0; i < nBar; ++i) {
		// MainLobe = gfDftWindowMainLobe / fHistorySizeSec
		// If log scale, we scale it accordingly.
		// But in both log & linear scale, the main lobe might be
		// smaller than the frequency range covered by a single bar.
		// Largest range covered by a bar:
		// Log scale: fFreqMax * (1 - 1 / (fFreqMax / fFreqMin) ^ (1 / (nBar - 1))) 
		// Linear scale: (fFreqMax - fFreqMin) / nBar
		// In addition, the main lobe has the sinc shape,
		// in which the power reduces to 0 at the edge.
		// This shape has an area of Si(pi) / pi ~ 0.58
		// Therefore, we want at least 58% overlap between each main lobe
		// so inputs with frequency at the edge can still be seen.
		// In other words, we want every main lobe to cover at least 
		// ~ pi / Si(pi) ~ 1.7 bars.
		// Lastly, if HistorySize is too small, there might be aliasing.
		// Upsampling is a workaround.

		double fFreq;
		if (bLogScale)
			fFreq = fFreqMin * pow(fFreqMax / fFreqMin, (double)i / (fnBar - 1.0));
		else
			fFreq = fFreqMin + (fFreqMax - fFreqMin) * ((double)i / (fnBar - 1.0));

		double FrequencyGap;
		if (bLogScale)
			FrequencyGap = fFreq * (pow(fFreqMax / fFreqMin, 1.0 / (fnBar - 1.0)) - 1.0);
		else
			FrequencyGap = (fFreqMax - fFreqMin) / fnBar;
		size_t MaxLocalHistorySize =
			(size_t)(fSampleRate * gfDftWindowMainLobe * gfSincIntegral / FrequencyGap);

		size_t LocalHistorySize;
		if (bLogScale && bUniformMainLobe)
			LocalHistorySize = (size_t)(fHistorySize * (fFreqMin / fFreq));
		else
			LocalHistorySize = HistorySize;

		LocalHistorySize = min_macro(LocalHistorySize, MaxLocalHistorySize);
		double fLocalHistorySize = (double)LocalHistorySize;

		for (size_t ii = 0; ii < HistorySize - LocalHistorySize; ++ii) {
			DftMatrixCos[i * HistorySize + ii] = 0.0f;
			DftMatrixSin[i * HistorySize + ii] = 0.0f;
		}
		for (size_t ii = HistorySize - LocalHistorySize; ii < HistorySize; ++ii) {
			size_t iii = ii - (HistorySize - LocalHistorySize);
			double fAngle = 2.0 * gfPi * fFreq * (double)iii / fSampleRate;
			double fWindowFactor = DftWindow((double)iii / fLocalHistorySize);
			double fNormalizeFactor =
				fWindowFactor * fSensitivity /
				(gfDftWindowNorm * fLocalHistorySize);
			DftMatrixCos[i * HistorySize + ii] = (float)(cos(fAngle) * fNormalizeFactor);
			DftMatrixSin[i * HistorySize + ii] = (float)(sin(fAngle) * fNormalizeFactor);
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
		fprintf(stderr, "Failed to enumerate devices. Error code: %i\n", MiniAudioResult);
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
	memset(aSample, 0, array_size(aSample, HistorySize));
	memset(aSampleTemp, 0, array_size(aSampleTemp, HistorySize));
	memset(aOutputHeightOld, 0, array_size(aOutputHeightOld, nBar));
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
			nSampleCollected = min_macro(nSampleCollected + 1, HistorySize);
		}

		// Rotate the buffer

		for (size_t i = iTemp; i < HistorySize; ++i)
			aSample[i - iTemp] = aSampleTemp[i];
		for (size_t i = 0; i < iTemp; ++i)
			aSample[HistorySize - iTemp + i] = aSampleTemp[i];

		// Matrix multiplication

		MatrixMultCpu_Compute(pMatrixMultState, aSample, aOutputHeight);

		// Apply rate filter

		// FIXME: This rate filter is not working very well
		// to hide the noise caused by throwing away samples.
		double fRate
			= 1.0 - exp(-(1.0 / 60.0) * log(1.0 / (1.0 - 0.99)) / fHistorySizeSec); // TODO: calculate FPS

		for (size_t i = 0; i < nBar; ++i) {
			aOutputHeight[i] = fminf(aOutputHeight[i], 1.0f);
			aOutputHeightOld[i] += fRate * (aOutputHeight[i] - aOutputHeightOld[i]);
		}

		// Render
		if (RendererId == RendererId_Legacy)
			RenderLegacy_Render(pRenderState, aOutputHeightOld);
		else if (RendererId == RendererId_Modern)
			RenderModern_Render(pRenderState, aOutputHeightOld);
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
