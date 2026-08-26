// Renders frames of the turbo cinematic to BMP files without running the
// game, for tuning shots. Usage: TurboPreview <output-dir> [step-seconds|t1,t2,...]
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "turbo/cinematic.h"
#include "turbo/turbo_config.h"

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		std::cerr << "Usage: " << argv[0] << " <output-dir> [step-seconds|t1,t2,...]" << std::endl;
		return 2;
	}
	const std::string outputDirectory = argv[1];
	SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
	if (SDL_Init(SDL_INIT_VIDEO) != 0)
	{
		std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
		return 1;
	}
	SDL_Window* window = SDL_CreateWindow("preview", 0, 0, 128, 160, SDL_WINDOW_HIDDEN);
	SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE);
	SDL_Texture* target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 128, 160);
	SDL_Texture* snapshot = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 128, 160);
	// Stand-in game frame: sky, mountains, ground bands, road and a car block.
	SDL_SetRenderTarget(renderer, snapshot);
	SDL_SetRenderDrawColor(renderer, 150, 190, 245, 255);
	SDL_RenderClear(renderer);
	SDL_Rect ground = {0, 70, 128, 90};
	SDL_SetRenderDrawColor(renderer, 150, 102, 20, 255);
	SDL_RenderFillRect(renderer, &ground);
	SDL_Rect road = {30, 70, 68, 90};
	SDL_SetRenderDrawColor(renderer, 160, 160, 176, 255);
	SDL_RenderFillRect(renderer, &road);
	SDL_Rect carBox = {46, 115, 36, 36};
	SDL_SetRenderDrawColor(renderer, 230, 230, 230, 255);
	SDL_RenderFillRect(renderer, &carBox);

	TurboConfig config = loadTurboConfig();
	Cinematic cinematic(config, renderer, 128, 160);
	CinematicPalette palette;
	cinematic.begin(palette, carBox);

	std::vector<double> times;
	if (argc >= 3 && std::string(argv[2]).find(',') != std::string::npos)
	{
		std::istringstream list(argv[2]);
		std::string item;
		while (std::getline(list, item, ','))
			times.push_back(std::atof(item.c_str()));
	}
	else
	{
		const double step = argc >= 3 ? std::atof(argv[2]) : 0.25;
		for (double t = 0.0; t <= cinematic.dropTime() + 0.001; t += step)
			times.push_back(t);
	}
	// Advance the cinematic in small steps so slow-motion state stays continuous.
	double t = 0.0;
	int index = 0;
	for (double wanted : times)
	{
		while (t < wanted - 1e-6)
		{
			t = std::min(wanted, t + 1.0 / 60.0);
			SDL_SetRenderTarget(renderer, target);
			cinematic.render(t, snapshot);
		}
		SDL_SetRenderTarget(renderer, target);
		const CinematicFrame frame = cinematic.render(wanted, snapshot);
		SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, 128, 160, 32, SDL_PIXELFORMAT_RGBA32);
		SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA32, surface->pixels, surface->pitch);
		char name[256];
		std::snprintf(name, sizeof(name), "%s/t%06.2f_%s.bmp", outputDirectory.c_str(), wanted, frame.shotName);
		SDL_SaveBMP(surface, name);
		SDL_FreeSurface(surface);
		++index;
	}
	std::cout << "Rendered " << index << " frames" << std::endl;
	SDL_DestroyTexture(snapshot);
	SDL_DestroyTexture(target);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
