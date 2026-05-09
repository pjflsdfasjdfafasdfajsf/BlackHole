#include <SDL3/SDL.h>
#include <SDL3_shadercross/SDL_shadercross.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "Math.h"

#define Unused(X) (void)X;
#define Assert(X) if (!(X)) { *(int *)0 = 0; }
#define ArrayCount(X) (sizeof(X) / sizeof((X)[0]))

#define NotImplemented Assert(!"TODO"); __builtin_unreachable()

#define KB (1024ULL)
#define MB (1024ULL * KB)

static TTF_Text *TextListHead = 0;

struct ReadFileResult
{
	void *Memory;
	Sint64 MemorySize;
};

struct Pipeline
{
	const char *Shader;
	SDL_GPUVertexBufferDescription *VertexBufferDescription;
	int NumVertexBuffers;
	SDL_GPUVertexAttribute *VertexAttributes;
	int NumVertexAttributes;
	bool Blend;

	SDL_GPUGraphicsPipeline *Handle;
	SDL_Time LastWriteTime;
};

struct TextDrawCommand
{
	TTF_GPUAtlasDrawSequence *Sequence;
	TTF_Text *Text;
	int VertexOffset;
	int IndexOffset;
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

// these are purely for readability and do not have any functionality behind them
static void BeginPipeline(void) {}
static void EndPipeline(void) {}

static void PipelineSetShader(struct Pipeline *Pipeline, const char *Shader)
{
	Pipeline->Shader = Shader;
}

static void PipelineSetFormat(struct Pipeline *Pipeline, SDL_GPUVertexBufferDescription *VertexBufferDescription, int NumVertexBuffers, 
	SDL_GPUVertexAttribute *VertexAttributes, int NumVertexAttributes)
{
	Pipeline->VertexBufferDescription = VertexBufferDescription;
	Pipeline->NumVertexBuffers = NumVertexBuffers;
	Pipeline->VertexAttributes = VertexAttributes;
	Pipeline->NumVertexAttributes = NumVertexAttributes;
}

static void PipelineSetBlend(struct Pipeline *Pipeline, bool Enable)
{
	Pipeline->Blend = Enable;
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

static SDL_GPUGraphicsPipeline *CreatePipeline(SDL_GPUDevice *Device, SDL_Window *Window, struct Pipeline *Pipeline)
{
	if (!Pipeline->Shader)
	{
		return 0;
	}

	SDL_GPUGraphicsPipeline *Result = 0;

	struct ReadFileResult ShaderSource = ReadEntireFile(Pipeline->Shader);
	if (!ShaderSource.Memory)
	{
		return 0;
	}

	SDL_GPUShader *VertexShader = CompileHLSL(Device, (const char *)ShaderSource.Memory, SDL_SHADERCROSS_SHADERSTAGE_VERTEX, "VsMain");
	SDL_GPUShader *FragmentShader = CompileHLSL(Device, (const char *)ShaderSource.Memory, SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT, "PsMain");

	if (VertexShader && FragmentShader)
	{
		SDL_GPUColorTargetDescription ColorTargetDescription = {
			.format = SDL_GetGPUSwapchainTextureFormat(Device, Window),
			.blend_state.color_write_mask = 0xF,
		};

		if (Pipeline->Blend)
		{
			ColorTargetDescription.blend_state.enable_blend = true;
			ColorTargetDescription.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
			ColorTargetDescription.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
			ColorTargetDescription.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
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

	FreeFileMemory(ShaderSource);

	return Result;
}

static void AppendText(TTF_Text *Text)
{
	SDL_PropertiesID Properties = TTF_GetTextProperties(Text);
	SDL_SetPointerProperty(Properties, "Next", TextListHead);
	TextListHead = Text;
}

/// use this function instead of TTF_SetTextPosition for positioning TTF_Text.
/// SDL_ttf's TTF_SetTextPosition bakes the position into vertex data and also clips glyps against the unoffset text width, causing the text to get cut off and
/// this function is the workaround against this issue.
static void SetTextPosition(TTF_Text *Text, int X, int Y)
{
	SDL_PropertiesID Properties = TTF_GetTextProperties(Text);
	SDL_SetNumberProperty(Properties, "X", X);
	SDL_SetNumberProperty(Properties, "Y", Y);
}

int main(void)
{
	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		SDL_Log("%s", SDL_GetError());

		return 1;
	}

	if (!TTF_Init())
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

	struct Pipeline Pipelines[2] = {0};

	BeginPipeline();
	{
		struct Pipeline *Pipeline = &Pipelines[0];

		PipelineSetShader(Pipeline, "Shaders/Mesh.hlsl");
	}
	EndPipeline();

	BeginPipeline();
	{
		struct Pipeline *Pipeline = &Pipelines[1];

		static SDL_GPUVertexBufferDescription VertexBufferDescription = {
			.slot = 0,
			.pitch = sizeof(struct Vertex),
			.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
			.instance_step_rate = 0,
		};

		static SDL_GPUVertexAttribute VertexAttributes[3] = {
			{ .location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = 0 },
			{ .location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, .offset = sizeof(float) * 3 },
			{ .location = 2, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = sizeof(float) * 7 },
		};

		PipelineSetShader(Pipeline, "Shaders/Text.hlsl");
		PipelineSetFormat(Pipeline, &VertexBufferDescription, 1, VertexAttributes, 3);
		PipelineSetBlend(Pipeline, true);
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

	SDL_GPUBuffer *TextVertexBuffer = SDL_CreateGPUBuffer(Device, &(SDL_GPUBufferCreateInfo){
		.usage = SDL_GPU_BUFFERUSAGE_VERTEX,
		.size = 16 * MB,
	});

	SDL_GPUBuffer *TextIndexBuffer = SDL_CreateGPUBuffer(Device, &(SDL_GPUBufferCreateInfo){
		.usage = SDL_GPU_BUFFERUSAGE_INDEX,
		.size = 16 * MB,
	});

	SDL_GPUTransferBuffer *TextTransferBuffer = SDL_CreateGPUTransferBuffer(Device, &(SDL_GPUTransferBufferCreateInfo){
		.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
		.size = 16 * MB + 16 * MB,
	});

	SDL_GPUSampler *TextSampler = SDL_CreateGPUSampler(Device, &(SDL_GPUSamplerCreateInfo){
		.min_filter = SDL_GPU_FILTER_LINEAR,
		.mag_filter = SDL_GPU_FILTER_LINEAR,
	});

	TTF_Font *Font = TTF_OpenFont("Assets/DebugFont.ttf", 64.0f);
	if (!Font)
	{
		SDL_Log("%s", SDL_GetError());

		return 1;
	}

	TTF_TextEngine *TextEngine = TTF_CreateGPUTextEngine(Device);

	TTF_Text *TitleText = TTF_CreateText(TextEngine, Font, "hello", 0);
	SetTextPosition(TitleText, 100, 0);
	AppendText(TitleText);

	TTF_Text *OtherText = TTF_CreateText(TextEngine, Font, "HI", 0);
	SetTextPosition(OtherText, 100, -100);
	AppendText(OtherText);

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

		Uint8 *TransferMap = SDL_MapGPUTransferBuffer(Device, TextTransferBuffer, 1);
		struct Vertex *VertexDestination = (struct Vertex *)TransferMap;
		Uint32 *IndexDestination = (Uint32 *)(TransferMap + 16 * MB);

		int Vertices = 0, Indices = 0;
		struct TextDrawCommand DrawCommands[256];
		int DrawCommandCount = 0;

		for (TTF_Text *Text = TextListHead; Text; )
		{
			float R, G, B, A;
			TTF_GetTextColorFloat(Text, &R, &G, &B, &A);

			TTF_GPUAtlasDrawSequence *Sequence = TTF_GetGPUTextDrawData(Text);
			while (Sequence)
			{
				Assert((unsigned int)DrawCommandCount < ArrayCount(DrawCommands));
				DrawCommands[DrawCommandCount++] = (struct TextDrawCommand){ Sequence, Text, Vertices, Indices };

				for (int Vertex = 0; Vertex < Sequence->num_vertices; Vertex++)
				{
					Assert(Vertices < (int)(16 * MB / sizeof(struct Vertex)));

					VertexDestination[Vertices + Vertex] = (struct Vertex){ 
						Sequence->xy[Vertex].x, Sequence->xy[Vertex].y, 0.0f,
						R, G, B, A,
						Sequence->uv[Vertex].x, Sequence->uv[Vertex].y,
					};
				}
				for (int Index = 0; Index < Sequence->num_indices; Index++)
				{
					IndexDestination[Indices + Index] = Sequence->indices[Index];
				}

				Vertices += Sequence->num_vertices;
				Indices += Sequence->num_indices;
				Sequence = Sequence->next;
			}

			SDL_PropertiesID Properties = TTF_GetTextProperties(Text);
			Text = SDL_GetPointerProperty(Properties, "Next", 0);
		}
		SDL_UnmapGPUTransferBuffer(Device, TextTransferBuffer);

		if (Vertices > 0)
		{
			SDL_GPUCopyPass *CopyPass = SDL_BeginGPUCopyPass(CommandBuffer);
			SDL_UploadToGPUBuffer(CopyPass, 
				&(SDL_GPUTransferBufferLocation){ .transfer_buffer = TextTransferBuffer, .offset = 0 }, 
				&(SDL_GPUBufferRegion){ .buffer = TextVertexBuffer, .offset = 0, .size = Vertices * sizeof(struct Vertex) }, true);
			
			SDL_UploadToGPUBuffer(CopyPass, 
				&(SDL_GPUTransferBufferLocation){ .transfer_buffer = TextTransferBuffer, .offset = 16 * MB }, 
				&(SDL_GPUBufferRegion){ .buffer = TextIndexBuffer, .offset = 0, .size = Indices * sizeof(Uint32) }, true);

			SDL_EndGPUCopyPass(CopyPass);
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
			if (Pipelines[0].Handle)
			{
				SDL_BindGPUGraphicsPipeline(RenderPass, Pipelines[0].Handle);
				SDL_DrawGPUPrimitives(RenderPass, 3, 1, 0, 0);
			}

			if (Pipelines[1].Handle)
			{
				SDL_BindGPUGraphicsPipeline(RenderPass, Pipelines[1].Handle);
				SDL_BindGPUVertexBuffers(RenderPass, 0, &(SDL_GPUBufferBinding){
					.buffer = TextVertexBuffer,
					.offset = 0,
				}, 1);
				SDL_BindGPUIndexBuffer(RenderPass, &(SDL_GPUBufferBinding){
					.buffer = TextIndexBuffer,
					.offset = 0,
				}, SDL_GPU_INDEXELEMENTSIZE_32BIT);

				for (int i = 0; i < DrawCommandCount; i++)
				{
					struct TextDrawCommand DrawCommand = DrawCommands[i];

					SDL_PropertiesID Properties = TTF_GetTextProperties(DrawCommand.Text);
					float X = (float)SDL_GetNumberProperty(Properties, "X", 0);
					float Y = (float)SDL_GetNumberProperty(Properties, "Y", 0);

					struct Matrix4x4 Uniforms[2] = {
						OrthographicMatrix(0.0f, 1280.0f, -720.0f, 0.0f, -1.0f, 1.0f),
						TranslationMatrix(Vector3(X, Y, 1.0f)),
					};

					SDL_PushGPUVertexUniformData(CommandBuffer, 0, Uniforms, sizeof(Uniforms));

					SDL_BindGPUFragmentSamplers(RenderPass, 0, &(SDL_GPUTextureSamplerBinding){
						.texture = DrawCommand.Sequence->atlas_texture,
						.sampler = TextSampler,
					}, 1);

					SDL_DrawGPUIndexedPrimitives(RenderPass, DrawCommand.Sequence->num_indices, 1, DrawCommand.IndexOffset, DrawCommand.VertexOffset, 0);
				}
			}
			SDL_EndGPURenderPass(RenderPass);
		}

		SDL_SubmitGPUCommandBuffer(CommandBuffer);
	}
}