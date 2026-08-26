#pragma once

#include <SDL.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "cinematic.h"
#include "game_probe.h"
#include "turbo_audio.h"
#include "turbo_config.h"

class MophunVM;
class Video;
struct SPRITE;

// Host-side turbo mechanic layered on top of the emulated V-Rally 2.
//
// The guest game keeps running unmodified; this class observes its car state
// through VRally2Probe, draws the meter and boost effects over the presented
// frame, feeds the game a scalable clock for slow motion, blocks input during
// the cinematic and pokes the game's target speed while the boost is active.
class TurboSystem {
	public:
		TurboSystem(MophunVM& vm, Video& video, const TurboConfig& config);
		~TurboSystem();

		TurboSystem(const TurboSystem&) = delete;
		TurboSystem& operator=(const TurboSystem&) = delete;

		bool enabled() const { return !config.disabled; }
		// Directory that may contain turbo_music.mp3 (the game's asset folder).
		void setAssetDirectory(const std::string& directory);

		// --- Hooks called by the syscall layer -----------------------------------------
		uint32_t filterInput(uint32_t keys);          // vGetButtonData
		uint32_t guestTicks();                        // vGetTickCount (virtual clock)
		void onFillRect();                            // vFillRect
		void onDrawObject(int x, int y, const SPRITE* sprite); // vDrawObject
		void onFlip(const char* screenshotPath);      // vFlipScreen (presents the frame)

	private:
		enum class State { Normal, Ready, Cinematic, Active, Expiring };

		struct Particle {
			float x, y, vx, vy, life, maxLife, size;
			Rgb color;
		};

		TurboConfig config;
		MophunVM& vm;
		Video& video;
		VRally2Probe probe;
		TurboAudio audio;
		Cinematic cinematic;
		std::string assetDirectory;
		bool musicSearched = false;

		State state = State::Normal;
		uint32_t frameIndex = 0;
		uint32_t fillRectsThisFrame = 0;
		bool inRace = false;
		CarSnapshot car;
		CarSnapshot previousCar;
		bool haveCarRect = false;
		SDL_Rect carRect{46, 115, 36, 36};
		SDL_Rect frameCarRect{0, 0, 0, 0};
		bool frameCarRectSeen = false;

		// Meter.
		float charge = 0.0f;
		float displayCharge = 0.0f;
		float collisionTimer = 0.0f;
		float collisionFlash = 0.0f;
		bool braking = false;
		bool offroad = false;
		bool reversing = false;
		float speedRatio = 0.0f;
		uint32_t lastKeys = 0;
		uint32_t rawKeys = 0;
		bool upReleasedSinceReady = false;
		bool pendingActivation = false;
		uint32_t lastPokeFrame = 0xffffffffU;

		// Clock.
		double virtualMs = 0.0;
		double lastRealSeconds = 0.0;
		double timeScale = 1.0;
		double lastPresentSeconds = 0.0;
		double lastDevPollSeconds = 0.0;

		// Cinematic.
		double cinematicStartSeconds = 0.0;
		double lastCinematicTime = 0.0;
		CinematicFrame cinematicFrame;
		bool skipRequested = false;
		uint32_t cinematicFramesRendered = 0;
		CinematicPalette palette;

		// Turbo gameplay.
		float turboElapsed = 0.0f;
		float effectIntensity = 0.0f;
		double activeStartSeconds = 0.0;
		std::vector<Particle> particles;
		float streakPhase[10];
		float streakAngle[10];
		uint32_t effectNoise = 0x1234567U;

		bool debugOverlay = false;
		bool devKeyState[16] = {};

		double now() const;
		void pump();
		void updateMeter(float dt);
		void enterState(State next);
		void startCinematic();
		void renderCinematicFrame(const char* screenshotPath);
		void endCinematic();
		void ensureMusic();
		void samplePalette();
		void resetForNewRace();
		float boostMultiplier() const;
		float randomUnit();

		void drawHud();
		void drawEffects(float intensity, double seconds);
		void updateParticles(float dt);
		void presentComposite(const char* screenshotPath, bool effects);
		void drawDebugOverlay();
		void drawText(int x, int y, const std::string& text, Rgb color);
		void pollDevKeys();
};
