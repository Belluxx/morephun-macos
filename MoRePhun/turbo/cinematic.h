#pragma once

#include <SDL.h>
#include <functional>
#include <vector>

#include "retro3d.h"

// Colours sampled from the live game frame so the 3D shots match the track.
struct CinematicPalette {
	Rgb sky{140, 180, 240};
	Rgb horizon{190, 215, 250};
	Rgb ground{160, 110, 20};
	Rgb groundAlt{130, 88, 12};
	Rgb road{160, 160, 176};
	Rgb roadAlt{176, 176, 192};
	Rgb mountain{120, 140, 200};
};

// Offline 3D frame generator used by VRally2TurboMod. It has no access to the
// running game or emulator state; rendered triangles are serialized into the MPN.
class Cinematic {
	public:
		Cinematic(double durationSeconds, SDL_Renderer* renderer, int width, int height);

		void begin(const CinematicPalette& palette);
		void setTriangleSink(std::function<void(const RetroScreenTriangle&)> sink)
		{
			retro.setTriangleSink(std::move(sink));
		}
		void render(double timeSeconds);

	private:
		struct Shot {
			float startBeat;
			float endBeat;
		};
		double durationSeconds;
		SDL_Renderer* renderer;
		RetroRenderer retro;
		CinematicPalette palette;
		std::vector<Shot> shots;

		// Static geometry (car-local space, +Z forward, +Y up, +X right).
		Mesh carBody;
		Mesh rearGlass;
		Mesh wheel;
		Mesh interior;
		Mesh driverBody;
		Mesh steeringWheel;
		Mesh tree;
		Mesh groundBands;

		double lastTime = 0.0;
		double worldTime = 0.0;
		float lastBeat = -1.0f;
		std::vector<double> smokePuffs;

		void buildModels();
		float worldScale(int shot, float u) const;
		int shotAt(float beat, float& u) const;
		void updateSmokePuffs(float previousBeat, float beat);

		void drawWorld(float beat, bool interiorView);
		void drawCar(float beat, bool withRearGlass);
		void drawInterior(float beat);
		void drawDriver(float beat);
		void drawSmoke();
		Camera cameraFor(int shot, float u, float beat) const;
		void animationParameters(float beat, float& thumb, float& cover, float& press, float& smirk,
			float& headYaw, float& brow) const;
};
