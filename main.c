#include <SDL3/SDL.h>

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

	SDL_GPUDevice *Device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, 0, 0);
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
		if (!RenderPass)
		{
			SDL_EndGPURenderPass(RenderPass);
		}

		SDL_SubmitGPUCommandBuffer(CommandBuffer);
	}
}