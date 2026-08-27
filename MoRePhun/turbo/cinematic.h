#pragma once

#include <SDL.h>
#include <functional>
#include <vector>

#include "retro3d.h"
#include "turbo_audio.h"

struct TurboConfig;

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

struct CinematicFrame {
	int shotIndex = 0;
	const char* shotName = "";
	float gameTimeScale = 0.0f; // clock factor the guest should run at right now
	float gameBlend = 0.0f;     // 1 = the presented image is the live game frame
	bool finished = false;      // the drop has been reached
};

// The turbo activation sequence. Time is expressed in seconds since the music
// started; the drop lands at dropTime(). Shots are laid out in beats so that
// changing the drop offset retimes the whole sequence.
class Cinematic {
	public:
		Cinematic(const TurboConfig& config, SDL_Renderer* renderer, int width, int height);

		void begin(const CinematicPalette& palette, const SDL_Rect& carRect);
		double dropTime() const;
		double beatLength() const;
		int shotCount() const;
		// The embedded guest sequence has no live SDL game snapshot.  It starts at
		// the first fully 3D shot and stretches shots 2-11 across the music build.
		void setGuestExport(bool enabled) { guestExport = enabled; }
		void setTriangleSink(std::function<void(const RetroScreenTriangle&)> sink)
		{
			retro.setTriangleSink(std::move(sink));
		}
		// Renders the frame for time t into the current render target. The game
		// snapshot is used by the first and last shots.
		CinematicFrame render(double t, SDL_Texture* gameSnapshot);

		void setSfxHandler(std::function<void(TurboSfx, float)> handler) { sfxHandler = handler; }
		void setLoopHandler(std::function<void(TurboLoop, float)> handler) { loopHandler = handler; }

	private:
		struct Shot {
			const char* name;
			float startBeat;
			float endBeat;
		};
		struct SoundCue {
			float beat;
			TurboSfx effect;
			float gain;
		};
		const TurboConfig& config;
		SDL_Renderer* renderer;
		int width;
		int height;
		RetroRenderer retro;
		CinematicPalette palette;
		SDL_Rect carRect{0, 0, 0, 0};
		std::vector<Shot> shots;
		std::vector<SoundCue> cues;
		std::function<void(TurboSfx, float)> sfxHandler;
		std::function<void(TurboLoop, float)> loopHandler;

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
		int nextPulse = 0;
		bool guestExport = false;

		void buildModels();
		float worldScale(int shot, float u) const;
		int shotAt(float beat, float& u) const;
		void fireCues(float previousBeat, float beat);

		void drawGameShot(SDL_Texture* snapshot, float u, float beat);
		void drawWorld(float beat, bool interiorView);
		void drawCar(float beat, bool withRearGlass);
		void drawInterior(float beat);
		void drawDriver(float beat);
		void drawSmoke();
		Camera cameraFor(int shot, float u, float beat) const;
		void animationParameters(float beat, float& thumb, float& cover, float& press, float& smirk,
			float& headYaw, float& brow) const;
};
