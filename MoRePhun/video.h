#pragma once
#include <SDL.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 160
#define WINDOW_SCALE 3

struct Renderer{
	SDL_Renderer *renderer = nullptr;
	SDL_Window *window = nullptr;
	SDL_Texture *framebuffer = nullptr;
};


class Video {
	public:
		Video();
		~Video();
		void present(const char* screenshotPath);
		Renderer app;
};
