
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL3/SDL.h>

#include <Utilty/Common.h>

// SDL_Renderer-based

typedef struct {
	size_t WindowW;
	size_t WindowH;
	size_t nBar;
	float fBarWidth;
	float fBarGap;
	uint32_t BackgroundColor;
	uint32_t BarColor;
	bool bConnectedBars;

	SDL_Renderer* pRenderer;
	SDL_FRect* aRectangle;
} render_legacy_state;

void* RenderLegacy_Init(
	SDL_Window* pWindow,
	size_t WindowW,
	size_t WindowH,
	size_t nBar,
	float fBarWidth,
	float fBarGap,
	uint32_t BackgroundColor,
	uint32_t BarColor,
	bool bConnectedBars
) {
	render_legacy_state* pState = malloc(sizeof(*pState));
	if (pState == NULL)
		return NULL;

	pState->WindowW = WindowW;
	pState->WindowH = WindowH;
	pState->nBar = nBar;
	pState->fBarWidth = fBarWidth;
	pState->fBarGap = fBarGap;
	pState->BackgroundColor = BackgroundColor;
	pState->BarColor = BarColor;
	pState->bConnectedBars = bConnectedBars;

	SDL_Renderer* pRenderer = SDL_CreateRenderer(pWindow, NULL);
	if (pRenderer == NULL) {
		free(pState);
		return NULL;
	}
	SDL_SetRenderVSync(pRenderer, 1); // This adds a LOT of latency. TODO: Configurable
	pState->pRenderer = pRenderer;

	if (!bConnectedBars) {
		SDL_FRect* aRectangle = malloc(array_size(aRectangle, nBar));
		if (aRectangle == NULL) {
			SDL_DestroyRenderer(pRenderer);
			free(pState);
			return NULL;
		}
		pState->aRectangle = aRectangle;
	}

	return pState;
}

static float HermiteEval(float Y0, float Y1, float M0, float M1, float T) {
	float T2 = T * T, T3 = T2 * T;
	float H00 = 2.0 * T3 + (-3.0 * T2 + 1.0);
	float H10 = T3 - 2.0 * T2 + T;
	float H01 = -2.0 * T3 + 3.0 * T2;
	float H11 = T3 - T2;
	return Y0 * H00 + M0 * H10 + Y1 * H01 + M1 * H11;
}

static float Tangent(const float* aBarHeight, size_t nBar, size_t i) {
	size_t i0 = i + (i == 0) - 1;
	size_t i1 = i - (i == nBar - 1) + 1;
	return (aBarHeight[i1] - aBarHeight[i0]) * 0.5f;
}

void RenderLegacy_Render(void* pStateVoid, const float* aBarHeight) {
	render_legacy_state* pState = (render_legacy_state*)pStateVoid;
	size_t nBar = pState->nBar;

	SDL_SetRenderDrawColor(
		pState->pRenderer,
		(uint8_t)(pState->BackgroundColor >> 24),
		(uint8_t)(pState->BackgroundColor >> 16),
		(uint8_t)(pState->BackgroundColor >> 8),
		(uint8_t)(pState->BackgroundColor >> 0)
	);
	SDL_RenderClear(pState->pRenderer);
	SDL_SetRenderDrawColor(
		pState->pRenderer,
		(uint8_t)(pState->BarColor >> 24),
		(uint8_t)(pState->BarColor >> 16),
		(uint8_t)(pState->BarColor >> 8),
		(uint8_t)(pState->BarColor >> 0) // FIXME: This alpha param isn't do anything
	);

	if (pState->bConnectedBars) {

		float fScreenWidthInv = 1.0f / (float)pState->WindowW;
		for (size_t i = 0; i < pState->WindowW; ++i) {
			
			float fiBar = (float)i * fScreenWidthInv * (float)(nBar - 1);
			size_t iBar = (size_t)fiBar;
			float LocalDist = fiBar - floor(fiBar);

			float Y0 = aBarHeight[iBar];
			float Y1 = aBarHeight[iBar + 1];
			float M0 = Tangent(aBarHeight, nBar, iBar);
			float M1 = Tangent(aBarHeight, nBar, iBar + 1);
			float Y = HermiteEval(Y0, Y1, M0, M1, LocalDist);

			Y = SDL_clamp(Y, 0.0f, 1.0f);
			SDL_RenderLine(
				pState->pRenderer,
				i,
				pState->WindowH - 1,
				i,
				(1.0 - Y) * (float)pState->WindowH
			);

		}

	} else {

		for (size_t i = 0; i < nBar; ++i) {
			float BarHeight = aBarHeight[i] * (float)pState->WindowH;
			BarHeight = fmaxf(BarHeight, 1.0f);
			pState->aRectangle[i] = (SDL_FRect){
				.x = (float)i * (pState->fBarWidth + pState->fBarGap),
				.y = (float)pState->WindowH - BarHeight,
				.w = pState->fBarWidth,
				.h = BarHeight
			};
		}
		SDL_RenderFillRects(pState->pRenderer, pState->aRectangle, (int)nBar);

	}

	SDL_RenderPresent(pState->pRenderer);
}

void RenderLegacy_Destroy(void* pStateVoid) {
	render_legacy_state* pState = (render_legacy_state*)pStateVoid;
	if (!pState->bConnectedBars) {
		free(pState->aRectangle);
	}
	SDL_DestroyRenderer(pState->pRenderer);
	free(pState);
}
