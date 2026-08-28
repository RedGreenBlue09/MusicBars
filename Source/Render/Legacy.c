
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL3/SDL.h>

#include <Common.h>

#include <stdio.h>
// SDL_Renderer-based

typedef struct {
	size_t WindowW;
	size_t WindowH;
	size_t nBar;
	size_t BarWidth;
	size_t BarGap;
	uint32_t BackgroundColor;
	uint32_t BarColor;
	SDL_Renderer* pRenderer;
	SDL_FRect* pRects;
} render_legacy_state;

void* RenderLegacy_Init(
	SDL_Window* pWindow,
	size_t WindowW,
	size_t WindowH,
	size_t nBar,
	size_t BarWidth,
	size_t BarGap,
	uint32_t BackgroundColor,
	uint32_t BarColor
) {
	render_legacy_state* pState = malloc(sizeof(*pState));
	if (pState == NULL)
		return NULL;

	pState->WindowW = WindowW;
	pState->WindowH = WindowH;
	pState->nBar = nBar;
	pState->BarWidth = BarWidth;
	pState->BarGap = BarGap;
	pState->BackgroundColor = BackgroundColor;
	pState->BarColor = BarColor;

	SDL_Renderer* pRenderer = SDL_CreateRenderer(pWindow, NULL);
	if (pRenderer == NULL) {
		free(pState);
		return NULL;
	}
	SDL_SetRenderVSync(pRenderer, 1); // This adds a LOT of latency. TODO: Configurable
	pState->pRenderer = pRenderer;

	SDL_FRect* pRects = malloc(nBar * sizeof(SDL_FRect));
	if (pRects == NULL) {
		SDL_DestroyRenderer(pRenderer);
		free(pState);
		return NULL;
	}
	pState->pRects = pRects;

	return pState;
}

void RenderLegacy_Render(void* pStateIn, const float* aOutput) {
	render_legacy_state* pState = (render_legacy_state*)pStateIn;
	SDL_SetRenderDrawColor(
		pState->pRenderer,
		(uint8_t)(pState->BackgroundColor >> 24),
		(uint8_t)(pState->BackgroundColor >> 16),
		(uint8_t)(pState->BackgroundColor >> 8),
		(uint8_t)(pState->BackgroundColor >> 0)
	);
	SDL_RenderClear(pState->pRenderer);
	for (size_t i = 0; i < pState->nBar; ++i) {
		float BarHeight = aOutput[i] * (float)pState->WindowH;
		BarHeight = fmaxf(BarHeight, 1.0f);
		pState->pRects[i] = (SDL_FRect){
			.x = (float)(i * (pState->BarWidth + pState->BarGap)),
			.y = (float)pState->WindowH - BarHeight,
			.w = (float)pState->BarWidth,
			.h = BarHeight
		};
	}
	SDL_SetRenderDrawColor(
		pState->pRenderer,
		(uint8_t)(pState->BarColor >> 24),
		(uint8_t)(pState->BarColor >> 16),
		(uint8_t)(pState->BarColor >> 8),
		(uint8_t)(pState->BarColor >> 0) // FIXME: This alpha param isn't do anything
	);
	SDL_RenderFillRects(pState->pRenderer, pState->pRects, (int)pState->nBar);
	SDL_RenderPresent(pState->pRenderer);
}

void RenderLegacy_Destroy(void* pStateIn) {
	render_legacy_state* pState = (render_legacy_state*)pStateIn;
	free(pState->pRects);
	SDL_DestroyRenderer(pState->pRenderer);
	free(pState);
}
