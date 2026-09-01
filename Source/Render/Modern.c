
#include <SDL3/SDL.h>

#include <stddef.h>
#include <stdint.h>

#if MUSICBARS_ENABLE_MODERN_SHADERS

#include <math.h>
#include <stdalign.h>
#include <stdio.h>

#include <SDL3_shadercross/SDL_shadercross.h>

#include <Utilty/Common.h>

typedef struct {
	alignas(256) float BarColor[4];
	float BarWidth; // Relative ratio to screen size
	float BarGap; // Relative ratio to screen size
} cbv_parameter;

static const char VertexShaderString[] =
	"struct vertex_output {"
	"    float4 Position : SV_Position;"
	"    float4 Color    : COLOR0;"
	"};"
	""
	"StructuredBuffer<float4> aBarHeight : register(t0, space0);"
	""
	"cbuffer Params : register(b0, space1) {"
	"    float4 BarColor;"
	"    float BarWidth;"
	"    float BarGap;"
	"};"
	""
	"vertex_output VertexMain(uint VertexId : SV_VertexID, uint InstanceId : SV_InstanceID) {"
	"    float BarHeight = aBarHeight[InstanceId / 4][InstanceId % 4];"
	"    "
	"    float2 TopLeft = {(BarWidth + BarGap) * InstanceId, BarHeight};"
	"    float2 TopRight = {(BarWidth + BarGap) * InstanceId + BarWidth, BarHeight};"
	"    float2 BottomLeft = {(BarWidth + BarGap) * InstanceId, 0};"
	"    float2 BottomRight = {(BarWidth + BarGap) * InstanceId + BarWidth, 0};"
	"    static const float2 aVertexLookup[6] = {"
	"        TopLeft,"
	"        TopRight,"
	"        BottomLeft,"
	"        BottomLeft,"
	"        TopRight,"
	"        BottomRight"
	"    };"
	"    float2 Ndc = aVertexLookup[VertexId % 6];"
	"    Ndc.x = Ndc.x * 2.0 - 1.0;"
	"    Ndc.y = Ndc.y * 2.0 - 1.0;"
	""
	"    vertex_output Output = {float4(Ndc, 0.0, 1.0), BarColor};"
	"    return Output;"
	"}"
	"";

static const char FragmentShaderString[] =
	"struct fragment_input {"
	"    float4 Position : SV_Position;"
	"    float4 Color    : COLOR0;"
	"};"
	""
	"float4 FragmentMain(fragment_input Input) : SV_Target0 {"
	"    return Input.Color;"
	"}"
	"";

// Graphics shader
static SDL_GPUShader* CompileShaderFromHlsl(
	SDL_GPUDevice* pDevice,
	const char* ShaderString,
	const char* EntryPointString,
	SDL_ShaderCross_ShaderStage ShaderStage
) {
	SDL_ShaderCross_HLSL_Info ShaderInfo = {
		.source = ShaderString,
		.entrypoint = EntryPointString,
		.include_dir = NULL,
		.defines = NULL,
		.shader_stage = ShaderStage,
		.props = 0,
	};

	size_t ShaderSpirvSize;
	void* pShaderSpirv =
		SDL_ShaderCross_CompileSPIRVFromHLSL(&ShaderInfo, &ShaderSpirvSize);
	if (pShaderSpirv == NULL)
		return NULL;

	SDL_ShaderCross_GraphicsShaderMetadata* pShaderMetadata =
		SDL_ShaderCross_ReflectGraphicsSPIRV(pShaderSpirv, ShaderSpirvSize, 0);
	SDL_ShaderCross_SPIRV_Info ShaderSpirvInfo = {
		.bytecode = pShaderSpirv,
		.bytecode_size = ShaderSpirvSize,
		.entrypoint = ShaderInfo.entrypoint,
		.shader_stage = ShaderInfo.shader_stage,
		.props = ShaderInfo.props,
	};
	SDL_GPUShader* pShader = SDL_ShaderCross_CompileGraphicsShaderFromSPIRV(
		pDevice,
		&ShaderSpirvInfo,
		&pShaderMetadata->resource_info,
		ShaderInfo.props
	);
	SDL_free(pShaderSpirv);
	return pShader;
}

// SDL_Renderer-based

typedef struct {
	size_t WindowW;
	size_t WindowH;
	size_t nBar;
	float fBarWidth;
	float fBarGap;
	uint32_t BackgroundColor;
	uint32_t BarColor;

	SDL_Window* pWindow;
	SDL_GPUDevice* pDevice;
	SDL_GPUGraphicsPipeline* pPipeline;
	SDL_GPUBuffer* pBarHeightBuffer;
	SDL_GPUTransferBuffer* pTransferBuffer;
	SDL_GPUBuffer* pVertexBuffer;
} render_modern_state;

void* RenderModern_Init(
	SDL_Window* pWindow,
	size_t WindowW,
	size_t WindowH,
	size_t nBar,
	float fBarWidth,
	float fBarGap,
	uint32_t BackgroundColor,
	uint32_t BarColor
) {
	render_modern_state* pState = malloc(sizeof(*pState));
	if (pState == NULL)
		goto CleanupEnd;

	pState->pWindow = pWindow;
	pState->WindowW = WindowW;
	pState->WindowH = WindowH;

	pState->nBar = nBar;
	pState->fBarWidth = fBarWidth;
	pState->fBarGap = fBarGap;
	pState->BackgroundColor = BackgroundColor;
	pState->BarColor = BarColor;

	// GPU device

	// temp
	SDL_PropertiesID Props = SDL_CreateProperties();
	//SDL_SetBooleanProperty(Props, SDL_PROP_GPU_DEVICE_CREATE_PREFERLOWPOWER_BOOLEAN, true);
	SDL_SetBooleanProperty(Props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
	SDL_SetBooleanProperty(Props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN, true);
	SDL_SetBooleanProperty(Props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN, true);
	#if NDEBUG
	SDL_SetBooleanProperty(Props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, false);
	#else
	SDL_SetBooleanProperty(Props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, true);
	#endif
	pState->pDevice = SDL_CreateGPUDeviceWithProperties(Props);
	SDL_DestroyProperties(Props);

	if (pState->pDevice == NULL)
		goto CleanupState;

	if (!SDL_ClaimWindowForGPUDevice(pState->pDevice, pWindow))
		goto CleanupGpuDevice;

	SDL_GPUPresentMode PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
	if (SDL_WindowSupportsGPUPresentMode(pState->pDevice, pWindow, SDL_GPU_PRESENTMODE_MAILBOX))
		PresentMode = SDL_GPU_PRESENTMODE_MAILBOX;
	SDL_SetGPUSwapchainParameters(pState->pDevice, pWindow, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, PresentMode);

	SDL_SetGPUAllowedFramesInFlight(pState->pDevice, 1);

	// Shaders

	if (!SDL_ShaderCross_Init())
		goto CleanupWindowBind;

	SDL_GPUShader* pVertexShader = CompileShaderFromHlsl(
		pState->pDevice,
		VertexShaderString,
		"VertexMain",
		SDL_SHADERCROSS_SHADERSTAGE_VERTEX
	);
	if (pVertexShader == NULL)
		goto CleanupShaderCross;

	SDL_GPUShader* pFragmentShader = CompileShaderFromHlsl(
		pState->pDevice,
		FragmentShaderString,
		"FragmentMain",
		SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT
	);
	if (pFragmentShader == NULL)
		goto CleanupVertexShader;

	// Pipeline

	SDL_GPUColorTargetDescription ColorTarget = {
		.format = SDL_GetGPUSwapchainTextureFormat(pState->pDevice, pWindow),
	};

	// Dummy vertex buffer to avoid assert error
	SDL_GPUVertexBufferDescription VertexBufferDesc = {
		.slot = 0,
		.pitch = 16,
		.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
	};

	SDL_GPUGraphicsPipelineCreateInfo PipelineInfo = {
		.vertex_shader = pVertexShader,
		.fragment_shader = pFragmentShader,
		.vertex_input_state = {
			.vertex_buffer_descriptions = &VertexBufferDesc,
			.num_vertex_buffers = 1,
			.vertex_attributes = NULL,
			.num_vertex_attributes = 0,
		},
		.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
		.target_info = {
			.color_target_descriptions = &ColorTarget,
			.num_color_targets = 1,
		},
	};
	pState->pPipeline = SDL_CreateGPUGraphicsPipeline(
		pState->pDevice,
		&PipelineInfo
	);
	if (pState->pPipeline == NULL)
		goto CleanupFragmentShader;

	// Bar height buffer

	pState->pBarHeightBuffer = SDL_CreateGPUBuffer(
		pState->pDevice,
		&(SDL_GPUBufferCreateInfo){
			.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
			.size = div_roundup(nBar * sizeof(float), 16) * 16,
		}
	);
	if (pState->pBarHeightBuffer == NULL)
		goto CleanupGraphicsPipeline;

	pState->pTransferBuffer = SDL_CreateGPUTransferBuffer(
		pState->pDevice,
		&(SDL_GPUTransferBufferCreateInfo){
			.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
			.size = div_roundup(nBar * sizeof(float), 16) * 16,
		}
	);
	if (pState->pTransferBuffer == NULL)
		goto CleanupBarHeightBuffer;

	// Vertex buffer

	pState->pVertexBuffer = SDL_CreateGPUBuffer(
		pState->pDevice,
		&(SDL_GPUBufferCreateInfo){
			.usage = SDL_GPU_BUFFERUSAGE_VERTEX,
			.size = 16,
		}
	);
	if (pState->pVertexBuffer == NULL)
		goto CleanupTransferBuffer;

	// Success

	SDL_ReleaseGPUShader(pState->pDevice, pFragmentShader);
	SDL_ReleaseGPUShader(pState->pDevice, pVertexShader);
	SDL_ShaderCross_Quit();
	return pState;

	// Fail

	CleanupTransferBuffer:
	SDL_ReleaseGPUTransferBuffer(pState->pDevice, pState->pTransferBuffer);

	CleanupBarHeightBuffer:
	SDL_ReleaseGPUBuffer(pState->pDevice, pState->pBarHeightBuffer);

	CleanupGraphicsPipeline:
	SDL_ReleaseGPUGraphicsPipeline(pState->pDevice, pState->pPipeline);

	CleanupFragmentShader:
	SDL_ReleaseGPUShader(pState->pDevice, pFragmentShader);

	CleanupVertexShader:
	SDL_ReleaseGPUShader(pState->pDevice, pVertexShader);

	CleanupWindowBind:
	SDL_ReleaseWindowFromGPUDevice(pState->pDevice, pWindow);

	CleanupShaderCross:
	SDL_ShaderCross_Quit();

	CleanupGpuDevice:
	SDL_DestroyGPUDevice(pState->pDevice);

	CleanupState:
	free(pState);

	CleanupEnd:
	return NULL;
}

void RenderModern_Render(void* pStateVoid, const float* aOutput) {
	render_modern_state* pState = (render_modern_state*)pStateVoid;

	// Copy bar height

	float* aBarHeight =
		(float*)SDL_MapGPUTransferBuffer(pState->pDevice, pState->pTransferBuffer, true);

	size_t BarHeightBufferSize = array_size(aOutput, pState->nBar);
	size_t BarHeightBufferGpuSize = div_roundup(BarHeightBufferSize, 16) * 16;
	for (size_t i = 0; i < pState->nBar; ++i)
		aBarHeight[i] = fmaxf(aOutput[i], 1.0f / (float)pState->WindowH);
	memset(&aBarHeight[pState->nBar], BarHeightBufferGpuSize - BarHeightBufferSize, 0);

	SDL_UnmapGPUTransferBuffer(pState->pDevice, pState->pTransferBuffer);

	SDL_GPUCommandBuffer* pCommandBuffer =
		SDL_AcquireGPUCommandBuffer(pState->pDevice);

	SDL_GPUCopyPass* pCopyPass = SDL_BeginGPUCopyPass(pCommandBuffer);
	SDL_UploadToGPUBuffer(pCopyPass,
		&(SDL_GPUTransferBufferLocation){.
			transfer_buffer = pState->pTransferBuffer,
			.offset = 0
		},
		&(SDL_GPUBufferRegion) {
			.buffer = pState->pBarHeightBuffer,
			.offset = 0,
			.size = BarHeightBufferGpuSize
		},
		true
	);
	SDL_EndGPUCopyPass(pCopyPass);

	//

	uint32_t TextureW;
	uint32_t TextureH;
	SDL_GPUTexture* SwapchainTexture;
	SDL_WaitAndAcquireGPUSwapchainTexture(
		pCommandBuffer,
		pState->pWindow,
		&SwapchainTexture,
		&TextureW,
		&TextureH
	);

	if (SwapchainTexture) {
		float fBarColor[4] = {
			(float)(uint8_t)(pState->BarColor >> 24) / 255.0f,
			(float)(uint8_t)(pState->BarColor >> 16) / 255.0f,
			(float)(uint8_t)(pState->BarColor >> 8) / 255.0f,
			(float)(uint8_t)(pState->BarColor >> 0) / 255.0f
		};
		float fBackgroundColor[4] = {
			(float)(uint8_t)(pState->BackgroundColor >> 24) / 255.0f,
			(float)(uint8_t)(pState->BackgroundColor >> 16) / 255.0f,
			(float)(uint8_t)(pState->BackgroundColor >> 8) / 255.0f,
			(float)(uint8_t)(pState->BackgroundColor >> 0) / 255.0f
		};

		SDL_GPUColorTargetInfo ColorInfo = {
			.texture = SwapchainTexture,
			.clear_color = {
				fBackgroundColor[0],
				fBackgroundColor[1],
				fBackgroundColor[2],
				fBackgroundColor[3]
			},
			.load_op = SDL_GPU_LOADOP_CLEAR,
			.store_op = SDL_GPU_STOREOP_STORE,
		};
		SDL_GPURenderPass* pRenderPass =
			SDL_BeginGPURenderPass(pCommandBuffer, &ColorInfo, 1, NULL);

		SDL_BindGPUGraphicsPipeline(pRenderPass, pState->pPipeline);

		SDL_BindGPUVertexBuffers(
			pRenderPass,
			0,
			&(SDL_GPUBufferBinding){
				.buffer = pState->pVertexBuffer,
				.offset = 0
			},
			1
		);

		SDL_BindGPUVertexStorageBuffers(pRenderPass, 0, &pState->pBarHeightBuffer, 1);

		cbv_parameter CbvParameter = {
			{fBarColor[0], fBarColor[1], fBarColor[2], fBarColor[3]},
			pState->fBarWidth / (float)TextureW,
			pState->fBarGap / (float)TextureW
		};
		SDL_PushGPUVertexUniformData(
			pCommandBuffer,
			0,
			&CbvParameter,
			sizeof(CbvParameter)
		);

		SDL_DrawGPUPrimitives(pRenderPass, 6, pState->nBar, 0, 0);
		SDL_EndGPURenderPass(pRenderPass);
	}

	SDL_GPUFence* pFrameFence =
		SDL_SubmitGPUCommandBufferAndAcquireFence(pCommandBuffer);
	SDL_WaitForGPUFences(pState->pDevice, true, &pFrameFence, 1);
	SDL_ReleaseGPUFence(pState->pDevice, pFrameFence);

}

void RenderModern_Destroy(void* pStateVoid) {
	render_modern_state* pState = (render_modern_state*)pStateVoid;
	SDL_ReleaseGPUBuffer(pState->pDevice, pState->pVertexBuffer);
	SDL_ReleaseGPUTransferBuffer(pState->pDevice, pState->pTransferBuffer);
	SDL_ReleaseGPUBuffer(pState->pDevice, pState->pBarHeightBuffer);
	SDL_ReleaseGPUGraphicsPipeline(pState->pDevice, pState->pPipeline);
	SDL_ReleaseWindowFromGPUDevice(pState->pDevice, pState->pWindow);
	SDL_DestroyGPUDevice(pState->pDevice);
	free(pState);
}

#else

void* RenderModern_Init(
	SDL_Window* pWindow,
	size_t WindowW,
	size_t WindowH,
	size_t nBar,
	float fBarWidth,
	float fBarGap,
	uint32_t BackgroundColor,
	uint32_t BarColor
) {
	(void)pWindow;
	(void)WindowW;
	(void)WindowH;
	(void)nBar;
	(void)fBarWidth;
	(void)fBarGap;
	(void)BackgroundColor;
	(void)BarColor;
	return NULL;
}

void RenderModern_Render(void* pStateVoid, const float* aOutput) {
	(void)pStateVoid;
	(void)aOutput;
}

void RenderModern_Destroy(void* pStateVoid) {
	(void)pStateVoid;
}

#endif
