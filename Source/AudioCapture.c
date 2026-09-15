
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

#include <miniaudio.h>

#include "Utilty/Arena.h"
#include "Utilty/Atomic.h"
#include "Utilty/Common.h"
#include "Utilty/Machine.h"

#include "AudioCapture.h"

#define CACHE_LINE_SIZE 128

typedef struct {
	float* aSlot;
	size_t Capacity;
	alignas(CACHE_LINE_SIZE) atomic size_t iWrite;
	alignas(CACHE_LINE_SIZE) size_t iWriteCached;
	alignas(CACHE_LINE_SIZE) atomic size_t iRead;
	alignas(CACHE_LINE_SIZE) size_t iReadCached;
} audio_queue;

// Based on https://github.com/rigtorp/SPSCQueue

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

static ma_device_id* FindAudioDeviceByName(
	const char* sCaptureDevice,
	ma_device_info* aCaptureInfo,
	ma_uint32 nCapture
) {
#ifdef OS_LINUX
	if (strcmp(sCaptureDevice, "(desktop)") == 0) {
		for (ma_uint32 i = 0; i < nCapture; i++) {
			if (strcasestr(aCaptureInfo[i].name, "monitor") != NULL) {
				printf("Using audio capture device: %s\n", aCaptureInfo[i].name);
				return &aCaptureInfo[i].id;
			}
		}
		return NULL;
	}
#endif

	for (ma_uint32 i = 0; i < nCapture; i++) {
		if (strcmp(aCaptureInfo[i].name, sCaptureDevice) == 0) {
			return &aCaptureInfo[i].id;
		}
	}

	return NULL;
}

typedef struct {
	ma_device AudioDevice;
	audio_queue Queue;
	size_t HistorySize;
	size_t iTemp;
	float* aSampleTemp;
} audio_capture_state;

void* AudioCapture_Init(
	size_t SampleRate,
	size_t HistorySize,
	const char* sCaptureDevice
) {

	audio_capture_state* pState;
	void* pArena = Arena_Create(
		sizeof(*pState) + array_size(pState->aSampleTemp, HistorySize)
	);
	if (pArena == NULL) {
		fprintf(stderr, "Failed to allocate audio capture state.\n");
		goto CleanupEnd;
	}
	size_t ArenaCounter = 0;
	pState = Arena_Push(pArena, &ArenaCounter, sizeof(*pState));
	pState->aSampleTemp = Arena_Push(pArena, &ArenaCounter, array_size(pState->aSampleTemp, HistorySize));
	memset(pState->aSampleTemp, 0, array_size(pState->aSampleTemp, HistorySize));

	// 1s of buffer to minimize the chance of dropping samples
	if (!AudioQueue_Create(&pState->Queue, SampleRate)) {
		fprintf(stderr, "Unable to initialize audio queue.\n");
		goto CleanupState;
	}

	ma_result MiniAudioResult;
	ma_device_id* pAudioDeviceId = NULL;
	bool bUseLoopback = false;
	ma_context AudioContext;

#ifdef OS_WINDOWS
	if (strcmp(sCaptureDevice, "(desktop)") == 0)
		bUseLoopback = true;
#endif

	if (!bUseLoopback) {
		MiniAudioResult = ma_context_init(NULL, 0, NULL, &AudioContext);
		if (MiniAudioResult != MA_SUCCESS) {
			fprintf(stderr, "Failed to initialize audio context. Error code: %i\n", MiniAudioResult);
			goto CleanupAudioQueue;
		}

		ma_device_info* aPlaybackInfo;
		ma_uint32 nPlayback;
		ma_device_info* aCaptureInfo;
		ma_uint32 nCapture;

		MiniAudioResult = ma_context_get_devices(&AudioContext, &aPlaybackInfo, &nPlayback, &aCaptureInfo, &nCapture);
		if (MiniAudioResult != MA_SUCCESS) {
			fprintf(stderr, "Failed to enumerate devices. Error code: %i\n", MiniAudioResult);
			goto CleanupAudioContext;
		}

		pAudioDeviceId = FindAudioDeviceByName(sCaptureDevice, aCaptureInfo, nCapture);
		if (pAudioDeviceId == NULL) {
			fprintf(
				stderr,
				"Cannot find the audio device '%s'. Error code: %i\n",
				sCaptureDevice,
				MiniAudioResult
			);
			goto CleanupAudioContext;
		}

		ma_context_uninit(&AudioContext);
	}

	// Create and configure device
	ma_device_config MiniAudioConfig = ma_device_config_init(
		bUseLoopback ? ma_device_type_loopback : ma_device_type_capture
	);

	MiniAudioConfig.capture.pDeviceID = pAudioDeviceId;
	MiniAudioConfig.capture.format = ma_format_f32;
	MiniAudioConfig.capture.channels = 1;
	MiniAudioConfig.sampleRate = SampleRate;
	MiniAudioConfig.dataCallback = ReceiveAudio;
	MiniAudioConfig.pUserData = &pState->Queue;
	MiniAudioConfig.noFixedSizedCallback = true;

	MiniAudioResult = ma_device_init(NULL, &MiniAudioConfig, &pState->AudioDevice);
	if (MiniAudioResult != MA_SUCCESS) {
		fprintf(stderr, "Unable to initialize audio device. Error code: %i\n", MiniAudioResult);
		goto CleanupAudioQueue;
	}

	MiniAudioResult = ma_device_start(&pState->AudioDevice);
	if (MiniAudioResult != MA_SUCCESS) {
		fprintf(stderr, "Unable to start audio device. Error code: %i\n", MiniAudioResult);
		goto CleanupAudioDevice;
	}

	pState->HistorySize = HistorySize;
	pState->iTemp = 0;
	return pState;

	CleanupAudioContext:
	if (!bUseLoopback) {
		ma_context_uninit(&AudioContext);
	}

	CleanupAudioDevice:
	ma_device_uninit(&pState->AudioDevice);

	CleanupAudioQueue:
	AudioQueue_Destroy(&pState->Queue);

	CleanupState:
	Arena_Destroy(pArena);

	CleanupEnd:
	return NULL;
}

void AudioCapture_GetSamples(void* pState, float* aSample) {
	audio_capture_state* pAudioState = (audio_capture_state*)pState;
	audio_queue* pQueue = &pAudioState->Queue;
	size_t HistorySize = pAudioState->HistorySize;
	size_t iTemp = pAudioState->iTemp;
	float* aSampleTemp = pAudioState->aSampleTemp;

	float fSample;
	while (AudioQueue_TryPop(pQueue, &fSample)) {
		aSampleTemp[iTemp++] = fSample;
		iTemp = (iTemp >= HistorySize) ? 0 : iTemp;
	}

	// Rotate the buffer

	for (size_t i = iTemp; i < HistorySize; ++i)
		aSample[i - iTemp] = aSampleTemp[i];
	for (size_t i = 0; i < iTemp; ++i)
		aSample[HistorySize - iTemp + i] = aSampleTemp[i];
	pAudioState->iTemp = iTemp;
}

void AudioCapture_Uninit(void* pState) {
	audio_capture_state* pAudioState = (audio_capture_state*)pState;
	ma_device_uninit(&pAudioState->AudioDevice);
	AudioQueue_Destroy(&pAudioState->Queue);
}