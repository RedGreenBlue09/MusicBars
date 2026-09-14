#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <SDL3/SDL.h>

// Renderer configuration
typedef struct {
	size_t WindowW;
	size_t WindowH;
	size_t nBar;
	float fBarWidth;
	float fBarGap;
	uint32_t BackgroundColor;
	uint32_t BarColor;
	float fMinimumBarHeight;
	float fVerticalOffset;
	bool bConnectedBars;
	bool bVsync;
} renderer_config;

// Renderer identifier
typedef enum {
	RendererId_Legacy,
	RendererId_Modern
} renderer_id;

// Legacy renderer functions
void* RenderLegacy_Init(
	SDL_Window* pWindow,
	renderer_config* pConfig
);

void RenderLegacy_Render(
	void* pStateVoid,
	const float* aBarHeight
);

void RenderLegacy_Destroy(void* pStateVoid);

// Modern renderer functions
void* RenderModern_Init(
	SDL_Window* pWindow,
	renderer_config* pConfig
);

void RenderModern_Render(
	void* pStateVoid,
	const float* aBarHeight
);

void RenderModern_Destroy(void* pStateVoid);
