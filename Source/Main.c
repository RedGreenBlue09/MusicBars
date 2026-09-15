
#define MINIAUDIO_IMPLEMENTATION

#include <math.h>
#include <stdbool.h>
#include <threads.h>

#include <miniaudio.h>
#include <SDL3/SDL.h>

#include "AudioCapture.h"
#include "DftWindow.h"
#include "Render/RendererCommon.h"
#include "Utilty/Arena.h"
#include "Utilty/Atomic.h"
#include "Utilty/Common.h"
#include "Utilty/Time.h"

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

static const double gfPi = 0x1.921FB54442D18p1;

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
void MatrixMultCpu_Compute(void* pStateVoid, const float* aSample, float* aOutput);
void MatrixMultCpu_Destroy(void* pStateVoid);

int main(int argc, char** argv) {

	int Result = 0;

	// TODO: Read from config

	const size_t nBar = 80;
	const char* sCaptureDevice = "(desktop)";

	// Trap Nation
	//const double fFreqMin = 25;
	//const double fFreqMax = 140;
	//const size_t HistorySizeMs = 150;
	const double fFreqMin = 25;
	const double fFreqMax = 250;
	const double fHistorySizeMs = 150.0;
	const size_t DesiredSampleRate = 0;
	const double fSensitivity = 1.0;
	const bool bLogScale = false;
	const bool bUniformMainLobe = true;
	const bool bEnergyEstimation = true;
	const dft_window_id iWindowFunction = 5;

	// Stress test
	//const size_t nBar = 1200;
	//const float fBarGap = 0.0f;
	//const float fBarWidth = 1.0f;
	const float fBarGap = 5.0f;
	const float fBarWidth = 10.0f;
	const uint32_t BackgroundColor = 0x0000007F;
	const uint32_t BarColor = 0xFFFFFF7F;
	const float fMinimumBarHeight = 1.0;
	const float fVerticalOffset = 0.0;
	const bool bConnectedBars = true;
	const bool bAlwaysOnTop = true;
	const bool bTransparentWindow = true;
	const bool bVsync = true;

	// Create SDL window

	const double fnBar = (double)nBar;
	const size_t WindowW =
		(size_t)(fnBar * fBarWidth + (fnBar - 1.0) * fBarGap);
	const size_t WindowH = 400;

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		fprintf(stderr, "Unable to initialize SDL: %s\n", SDL_GetError());
		Result = -1;
		goto End;
	}
	SDL_Window* pWindow = SDL_CreateWindow(
		"MusicBars",
		(int)WindowW, // TODO: set size to 1 and let the renderer cook
		(int)WindowH,
		SDL_WINDOW_BORDERLESS | (bTransparentWindow ? SDL_WINDOW_TRANSPARENT : 0)
	); // DX12 GPU renderer does not work with TRANSPARENT yet. Waiting on SDL...
	if (!pWindow) {
		fprintf(stderr, "Unable to create the window: %s", SDL_GetError());
		Result = -1;
		goto CleanupSdl;
	}
	if (!SDL_SetWindowHitTest(pWindow, HitTestCallback, NULL)) {
		fprintf(stderr, "Unable to set window hit test callback: %s", SDL_GetError());
	}
	if (!SDL_SetWindowAlwaysOnTop(pWindow, bAlwaysOnTop)) {
		fprintf(stderr, "Warning: Cannot bring the window on top.%s", SDL_GetError());
	}

	// Create renderer

	void* pRenderState;
	renderer_id RendererId;
	do {
		renderer_config RendererConfig = {
			WindowW,
			WindowH,
			nBar,
			fBarWidth,
			fBarGap,
			BackgroundColor,
			BarColor,
			fMinimumBarHeight,
			fVerticalOffset,
			bConnectedBars,
			bVsync,
		};
		pRenderState = RenderModern_Init(pWindow, &RendererConfig);
		if (pRenderState != NULL) {
			RendererId = RendererId_Modern;
			break;
		}

		pRenderState = RenderLegacy_Init(pWindow, &RendererConfig);
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
	const size_t SampleRate =
		(DesiredSampleRate != 0) ?
		DesiredSampleRate :
		(size_t)(round(fFreqMax * 2.4));
	const double fSampleRate = (double)SampleRate;
	const size_t HistorySize = (size_t)round(fHistorySizeMs / 1000.0 * fSampleRate);
	const double fHistorySize = (double)HistorySize;
	const double fHistorySizeSec = fHistorySize / fSampleRate;

	float* aSample;
	float* aOutputHeight;
	float* aOutputHeightOld;
	float* DftMatrixCos;
	float* DftMatrixSin;
	void* pArena = Arena_Create(
		array_size(aSample, HistorySize) +
		array_size(aOutputHeight, nBar) +
		array_size(aOutputHeightOld, nBar) +
		array_size(DftMatrixCos, nBar * HistorySize) +
		array_size(DftMatrixSin, nBar * HistorySize)
	);
	if (pArena == NULL) {
		fprintf(stderr, "Unable to allocate DFT matrix.\n");
		Result = -1;
		goto CleanupRenderer;
	}
	size_t ArenaCounter = 0;
	aSample          = Arena_Push(pArena, &ArenaCounter, array_size(aSample, HistorySize));
	aOutputHeight    = Arena_Push(pArena, &ArenaCounter, array_size(aOutputHeight, nBar));
	aOutputHeightOld = Arena_Push(pArena, &ArenaCounter, array_size(aOutputHeightOld, nBar));
	DftMatrixCos     = Arena_Push(pArena, &ArenaCounter, array_size(DftMatrixCos, nBar * HistorySize));
	DftMatrixSin     = Arena_Push(pArena, &ArenaCounter, array_size(DftMatrixSin, nBar * HistorySize));

	dft_window_info DftWindowInfo = gaDftWindowInfo[iWindowFunction];

	for (size_t i = 0; i < nBar; ++i) {
		// MainLobe = gfDftWindowMainLobe / fHistorySizeSec
		// If log scale, we scale it accordingly.
		// But in both log & linear scale, the main lobe might be
		// smaller than the frequency range covered by a single bar.
		// Largest range covered by a bar:
		// Log scale: fFreqMax * (1 - 1 / (fFreqMax / fFreqMin) ^ (1 / (nBar - 1))) 
		// Linear scale: (fFreqMax - fFreqMin) / nBar
		// In addition, we want each main lobe to overlap so that their area
		// covers the whole spectrum,
		// preventing high-frequency bars from receiving no energy.
		// Lastly, if HistorySize is too small, there might be aliasing.
		// Upsampling is a workaround.

		double fFreq;
		if (bLogScale)
			fFreq = fFreqMin * pow(fFreqMax / fFreqMin, (double)i / (fnBar - 1.0));
		else
			fFreq = fFreqMin + (fFreqMax - fFreqMin) * ((double)i / (fnBar - 1.0));

		double fFrequencyGap; // Bandwith
		if (bLogScale)
			fFrequencyGap = fFreq * (pow(fFreqMax / fFreqMin, 1.0 / (fnBar - 1.0)) - 1.0);
		else
			fFrequencyGap = (fFreqMax - fFreqMin) / fnBar;
		size_t MaxLocalHistorySize =
			(size_t)(round(fSampleRate * DftWindowInfo.MainLobeArea / fFrequencyGap));

		size_t LocalHistorySize;
		if (bLogScale && bUniformMainLobe)
			LocalHistorySize = (size_t)(round(fHistorySize * (fFreqMin / fFreq)));
		else
			LocalHistorySize = HistorySize;

		LocalHistorySize = min_macro(LocalHistorySize, MaxLocalHistorySize);
		double fLocalHistorySize = (double)LocalHistorySize;

		// TODO: Dynamic vector size to reduce resource consumption
		for (size_t ii = 0; ii < HistorySize - LocalHistorySize; ++ii) {
			DftMatrixCos[i * HistorySize + ii] = 0.0f;
			DftMatrixSin[i * HistorySize + ii] = 0.0f;
		}
		for (size_t ii = HistorySize - LocalHistorySize; ii < HistorySize; ++ii) {
			size_t iii = ii - (HistorySize - LocalHistorySize);
			double fAngle = 2.0 * gfPi * fFreq * ((double)iii + 0.5) / fSampleRate;
			double fWindowFactor =
				DftWindowInfo.Compute(((double)iii + 0.5) / fLocalHistorySize);
			double fNormalizeFactor =
				fWindowFactor * fSensitivity /
				(DftWindowInfo.Area * fLocalHistorySize);
			if (bEnergyEstimation)
				fNormalizeFactor *= (fFrequencyGap) * (fLocalHistorySize / fHistorySize);
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
		goto CleanupArena;
	}

	// Initialize audio capture

	void* pAudioCaptureState =
		AudioCapture_Init(SampleRate, HistorySize, sCaptureDevice);
	if (pAudioCaptureState == NULL) {
		fprintf(stderr, "Unable to initialize audio capture.\n");
		Result = -1;
		goto CleanupMatrixMult;
	}

	// Render thread

	size_t iTemp = 0;
	memset(aOutputHeightOld, 0, array_size(aOutputHeightOld, nBar));
	uint64_t Second = clock64_resolution();
	double fSecond = (double)Second;
	uint64_t TimeLastFrame = 0;
	while (true) {
		SDL_Event Event;
		while (SDL_PollEvent(&Event)) {
			if (Event.type == SDL_EVENT_QUIT)
				goto RenderEnd;
			if (Event.type == SDL_EVENT_KEY_DOWN && Event.key.key == SDLK_ESCAPE)
				goto RenderEnd;
		}

		// Get newest samples

		AudioCapture_GetSamples(pAudioCaptureState, aSample);

		// Matrix multiplication

		MatrixMultCpu_Compute(pMatrixMultState, aSample, aOutputHeight);
		
		// Apply rate filter

		// FIXME: This rate filter is not working very well
		// to hide the noise caused by throwing away samples.
		// A way to fix this is to use frequency domain averaging
		// with weights that look like the main lobe.
		uint64_t TimeCurrent = clock64();
		float fRate;
		if (TimeLastFrame == 0)
			fRate = 1.0f;
		else
			fRate = 1.0 - exp(
				-(double)(TimeCurrent - TimeLastFrame) / fSecond *
				log(1.0 / (1.0 - 0.99)) /
				fHistorySizeSec
			);

		for (size_t i = 0; i < nBar; ++i) {
			aOutputHeightOld[i] += fRate * (aOutputHeight[i] - aOutputHeightOld[i]);
		}

		// Render
		if (RendererId == RendererId_Legacy)
			RenderLegacy_Render(pRenderState, aOutputHeightOld);
		else if (RendererId == RendererId_Modern)
			RenderModern_Render(pRenderState, aOutputHeightOld);

		TimeLastFrame = TimeCurrent;
	}
	RenderEnd:

	// Cleanup

	CleanupAudioCapture:
	AudioCapture_Uninit(pAudioCaptureState);

	CleanupMatrixMult:
	MatrixMultCpu_Destroy(pMatrixMultState);

	CleanupArena:
	Arena_Destroy(pArena);

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
