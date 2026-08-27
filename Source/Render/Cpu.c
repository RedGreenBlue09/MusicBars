
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <SDL3/SDL.h>

#include <Common.h>

// SDL_Renderer-based

typedef struct {
	size_t WindowW;
	size_t WindowH;
	size_t nBar;
	size_t BarWidth;
	size_t BarGap;
	SDL_Renderer* pRenderer;
	SDL_Texture* pTexture;
	SDL_FRect* pRects;
} render_cpu_state;

void* RenderCpu_Init(
	SDL_Window* pWindow,
	size_t WindowW,
	size_t WindowH,
	size_t nBar,
	size_t BarWidth,
	size_t BarGap
) {
	render_cpu_state* pState = malloc(sizeof(render_cpu_state));
	if (pState == NULL)
		return NULL;

	pState->WindowW = WindowW;
	pState->WindowH = WindowH;
	pState->nBar = nBar;
	pState->BarWidth = BarWidth;
	pState->BarGap = BarGap;

	SDL_Renderer* pRenderer = SDL_CreateRenderer(pWindow, NULL);
	if (pRenderer == NULL) {
		free(pState);
		return NULL;
	}
	SDL_SetRenderVSync(pRenderer, true);
	pState->pRenderer = pRenderer;
	
	// FIXME: Unused
	SDL_Texture* pTexture = SDL_CreateTexture(
		pRenderer,
		SDL_PIXELFORMAT_RGBA8888,
		SDL_TEXTUREACCESS_STREAMING,
		(int)WindowW,
		(int)WindowH
	);
	if (pTexture == NULL) {
		SDL_DestroyRenderer(pRenderer);
		free(pState);
		return NULL;
	}
	SDL_SetTextureBlendMode(pTexture, SDL_BLENDMODE_BLEND);
	pState->pTexture = pTexture;

	SDL_FRect* pRects = malloc(nBar * sizeof(SDL_FRect));
	if (pRects == NULL) {
		SDL_DestroyTexture(pTexture);
		SDL_DestroyRenderer(pRenderer);
		free(pState);
		return NULL;
	}
	pState->pRects = pRects;

	return pState;
}

void RenderCpu_Render(void* pStateIn, const float* aOutput) {
	render_cpu_state* pState = (render_cpu_state*)pStateIn;
	SDL_SetRenderDrawColor(pState->pRenderer, 0, 0, 0, 0);
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
	SDL_SetRenderDrawColor(pState->pRenderer, 255, 255, 255, 255);
	SDL_RenderFillRects(pState->pRenderer, pState->pRects, (int)pState->nBar);
	SDL_RenderPresent(pState->pRenderer);
}

void RenderCpu_Destroy(void* pStateIn) {
	render_cpu_state* pState = (render_cpu_state*)pStateIn;
	free(pState->pRects);
	SDL_DestroyTexture(pState->pTexture);
	SDL_DestroyRenderer(pState->pRenderer);
	free(pState);
}
