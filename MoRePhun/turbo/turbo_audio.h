#pragma once

#include <cstdint>
#include <memory>
#include <string>

struct TurboConfig;

enum class TurboSfx {
	ExhaustPulse,
	CoverClick,
	ButtonClick,
	Ignition,
	Whoosh,
	WheelCreak,
	Thump
};

enum class TurboLoop {
	EngineRumble,
	TurboEngine,
	AirShockwave
};

// Host audio for the turbo feature: streams a decoded window of the music
// track through an SDL audio device, exposes a sample-accurate playback clock
// for cinematic synchronization, and synthesizes the handful of retro sound
// effects procedurally so no extra assets are needed.
class TurboAudio {
	public:
		explicit TurboAudio(const TurboConfig& config);
		~TurboAudio();

		TurboAudio(const TurboAudio&) = delete;
		TurboAudio& operator=(const TurboAudio&) = delete;

		bool deviceAvailable() const;
		bool loadMusic(const std::string& path, std::string& error);
		bool musicLoaded() const;

		// Starts the track at TurboConfig::musicStartOffset.
		void startMusic();
		void fadeOutMusic(float seconds);
		void stopMusic();
		bool musicPlaying() const;
		// Seconds into the original song currently reaching the listener, or
		// a wall-clock estimate when no device is available.
		double musicPosition() const;

		void playSfx(TurboSfx effect, float gain = 1.0f);
		void setLoop(TurboLoop loop, float level);
		void stopAllLoops();

	private:
		struct Impl;
		std::unique_ptr<Impl> impl;
};
