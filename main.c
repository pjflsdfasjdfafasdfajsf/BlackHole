#include <SDL3/SDL.h>
#include <SDL3_shadercross/SDL_shadercross.h>

#define Unused(Expression) (void)Expression;
#define Assert(Expression) if (!(Expression)) { *(int *)0 = 0; }
#define NotImplemented Assert(!"TODO"); __builtin_unreachable()

struct ReadFileResult
{
	void *Memory;
	Sint64 MemorySize;
};

static struct ReadFileResult ReadEntireFile(const char *Path)
{
	struct ReadFileResult Result = {0};

	Uint64 MemorySize = 0;
	void *Memory = SDL_LoadFile(Path, &MemorySize);

	if (Memory)
	{
		Result.Memory = Memory;
		Result.MemorySize = MemorySize;
	}
	else
	{
		SDL_Log("%s", SDL_GetError());
	}

	return Result;
}

static void FreeFileMemory(struct ReadFileResult File)
{
	if (File.Memory)
	{
		SDL_free(File.Memory);
	}
}

static SDL_GPUShader *CompileHLSL(SDL_GPUDevice *Device, const char *Source, SDL_ShaderCross_ShaderStage Stage, const char *Entrypoint)
{
	SDL_ShaderCross_HLSL_Info HLSL = {
		.source = Source,
		.entrypoint = Entrypoint,
		.shader_stage = Stage,
	};

	Uint64 SPIRVSize = 0;
	void *SPIRV = SDL_ShaderCross_CompileSPIRVFromHLSL(&HLSL, &SPIRVSize);
	if (!SPIRV)
	{
		SDL_Log("%s", SDL_GetError());

		return 0;
	}

	SDL_ShaderCross_GraphicsShaderMetadata *Metadata = SDL_ShaderCross_ReflectGraphicsSPIRV(SPIRV, SPIRVSize, 0);
	if (!Metadata)
	{
		SDL_Log("%s", SDL_GetError());

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

	if (!Shader)
	{
		SDL_Log("%s", SDL_GetError());
	}

	return Shader;
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

	struct ReadFileResult ShaderSource = ReadEntireFile("Shaders/Mesh.hlsl");
	Assert(ShaderSource.Memory);

	SDL_GPUShader *VertexShader = CompileHLSL(Device, (const char *)ShaderSource.Memory, SDL_SHADERCROSS_SHADERSTAGE_VERTEX, "VsMain");
	if (!VertexShader)
	{
		SDL_Log("%s", SDL_GetError());

		return 1;
	}

	SDL_GPUShader *FragmentShader = CompileHLSL(Device, (const char *)ShaderSource.Memory, SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT, "PsMain");
	if (!FragmentShader)
	{
		SDL_Log("%s", SDL_GetError());

		return 1;
	}

	FreeFileMemory(ShaderSource);

	SDL_GPUColorTargetDescription ColorTargetDescription = {
		.format = SDL_GetGPUSwapchainTextureFormat(Device, Window),
		.blend_state.color_write_mask = 0xF,
	};

	SDL_GPUGraphicsPipelineCreateInfo PipelineCreateInfo = {
		.vertex_shader = VertexShader,
		.fragment_shader = FragmentShader,
		.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,

		.vertex_input_state.num_vertex_buffers = 0,
		.vertex_input_state.num_vertex_attributes = 0,

		.target_info.num_color_targets = 1,
		.target_info.color_target_descriptions = &ColorTargetDescription,
	};

	SDL_GPUGraphicsPipeline *Pipeline = SDL_CreateGPUGraphicsPipeline(Device, &PipelineCreateInfo);
	if (!Pipeline)
	{
		SDL_Log("%s", SDL_GetError());

		return 1;
	}

	SDL_ReleaseGPUShader(Device, VertexShader);
	SDL_ReleaseGPUShader(Device, FragmentShader);

	for (;;) 
	{
		SDL_Event Event;

		while (SDL_PollEvent(&Event))
		{
			if (Event.type == SDL_EVENT_QUIT)
			{
				return 0;
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

		SDL_GPUColorTargetInfo ColorTarget = {
			.texture = SwapchainTexture,
			.clear_color = (SDL_FColor){0.0f, 0.0f, 0.0f, 0.0f},
			.load_op = SDL_GPU_LOADOP_CLEAR,
			.store_op = SDL_GPU_STOREOP_STORE,
		};

		SDL_GPURenderPass *RenderPass = SDL_BeginGPURenderPass(CommandBuffer, &ColorTarget, 1, 0);
		if (RenderPass)
		{
			SDL_BindGPUGraphicsPipeline(RenderPass, Pipeline);
			SDL_DrawGPUPrimitives(RenderPass, 3, 1, 0, 0);
			SDL_EndGPURenderPass(RenderPass);
		}

		SDL_SubmitGPUCommandBuffer(CommandBuffer);
	}
}