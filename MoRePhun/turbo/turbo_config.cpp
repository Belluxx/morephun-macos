#include "turbo_config.h"

#include <cstdlib>
#include <cstring>

namespace {

const char* envValue(const char* name)
{
	const char* value = std::getenv(name);
	return value != nullptr && value[0] != '\0' ? value : nullptr;
}

void readFloat(const char* name, float& field)
{
	if (const char* value = envValue(name))
		field = static_cast<float>(std::atof(value));
}

void readUnsigned(const char* name, uint32_t& field)
{
	if (const char* value = envValue(name))
		field = static_cast<uint32_t>(std::strtoul(value, nullptr, 0));
}

void readBool(const char* name, bool& field)
{
	if (const char* value = envValue(name))
		field = std::strcmp(value, "0") != 0 && std::strcmp(value, "false") != 0;
}

void readString(const char* name, std::string& field)
{
	if (const char* value = envValue(name))
		field = value;
}

} // namespace

TurboConfig loadTurboConfig()
{
	TurboConfig config;
	readFloat("MOPHUN_TURBO_MAX_CHARGE", config.maxCharge);
	readFloat("MOPHUN_TURBO_SPEED_REFERENCE", config.speedReference);
	readFloat("MOPHUN_TURBO_LOW_SPEED_THRESHOLD", config.lowSpeedThreshold);
	readFloat("MOPHUN_TURBO_HIGH_SPEED_THRESHOLD", config.highSpeedThreshold);
	readFloat("MOPHUN_TURBO_MAX_SPEED_THRESHOLD", config.maxSpeedThreshold);
	readFloat("MOPHUN_TURBO_CHARGE_RATE_HIGH", config.chargeRateHigh);
	readFloat("MOPHUN_TURBO_CHARGE_RATE_MAX", config.chargeRateMax);
	readFloat("MOPHUN_TURBO_DECAY_RATE_LOW", config.decayRateLow);
	readFloat("MOPHUN_TURBO_REVERSE_DECAY_RATE", config.reverseDecayRate);
	readFloat("MOPHUN_TURBO_BRAKE_DECAY_RATE", config.brakeDecayRate);
	readFloat("MOPHUN_TURBO_OFFROAD_DECAY_RATE", config.offroadDecayRate);
	readFloat("MOPHUN_TURBO_COLLISION_PENALTY", config.collisionPenalty);
	readFloat("MOPHUN_TURBO_COLLISION_SPEED_DROP_RATIO", config.collisionSpeedDropRatio);
	readFloat("MOPHUN_TURBO_COLLISION_COOLDOWN", config.collisionCooldown);
	readFloat("MOPHUN_TURBO_METER_SMOOTHING", config.meterSmoothing);
	readUnsigned("MOPHUN_TURBO_OFFROAD_TARGET_THRESHOLD", config.offroadTargetThreshold);
	readFloat("MOPHUN_TURBO_MINIMUM_ACTIVATION_CHARGE", config.minimumActivationCharge);
	readFloat("MOPHUN_TURBO_DURATION", config.turboDuration);
	readFloat("MOPHUN_TURBO_EXPIRE_DURATION", config.turboExpireDuration);
	readFloat("MOPHUN_TURBO_SPEED_MULTIPLIER", config.turboSpeedMultiplier);
	readFloat("MOPHUN_TURBO_CINEMATIC_TIME_SCALE", config.cinematicTimeScale);
	readFloat("MOPHUN_TURBO_ACTIVATION_TIME_SCALE", config.activationTimeScale);
	readFloat("MOPHUN_TURBO_SHOT_FRAME_RATE", config.shotFrameRate);
	readFloat("MOPHUN_TURBO_CINEMATIC_BEATS", config.cinematicBeats);
	readFloat("MOPHUN_TURBO_MUSIC_START_OFFSET", config.musicStartOffset);
	readFloat("MOPHUN_TURBO_MUSIC_DROP_OFFSET", config.musicDropOffset);
	readFloat("MOPHUN_TURBO_MUSIC_VOLUME", config.musicVolume);
	readFloat("MOPHUN_TURBO_SFX_VOLUME", config.sfxVolume);
	readFloat("MOPHUN_TURBO_AUDIO_LATENCY_COMPENSATION", config.audioLatencyCompensation);
	readFloat("MOPHUN_TURBO_MUSIC_FADE_OUT_DURATION", config.musicFadeOutDuration);
	readString("MOPHUN_TURBO_MUSIC", config.musicPath);
	readUnsigned("MOPHUN_TURBO_CAR_STRUCT_ADDRESS", config.carStructAddress);
	readUnsigned("MOPHUN_TURBO_ROAD_BANDS_PER_RACE_FRAME", config.roadBandsPerRaceFrame);
	readBool("MOPHUN_TURBO_DISABLED", config.disabled);
	readBool("MOPHUN_TURBO_DEBUG", config.debugOverlay);
	readBool("MOPHUN_TURBO_SKIP_CINEMATIC", config.skipCinematic);
	readBool("MOPHUN_TURBO_DEV_INSTANT_CHARGE", config.devInstantCharge);
	readUnsigned("MOPHUN_TURBO_DEV_TRIGGER_FRAME", config.devTriggerFrame);
	readString("MOPHUN_TURBO_DEV_SHOT_DIRECTORY", config.devShotDirectory);
	readUnsigned("MOPHUN_TURBO_DEV_SHOT_EVERY", config.devShotEvery);
	return config;
}
