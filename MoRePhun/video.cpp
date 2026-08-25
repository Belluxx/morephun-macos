#include "video.h"
#include <iostream>

Video::Video()
{
	int rendererFlags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC |
		SDL_RENDERER_TARGETTEXTURE;
	int windowFlags = 0;

	if (SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		std::cout << "Couldn't initialize SDL: " << SDL_GetError() << std::endl;
		exit(1);
	}

	app.window = SDL_CreateWindow("V-Rally 2 — MoRePhun", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		SCREEN_WIDTH * WINDOW_SCALE, SCREEN_HEIGHT * WINDOW_SCALE, windowFlags);

	if (!app.window)
	{
		std::cout << "Failed to open " << SCREEN_WIDTH << "x" << SCREEN_HEIGHT <<" window: " << SDL_GetError() << std::endl;
		exit(1);
	}

	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

	app.renderer = SDL_CreateRenderer(app.window, -1, rendererFlags);
	if (!app.renderer)
		app.renderer = SDL_CreateRenderer(app.window, -1,
			SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE);

	if (!app.renderer)
	{
		std::cout << "Failed to create renderer: " << SDL_GetError() << std::endl;
		exit(1);
	}

	SDL_RenderSetLogicalSize(app.renderer, SCREEN_WIDTH, SCREEN_HEIGHT);
	SDL_RenderSetIntegerScale(app.renderer, SDL_TRUE);

	app.framebuffer = SDL_CreateTexture(app.renderer, SDL_PIXELFORMAT_RGBA8888,
		SDL_TEXTUREACCESS_TARGET, SCREEN_WIDTH, SCREEN_HEIGHT);
	if (!app.framebuffer)
	{
		std::cout << "Failed to create framebuffer: " << SDL_GetError() << std::endl;
		exit(1);
	}

	SDL_SetTextureBlendMode(app.framebuffer, SDL_BLENDMODE_NONE);
	SDL_SetRenderTarget(app.renderer, app.framebuffer);
	SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 255);
	SDL_RenderClear(app.renderer);
}

Video::~Video()
{
	SDL_DestroyTexture(app.framebuffer);
	SDL_DestroyRenderer(app.renderer);
	SDL_DestroyWindow(app.window);
	SDL_Quit();
}

void Video::present(const char* screenshotPath)
{
	SDL_Rect drawingClip{};
	const SDL_bool drawingClipEnabled = SDL_RenderIsClipEnabled(app.renderer);
	if (drawingClipEnabled)
		SDL_RenderGetClipRect(app.renderer, &drawingClip);

	SDL_SetRenderTarget(app.renderer, nullptr);
	SDL_RenderSetClipRect(app.renderer, nullptr);
	SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 255);
	SDL_RenderClear(app.renderer);
	SDL_RenderCopy(app.renderer, app.framebuffer, nullptr, nullptr);

	if (screenshotPath != nullptr)
	{
		int outputWidth = 0;
		int outputHeight = 0;
		SDL_GetRendererOutputSize(app.renderer, &outputWidth, &outputHeight);
		SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, outputWidth, outputHeight,
			32, SDL_PIXELFORMAT_RGBA32);
		if (surface != nullptr)
		{
			SDL_Rect frame = {0, 0, outputWidth, outputHeight};
			if (SDL_RenderReadPixels(app.renderer, &frame, surface->format->format,
				surface->pixels, surface->pitch) == 0)
				SDL_SaveBMP(surface, screenshotPath);
			SDL_FreeSurface(surface);
		}
	}

	SDL_RenderPresent(app.renderer);
	SDL_SetRenderTarget(app.renderer, app.framebuffer);
	SDL_RenderSetClipRect(app.renderer, drawingClipEnabled ? &drawingClip : nullptr);
}
