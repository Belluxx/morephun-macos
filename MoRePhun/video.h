#pragma once
#include <SDL.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 160
#define WINDOW_SCALE 3

struct Renderer{
	SDL_Renderer *renderer = nullptr;
	SDL_Window *window = nullptr;
	SDL_Texture *framebuffer = nullptr;
	// Copy of the most recently presented game frame; lets host-side overlays
	// re-present a stable image while the guest is still drawing the next one.
	SDL_Texture *snapshot = nullptr;
	// Scratch target for composited host frames (cinematics, effect passes).
	SDL_Texture *composite = nullptr;
};


class Video {
	public:
		Video();
		~Video();
		// Presents `source` (the guest framebuffer by default) to the window.
		void present(const char* screenshotPath, SDL_Texture* source = nullptr);
		// Copies the guest framebuffer into the snapshot texture.
		void captureSnapshot();
		// Switch the render target without losing the guest's clip window; call
		// restoreGuestTarget() afterwards.
		void beginHostTarget(SDL_Texture* target);
		void restoreGuestTarget();
		Renderer app;

	private:
		SDL_Rect savedClip{};
		bool savedClipEnabled = false;
};
