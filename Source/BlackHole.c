#include <SDL3/SDL.h>
#include <SDL3_shadercross/SDL_shadercross.h>

#define MIPS 6

#define Unused(X) (void)X;
#define Assert(X) if (!(X)) { *(int *)0 = 0; }
#define ArrayCount(X) (sizeof(X) / sizeof((X)[0]))

#define NotImplemented Assert(!"TODO"); __builtin_unreachable()

#define KB (1024ULL)
#define MB (1024ULL * KB)

enum BlendMode
{
	BLENDMODE_NONE,
	BLENDMODE_ALPHA,
	BLENDMODE_ADDITIVE,
};

struct Pipeline
{
	const char *Shader;
	SDL_GPUVertexBufferDescription *VertexBufferDescription;
	int NumVertexBuffers;
	SDL_GPUVertexAttribute *VertexAttributes;
	int NumVertexAttributes;
	enum BlendMode BlendMode;

	SDL_GPUTextureFormat ColorTargetFormat;

	SDL_GPUGraphicsPipeline *Handle;
	SDL_Time LastWriteTime;
};

// these are purely for readability and do not have any functionality behind them
static void BeginPipeline(void) {}
static void EndPipeline(void) {}

static void PipelineSetShader(struct Pipeline *Pipeline, const char *Shader)
{
	Pipeline->Shader = Shader;
}

static void PipelineSetBlendMode(struct Pipeline *Pipeline, enum BlendMode BlendMode)
{
	Pipeline->BlendMode = BlendMode;
}

static void PipelineSetTargetFormat(struct Pipeline *Pipeline, SDL_GPUTextureFormat Format)
{
	Pipeline->ColorTargetFormat = Format;
}

static SDL_GPUShader *CompileHLSL(SDL_GPUDevice *Device, const char *Path, SDL_ShaderCross_ShaderStage Stage, const char *Entrypoint)
{
	Uint64 StartTime = SDL_GetPerformanceCounter();
	
	char CachePath[256];
	SDL_snprintf(CachePath, sizeof(CachePath), "%s.%s.spv", Path, Entrypoint);

	SDL_PathInfo HLSLInfo, CacheInfo;
	bool CacheValid = false;

	if (SDL_GetPathInfo(Path, &HLSLInfo) && SDL_GetPathInfo(CachePath, &CacheInfo))
	{
		if (CacheInfo.modify_time >= HLSLInfo.modify_time)
		{
			CacheValid = true;
		}
	}

	void *SPIRV = 0;
	Uint64 SPIRVSize = 0;

	if (CacheValid)
	{
		// fast path, load the compiled binary blob
		SPIRV = SDL_LoadFile(CachePath, &SPIRVSize);
	}

	if (!SPIRV)
	{
		// slow path, compile hlsl
		Uint64 HLSLSize = 0;
		void *HLSLSource = SDL_LoadFile(Path, &HLSLSize);

		if (!HLSLSize)
		{
			return 0;
		}

		SDL_ShaderCross_HLSL_Info HLSL = {
			.source = (const char *)HLSLSource,
			.entrypoint = Entrypoint,
			.shader_stage = Stage,
		};

		SPIRV = SDL_ShaderCross_CompileSPIRVFromHLSL(&HLSL, &SPIRVSize);
		SDL_free(HLSLSource);

		if (SPIRV)
		{
			// save the binary blob so next time it does not need to be compiled and hits the path above
			SDL_IOStream *IO = SDL_IOFromFile(CachePath, "wb");

			if (IO)
			{
				SDL_WriteIO(IO, SPIRV, SPIRVSize);
				SDL_CloseIO(IO);
			}
		}
		else
		{
			SDL_Log("%s", SDL_GetError());
		}
	}

	if (!SPIRV)
	{
		return 0;
	}

	SDL_ShaderCross_GraphicsShaderMetadata *Metadata = SDL_ShaderCross_ReflectGraphicsSPIRV(SPIRV, SPIRVSize, 0);
	
	if (!Metadata)
	{
		SDL_free(SPIRV);
		
		return 0;
	}

	SDL_ShaderCross_SPIRV_Info SPIRVInfo = {
		.bytecode = SPIRV,
		.bytecode_size = SPIRVSize,
		.entrypoint = Entrypoint,
		.shader_stage = Stage,
	};

	SDL_GPUShader *Shader = SDL_ShaderCross_CompileGraphicsShaderFromSPIRV(Device, &SPIRVInfo, &Metadata->resource_info, 0);

	SDL_free(Metadata);
	SDL_free(SPIRV);

	Uint64 EndTime = SDL_GetPerformanceCounter();
	double ElapsedMS = (double)(EndTime - StartTime) * 1000.0 / (double)SDL_GetPerformanceFrequency();

	SDL_Log("%s:%s took %.2f ms", Path, Entrypoint, ElapsedMS);

	return Shader;
}

static SDL_GPUGraphicsPipeline *CreatePipeline(SDL_GPUDevice *Device, SDL_Window *Window, struct Pipeline *Pipeline)
{
	if (!Pipeline->Shader)
	{
		return 0;
	}

	SDL_GPUGraphicsPipeline *Result = 0;

	SDL_GPUShader *VertexShader = CompileHLSL(Device, Pipeline->Shader, SDL_SHADERCROSS_SHADERSTAGE_VERTEX, "VsMain");
	SDL_GPUShader *FragmentShader = CompileHLSL(Device, Pipeline->Shader, SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT, "PsMain");

	if (VertexShader && FragmentShader)
	{
		SDL_GPUColorTargetDescription ColorTargetDescription = {
			.format = (Pipeline->ColorTargetFormat != 0) ? Pipeline->ColorTargetFormat : SDL_GetGPUSwapchainTextureFormat(Device, Window),
			.blend_state.color_write_mask = 0xF,
		};

		if (Pipeline->BlendMode != BLENDMODE_NONE)
		{
			ColorTargetDescription.blend_state.enable_blend = true;
			ColorTargetDescription.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
			ColorTargetDescription.blend_state.src_color_blendfactor = Pipeline->BlendMode == BLENDMODE_ADDITIVE 
				? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_SRC_ALPHA;
			ColorTargetDescription.blend_state.dst_color_blendfactor = Pipeline->BlendMode == BLENDMODE_ADDITIVE 
				? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
			ColorTargetDescription.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
			ColorTargetDescription.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
			ColorTargetDescription.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
		}

		SDL_GPUGraphicsPipelineCreateInfo PipelineCreateInfo = {
			.vertex_shader = VertexShader,
			.fragment_shader = FragmentShader,
			.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,

			.vertex_input_state.vertex_buffer_descriptions = Pipeline->VertexBufferDescription,
			.vertex_input_state.num_vertex_buffers = Pipeline->NumVertexBuffers,
			.vertex_input_state.vertex_attributes = Pipeline->VertexAttributes,
			.vertex_input_state.num_vertex_attributes = Pipeline->NumVertexAttributes,

			.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE,

			.target_info.num_color_targets = 1,
			.target_info.color_target_descriptions = &ColorTargetDescription,
		};

		Result = SDL_CreateGPUGraphicsPipeline(Device, &PipelineCreateInfo);
		if (!Result)
		{
			SDL_Log("%s", SDL_GetError());
		}
	}

	if (VertexShader)
	{
		SDL_ReleaseGPUShader(Device, VertexShader);
	}
	if (FragmentShader)
	{
		SDL_ReleaseGPUShader(Device, FragmentShader);
	}

	return Result;
}

int main(void)
{
	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		SDL_Log("%s", SDL_GetError());

		return 1;
	}

	SDL_Window *Window = SDL_CreateWindow("Black Hole", 1280, 720, 0);
	if (!Window)
	{
		SDL_Log("%s", SDL_GetError());

		return 1;
	}

	SDL_GPUDevice *Device = SDL_CreateGPUDevice(SDL_ShaderCross_GetHLSLShaderFormats(), 0, 0);
	if (!Device)
	{
		SDL_Log("%s", SDL_GetError());

		return 1;
	}

	if (!SDL_ClaimWindowForGPUDevice(Device, Window))
	{
		SDL_Log("%s", SDL_GetError());

		return 1;
	}

	struct Pipeline Pipelines[4] = {0};

	BeginPipeline();
	{
		struct Pipeline *Pipeline = &Pipelines[0];

		PipelineSetShader(Pipeline, "Shaders/BlackHole.hlsl");
		PipelineSetTargetFormat(Pipeline, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT);
	}
	EndPipeline();

	BeginPipeline();
	{
		struct Pipeline *Pipeline = &Pipelines[1];

		PipelineSetShader(Pipeline, "Shaders/Downsample.hlsl");
		PipelineSetTargetFormat(Pipeline, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT);
	}
	EndPipeline();

	BeginPipeline();
	{
		struct Pipeline *Pipeline = &Pipelines[2];

		PipelineSetShader(Pipeline, "Shaders/Upsample.hlsl");
		PipelineSetTargetFormat(Pipeline, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT);
		PipelineSetBlendMode(Pipeline, BLENDMODE_ADDITIVE);
	}
	EndPipeline();

	BeginPipeline();
	{
		struct Pipeline *Pipeline = &Pipelines[3];

		PipelineSetShader(Pipeline, "Shaders/Composite.hlsl");
	}
	EndPipeline();

	for (unsigned int i = 0; i < ArrayCount(Pipelines); i++)
	{
		if (!Pipelines[i].Shader)
		{
			continue;
		}

		Pipelines[i].Handle = CreatePipeline(Device, Window, &Pipelines[i]);

		SDL_PathInfo PathInfo;
		if (SDL_GetPathInfo(Pipelines[i].Shader, &PathInfo))
		{
			Pipelines[i].LastWriteTime = PathInfo.modify_time;
		}
	}

	SDL_GPUSampler *LinearSampler = SDL_CreateGPUSampler(Device, &(SDL_GPUSamplerCreateInfo){
		.min_filter = SDL_GPU_FILTER_LINEAR,
		.mag_filter = SDL_GPU_FILTER_LINEAR,
	});

    SDL_GPUTexture *SceneTexture = SDL_CreateGPUTexture(Device, &(SDL_GPUTextureCreateInfo){
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
        .width = 1280, .height = 720,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
    });

    SDL_GPUTexture *DownsampleTextures[MIPS];
    {
    	int W = 1280 / 2, H = 720 / 2;

    	for (int i = 0; i < MIPS; i++)
    	{
    		DownsampleTextures[i] = SDL_CreateGPUTexture(Device, &(SDL_GPUTextureCreateInfo){
	            .type = SDL_GPU_TEXTURETYPE_2D,
	            .format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
	            .width = W,
	            .height = H,
	            .layer_count_or_depth = 1,
	            .num_levels = 1,
	            .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
        	});

        	W = (W > 1) ? W / 2 : 1;
       		H = (H > 1) ? H / 2 : 1;
    	}
    }

	for (;;) 
	{
		SDL_Event Event;


		while (SDL_PollEvent(&Event))
		{
			switch (Event.type)
			{
	
			case SDL_EVENT_QUIT: return 0;
			
			default: break;
			}
		}

		for (unsigned int i = 0; i < ArrayCount(Pipelines); i++)
		{
			if (!Pipelines[i].Shader)
			{
				continue;
			}

			SDL_PathInfo PathInfo;
			if (SDL_GetPathInfo(Pipelines[i].Shader, &PathInfo) && (PathInfo.modify_time > Pipelines[i].LastWriteTime))
			{
				// NOTE: just in case. some editors may write the whole file to zeroes for example
				SDL_Delay(10);

				SDL_GPUGraphicsPipeline *NewHandle = CreatePipeline(Device, Window, &Pipelines[i]);

				if (NewHandle)
				{
					if (Pipelines[i].Handle)
					{
						SDL_ReleaseGPUGraphicsPipeline(Device, Pipelines[i].Handle);
					}

					Pipelines[i].Handle = NewHandle;
				}

				Pipelines[i].LastWriteTime = PathInfo.modify_time;
			}
		}

		SDL_GPUCommandBuffer *CommandBuffer = SDL_AcquireGPUCommandBuffer(Device);
		if (!CommandBuffer)
		{
			continue;
		}

		SDL_GPUTexture *SwapchainTexture = 0;
		if (!SDL_WaitAndAcquireGPUSwapchainTexture(CommandBuffer, Window, &SwapchainTexture, 0, 0) || !SwapchainTexture)
		{
			// the command buffer must still be submitted to clean everything up
			SDL_SubmitGPUCommandBuffer(CommandBuffer);

			continue;
		}

		SDL_GPURenderPass *ScenePass = SDL_BeginGPURenderPass(CommandBuffer, &(SDL_GPUColorTargetInfo){
			.texture = SceneTexture,
			.clear_color = (SDL_FColor){0,0,0,0},
			.load_op = SDL_GPU_LOADOP_CLEAR,
			.store_op = SDL_GPU_STOREOP_STORE,
		}, 1, 0);

		if (ScenePass)
		{
			if (Pipelines[0].Handle)
			{
				SDL_BindGPUGraphicsPipeline(ScenePass, Pipelines[0].Handle);

				float Time = (float)SDL_GetTicks() / 1000.0f;
				SDL_PushGPUVertexUniformData(CommandBuffer, 0, &Time, sizeof(Time));

				SDL_DrawGPUPrimitives(ScenePass, 3, 1, 0, 0);
			}

			SDL_EndGPURenderPass(ScenePass);
		}

		SDL_GPUTexture *CurrentSource = SceneTexture;
		for (int i = 0; i < MIPS; i++)
		{
			SDL_GPURenderPass *DownsamplePass = SDL_BeginGPURenderPass(CommandBuffer, &(SDL_GPUColorTargetInfo){
				.texture = DownsampleTextures[i],
				.clear_color = (SDL_FColor){0,0,0,0},
				.load_op = SDL_GPU_LOADOP_CLEAR,
				.store_op = SDL_GPU_STOREOP_STORE,
			}, 1, 0);

			if (DownsamplePass)
			{
				if (Pipelines[1].Handle)
				{
					SDL_BindGPUGraphicsPipeline(DownsamplePass, Pipelines[1].Handle);

					SDL_BindGPUFragmentSamplers(DownsamplePass, 0, &(SDL_GPUTextureSamplerBinding){
						.texture = CurrentSource,
						.sampler = LinearSampler
					}, 1);

					SDL_DrawGPUPrimitives(DownsamplePass, 3, 1, 0, 0);
				}

				SDL_EndGPURenderPass(DownsamplePass);
			}

			CurrentSource = DownsampleTextures[i];
		}

		for (int i = MIPS - 2; i >= 0; i--)
		{
            SDL_GPURenderPass *UpsamplePass = SDL_BeginGPURenderPass(CommandBuffer, &(SDL_GPUColorTargetInfo){
                .texture = DownsampleTextures[i],
                .load_op = SDL_GPU_LOADOP_LOAD,
                .store_op = SDL_GPU_STOREOP_STORE
            }, 1, 0);

            if (UpsamplePass) 
            {
                if (Pipelines[2].Handle) 
                {
                    SDL_BindGPUGraphicsPipeline(UpsamplePass, Pipelines[2].Handle);
                    
                    SDL_BindGPUFragmentSamplers(UpsamplePass, 0, &(SDL_GPUTextureSamplerBinding){
                    	.texture = DownsampleTextures[i+1],
                    	.sampler = LinearSampler 
                    }, 1);

                    SDL_DrawGPUPrimitives(UpsamplePass, 3, 1, 0, 0);
                }

                SDL_EndGPURenderPass(UpsamplePass);
            }
        }

		SDL_GPURenderPass *CompositePass = SDL_BeginGPURenderPass(CommandBuffer, &(SDL_GPUColorTargetInfo){
			.texture = SwapchainTexture,
			.clear_color = (SDL_FColor){0,0,0,0},
			.load_op = SDL_GPU_LOADOP_CLEAR,
			.store_op = SDL_GPU_STOREOP_STORE,
		}, 1, 0);

		if (CompositePass)
		{
			if (Pipelines[3].Handle)
			{
				SDL_BindGPUGraphicsPipeline(CompositePass, Pipelines[3].Handle);

                SDL_GPUTextureSamplerBinding Samplers[2] = {
                    { .texture = SceneTexture, .sampler = LinearSampler },
                    { .texture = DownsampleTextures[0], .sampler = LinearSampler }
                };
                SDL_BindGPUFragmentSamplers(CompositePass, 0, Samplers, 2);

				SDL_DrawGPUPrimitives(CompositePass, 3, 1, 0, 0);
			}

			SDL_EndGPURenderPass(CompositePass);
		}

		SDL_SubmitGPUCommandBuffer(CommandBuffer);
	}
}