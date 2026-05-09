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

static TTF_TextEngine *TextEngine;
static SDL_GPUTexture *MagicPixel;

static TTF_Font *DebugFont;

static struct Vertex *VertexDestination;
static Uint32 *IndexDestination;
static int Vertices;
static int Indices;

static const void *HotID = NULL;
static const void *ActiveID = NULL;

static float MouseX = 0, MouseY = 0;
static float DragOffsetX = 0, DragOffsetY = 0;
static bool MouseDown = false;

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

struct UIDrawCommand
{
    SDL_GPUTexture *Texture;
    int VertexOffset;
    int IndexOffset;
    int NumIndices;
    float X, Y;
    float W, H;
    float Radius;
    float Mode;
    Uint8 CornerFlags;
};

static struct UIDrawCommand DrawCommands[256];
static int DrawCommandCount;

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

/// a magic pixel is 1x1 white texture
static SDL_GPUTexture *CreateMagicPixel(SDL_GPUDevice *Device)
{
	SDL_GPUTexture *MagicPixel = SDL_CreateGPUTexture(Device, &(SDL_GPUTextureCreateInfo){
		.type = SDL_GPU_TEXTURETYPE_2D,
		.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
		.width = 1, .height = 1,
		.layer_count_or_depth = 1,
		.num_levels = 1,
		.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
	});

	SDL_GPUTransferBuffer *TransferBuffer = SDL_CreateGPUTransferBuffer(Device, &(SDL_GPUTransferBufferCreateInfo){
		.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
		.size = 4,
	});

	*(Uint32 *)SDL_MapGPUTransferBuffer(Device, TransferBuffer, false) = 0xFFFFFFFF;
	SDL_UnmapGPUTransferBuffer(Device, TransferBuffer);

	SDL_GPUCommandBuffer *CommandBuffer = SDL_AcquireGPUCommandBuffer(Device);
	SDL_GPUCopyPass *CopyPass = SDL_BeginGPUCopyPass(CommandBuffer);
	SDL_UploadToGPUTexture(CopyPass, 
		&(SDL_GPUTextureTransferInfo){ .transfer_buffer = TransferBuffer }, 
		&(SDL_GPUTextureRegion){ .texture = MagicPixel, .w = 1, .h = 1, .d = 1 }, false);
	SDL_EndGPUCopyPass(CopyPass);
	SDL_SubmitGPUCommandBuffer(CommandBuffer);
	SDL_ReleaseGPUTransferBuffer(Device, TransferBuffer);

	return MagicPixel;
}

static void UIBox(float X, float Y, float W, float H, float Radius, Uint8 CornerFlags, SDL_FColor Color)
{
	Assert((unsigned int)DrawCommandCount < ArrayCount(DrawCommands));
	Assert(Vertices + 4 < (int)(16 * MB / sizeof(struct Vertex)));

	DrawCommands[DrawCommandCount++] = (struct UIDrawCommand){
	    .Texture = MagicPixel,
	    .VertexOffset = Vertices,
	    .IndexOffset = Indices,
	    .NumIndices = 6,
	    .X = 0, .Y = 0,
	    .W = W, .H = H,
	    .Radius = Radius,
	    .Mode = 1.0f,
	    .CornerFlags = CornerFlags,
	};

	VertexDestination[Vertices + 0] = (struct Vertex){ X,     -Y,     0, Color.r, Color.g, Color.b, Color.a, 0, 0 };
    VertexDestination[Vertices + 1] = (struct Vertex){ X + W, -Y,     0, Color.r, Color.g, Color.b, Color.a, 1, 0 };
    VertexDestination[Vertices + 2] = (struct Vertex){ X + W, -(Y+H), 0, Color.r, Color.g, Color.b, Color.a, 1, 1 };
    VertexDestination[Vertices + 3] = (struct Vertex){ X,     -(Y+H), 0, Color.r, Color.g, Color.b, Color.a, 0, 1 };

    IndexDestination[Indices + 0] = 0;
    IndexDestination[Indices + 1] = 1;
    IndexDestination[Indices + 2] = 2;
    IndexDestination[Indices + 3] = 0;
    IndexDestination[Indices + 4] = 2;
    IndexDestination[Indices + 5] = 3;

    Vertices += 4;
    Indices += 6;
}

static bool UIDraggableBox(const void *ID, float X, float Y, float W, float H, 
    float Radius, Uint8 CornerFlags, SDL_FColor Color,
    float *DragX, float *DragY)
{
    bool Hovered = (MouseX >= X && MouseX <= X + W && MouseY >= Y && MouseY <= Y + H);
    if (Hovered)
    {
        HotID = ID;
    }

    if (ActiveID == ID)
    {
        *DragX = MouseX - DragOffsetX;
        *DragY = MouseY - DragOffsetY;

        X = *DragX;
        Y = *DragY;

        HotID = ID;

        if (!MouseDown)
        {
            ActiveID = 0;
        }
    }
    else if (HotID == ID)
    {
        if (MouseDown)
        {
            ActiveID = ID;
            DragOffsetX = MouseX - X;
            DragOffsetY = MouseY - Y;
        }
    }

    UIBox(X, Y, W, H, Radius, CornerFlags, Color);

    return false;
}

static void UITriangle(float X, float Y, float W, float H, SDL_FColor Color)
{
    Assert((unsigned int)DrawCommandCount < ArrayCount(DrawCommands));
    Assert(Vertices + 3 < (int)(16 * MB / sizeof(struct Vertex)));

    DrawCommands[DrawCommandCount++] = (struct UIDrawCommand){
        .Texture = MagicPixel,
        .VertexOffset = Vertices,
        .IndexOffset = Indices,
        .NumIndices = 3,
        .X = 0, .Y = 0,
        .W = W, .H = H,
        .Radius = 0.0f,
        .Mode = 0.0f,
        .CornerFlags = 0,
    };

    float PointY, BaseY;
    if (H >= 0.0f)
    {
        PointY = -Y;
        BaseY  = -(Y + H);
    }
    else
    {
        PointY = -(Y - H);
        BaseY  = -Y;
    }

    VertexDestination[Vertices + 0] = (struct Vertex){ X + (W * 0.5f), PointY, 0, Color.r, Color.g, Color.b, Color.a, 0.5f, (H >= 0.0f) ? 0.0f : 1.0f };
    VertexDestination[Vertices + 1] = (struct Vertex){ X + W,          BaseY,  0, Color.r, Color.g, Color.b, Color.a, 1.0f, (H >= 0.0f) ? 1.0f : 0.0f };
    VertexDestination[Vertices + 2] = (struct Vertex){ X,              BaseY,  0, Color.r, Color.g, Color.b, Color.a, 0.0f, (H >= 0.0f) ? 1.0f : 0.0f };

    IndexDestination[Indices + 0] = 0;
    IndexDestination[Indices + 1] = 1;
    IndexDestination[Indices + 2] = 2;

    Vertices += 3;
    Indices += 3;
}

static bool UIClickableBox(const void *ID, float X, float Y, float W, float H, float Radius, Uint8 CornerFlags, SDL_FColor Color)
{
	bool Result = false;

	bool Hovered = (MouseX >= X && MouseX <= X + W && MouseY >= Y && MouseY <= Y + H);
	if (Hovered)
	{
		HotID = ID;
	}

	if (ActiveID == ID)
	{
		if (!MouseDown)
		{
			if (Hovered)
			{
				Result = true;
			}

			ActiveID = 0;
		}
	} 
	else if (HotID == ID)
	{
		if (MouseDown)
		{
			ActiveID = ID;
		}
	}

	if (ActiveID == ID)
	{
		Color.r *= 0.5f; Color.g *= 0.5f; Color.b *= 0.5f;
	}
	else if (HotID == ID)
	{
		Color.r *= 0.8f; Color.g *= 0.8f; Color.b *= 0.8f;
	}

	UIBox(X, Y, W, H, Radius, CornerFlags, Color);

	return Result;
}

static void UIText(TTF_Font *Font, const char *String, float X, float Y, SDL_FColor Color)
{
    TTF_Text *Text = TTF_CreateText(TextEngine, Font, String, 0);

    TTF_SetTextColorFloat(Text, Color.r, Color.g, Color.b, Color.a);

    TTF_GPUAtlasDrawSequence *Sequence = TTF_GetGPUTextDrawData(Text);
    while (Sequence)
    {
        Assert((unsigned int)DrawCommandCount < ArrayCount(DrawCommands));
        Assert(Vertices + Sequence->num_vertices < (int)(16 * MB / sizeof(struct Vertex)));

		DrawCommands[DrawCommandCount++] = (struct UIDrawCommand){
		    .Texture = Sequence->atlas_texture,
		    .VertexOffset = Vertices,
		    .IndexOffset = Indices,
		    .NumIndices = Sequence->num_indices,
		    .X = X, .Y = Y,
		    .W = 0, .H = 0,
		    .Radius = 0,
		    .Mode = 0.0f,
		};

        for (int i = 0; i < Sequence->num_vertices; i++)
        {
            VertexDestination[Vertices + i] = (struct Vertex){
                Sequence->xy[i].x, Sequence->xy[i].y, 0.0f,
                Color.r, Color.g, Color.b, Color.a,
                Sequence->uv[i].x, Sequence->uv[i].y,
            };
        }
        for (int i = 0; i < Sequence->num_indices; i++)
        {
            IndexDestination[Indices + i] = Sequence->indices[i];
        }

        Vertices += Sequence->num_vertices;
        Indices += Sequence->num_indices;
        Sequence = Sequence->next;
    }

    TTF_DestroyText(Text);
}

static void UpdateAndDraw(void)
{
    static bool ShowPanel = false;
    static float X = 0, Y = 0;

    UIDraggableBox("Header ID", X, Y, 400, 30, 10.0f, CORNERFLAG_TOP, GRAY, &X, &Y);
    UIText(DebugFont, "Debug", X + 35, -Y - 5, BLACK);

    if (UIClickableBox("Toggle ID", X + 12, Y + 8, 12, 12, 0.0f, 0, TRANSPARENT))
    {
        ShowPanel = !ShowPanel;
    }

    if (ShowPanel) 
    {
        UITriangle(X + 12, Y + 8, 12, 12, BLACK);
    }
    else 
    {
        UITriangle(X + 12, Y + 8, 12, -12, BLACK);
    }

    if (ShowPanel)
    {
        UIBox(X, Y + 25, 400, 200, 10.0f, CORNERFLAG_BOTTOM, WHITE);
    }

    if (!MouseDown)
    {
        ActiveID = 0;
    }
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

		PipelineSetShader(Pipeline, "Shaders/UI.hlsl");
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

	SDL_GPUBuffer *UIVertexBuffer = SDL_CreateGPUBuffer(Device, &(SDL_GPUBufferCreateInfo){
		.usage = SDL_GPU_BUFFERUSAGE_VERTEX,
		.size = 16 * MB,
	});

	SDL_GPUBuffer *UIIndexBuffer = SDL_CreateGPUBuffer(Device, &(SDL_GPUBufferCreateInfo){
		.usage = SDL_GPU_BUFFERUSAGE_INDEX,
		.size = 16 * MB,
	});

	SDL_GPUTransferBuffer *UITransferBuffer = SDL_CreateGPUTransferBuffer(Device, &(SDL_GPUTransferBufferCreateInfo){
		.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
		.size = 16 * MB + 16 * MB,
	});

	SDL_GPUSampler *UISampler = SDL_CreateGPUSampler(Device, &(SDL_GPUSamplerCreateInfo){
		.min_filter = SDL_GPU_FILTER_LINEAR,
		.mag_filter = SDL_GPU_FILTER_LINEAR,
	});

	MagicPixel = CreateMagicPixel(Device);

	DebugFont = TTF_OpenFont("Assets/DebugFont.ttf", 16.0f);
	if (!DebugFont)
	{
		SDL_Log("%s", SDL_GetError());

		return 1;
	}

	TextEngine = TTF_CreateGPUTextEngine(Device);

	for (;;) 
	{
		SDL_Event Event;


		while (SDL_PollEvent(&Event))
		{
			switch (Event.type)
			{
			case SDL_EVENT_MOUSE_MOTION:
			{
				MouseX = Event.motion.x, MouseY = Event.motion.y;
			}
			break;

		    case SDL_EVENT_MOUSE_BUTTON_DOWN:
		    {
		    	if (Event.button.button == SDL_BUTTON_LEFT)
		    	{
		    		MouseDown = true;
		    	}
		    }
		    break;

			case SDL_EVENT_MOUSE_BUTTON_UP:
			{
				if (Event.button.button == SDL_BUTTON_LEFT)
				{
					MouseDown = false;
				}
			}
			break;
				
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

		Uint8 *TransferMap = SDL_MapGPUTransferBuffer(Device, UITransferBuffer, 1);
		
		VertexDestination = (struct Vertex *)TransferMap;
		IndexDestination = (Uint32 *)(TransferMap + 16 * MB);

		Vertices = 0, Indices = 0;
		DrawCommandCount = 0;
		HotID = 0;

		UpdateAndDraw();

		SDL_UnmapGPUTransferBuffer(Device, UITransferBuffer);
		
		if (Vertices > 0)
		{
			SDL_GPUCopyPass *CopyPass = SDL_BeginGPUCopyPass(CommandBuffer);
			SDL_UploadToGPUBuffer(CopyPass, 
				&(SDL_GPUTransferBufferLocation){ .transfer_buffer = UITransferBuffer, .offset = 0 }, 
				&(SDL_GPUBufferRegion){ .buffer = UIVertexBuffer, .offset = 0, .size = Vertices * sizeof(struct Vertex) }, true);
			
			SDL_UploadToGPUBuffer(CopyPass, 
				&(SDL_GPUTransferBufferLocation){ .transfer_buffer = UITransferBuffer, .offset = 16 * MB }, 
				&(SDL_GPUBufferRegion){ .buffer = UIIndexBuffer, .offset = 0, .size = Indices * sizeof(Uint32) }, true);

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
					.buffer = UIVertexBuffer,
					.offset = 0,
				}, 1);
				SDL_BindGPUIndexBuffer(RenderPass, &(SDL_GPUBufferBinding){
					.buffer = UIIndexBuffer,
					.offset = 0,
				}, SDL_GPU_INDEXELEMENTSIZE_32BIT);

				for (int i = 0; i < DrawCommandCount; i++)
				{
					struct UIDrawCommand DrawCommand = DrawCommands[i];

					struct Matrix4x4 Uniforms[3] = {
					    OrthographicMatrix(0.0f, 1280.0f, -720.0f, 0.0f, -1.0f, 1.0f),
					    TranslationMatrix(Vector3(DrawCommand.X, DrawCommand.Y, 0.0f)),
						{ .m = { { DrawCommand.W, DrawCommand.H, DrawCommand.Radius, DrawCommand.Mode }, { (float)DrawCommand.CornerFlags } } },
					};

					SDL_PushGPUVertexUniformData(CommandBuffer, 0, Uniforms, sizeof(Uniforms));

					SDL_BindGPUFragmentSamplers(RenderPass, 0, &(SDL_GPUTextureSamplerBinding){
						.texture = DrawCommand.Texture,
						.sampler = UISampler,
					}, 1);

					SDL_DrawGPUIndexedPrimitives(RenderPass, DrawCommand.NumIndices, 1, DrawCommand.IndexOffset, DrawCommand.VertexOffset, 0);
				}
			}
			SDL_EndGPURenderPass(RenderPass);
		}

		SDL_SubmitGPUCommandBuffer(CommandBuffer);
	}
}