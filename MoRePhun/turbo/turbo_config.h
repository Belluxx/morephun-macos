#pragma once

#include <cstdint>
#include <string>

// Every tunable of the turbo feature lives here. Defaults target the RC14EU
// V-Rally 2 release. Each numeric/boolean field can be overridden at runtime
// through an environment variable named MOPHUN_TURBO_<NAME> where NAME is the
// upper-snake-case field name (e.g. MOPHUN_TURBO_MUSIC_DROP_OFFSET=128.3).
struct TurboConfig {
	// --- Meter -----------------------------------------------------------------
	float maxCharge = 100.0f;
	// Guest speed (distance units per game frame) that counts as "full speed".
	// The Speed Coupe cruises at ~45000 on tarmac; other cars reach ~53000.
	float speedReference = 48000.0f;
	float lowSpeedThreshold = 0.40f;
	float highSpeedThreshold = 0.70f;
	float maxSpeedThreshold = 0.90f;
	float chargeRateHigh = 4.0f;        // % per second in the high band
	float chargeRateMax = 9.0f;         // % per second at/above the max threshold
	float decayRateLow = 8.0f;          // % per second below the low threshold
	float reverseDecayRate = 25.0f;     // % per second while reversing
	float brakeDecayRate = 5.0f;        // % per second while braking (DOWN held)
	float offroadDecayRate = 14.0f;     // % per second while off the road
	float collisionPenalty = 30.0f;     // instantaneous % lost on impact
	float collisionSpeedDropRatio = 0.20f; // speed lost in one frame (of speedReference) that reads as an impact
	float collisionCooldown = 1.0f;     // seconds between collision penalties
	float meterSmoothing = 6.0f;        // display smoothing (1/s)
	// The game's own surface handling caps the target speed off-road
	// (~36000 for the Speed Coupe, ~20000 for the Hot Hatch); above this value
	// the car is considered on the road.
	uint32_t offroadTargetThreshold = 40000;
	float minimumActivationCharge = 100.0f;

	// --- Turbo gameplay ----------------------------------------------------------
	float turboDuration = 8.0f;         // seconds of full boost
	float turboExpireDuration = 1.5f;   // seconds to fade back to normal
	float turboSpeedMultiplier = 1.75f; // applied to the game's own target speed

	// --- Cinematic ---------------------------------------------------------------
	float cinematicTimeScale = 0.06f;   // world slow-motion factor for shots 2-9
	float activationTimeScale = 0.18f;  // game clock factor during shot 1
	float shotFrameRate = 60.0f;        // presentation rate while the game is paused
	float cinematicBeats = 32.0f;       // beats between music start and drop

	// --- Music -------------------------------------------------------------------
	float musicStartOffset = 109.0f;    // seconds into the song where the sequence starts
	float musicDropOffset = 128.28f;    // seconds into the song of the drop
	float musicVolume = 0.9f;
	float sfxVolume = 0.8f;
	float audioLatencyCompensation = 0.0f; // seconds added to the audio clock
	float musicFadeOutDuration = 1.5f;  // fade when turbo ends
	std::string musicPath;              // explicit file; otherwise turbo_music.mp3 is searched

	// --- Guest memory map (V-Rally 2 [RC14EU]) -------------------------------------
	uint32_t carStructAddress = 0x13dfc;
	uint32_t roadBandsPerRaceFrame = 12; // vFillRect calls per frame that mean "racing"

	// --- Development -----------------------------------------------------------------
	bool disabled = false;
	bool debugOverlay = false;
	bool skipCinematic = false;
	bool devInstantCharge = false;      // meter starts full and refills instantly
	uint32_t devTriggerFrame = 0;       // auto-activate at this game frame (headless tests)
	std::string devShotDirectory;       // dump cinematic frames as BMP here
	uint32_t devShotEvery = 6;
};

TurboConfig loadTurboConfig();
