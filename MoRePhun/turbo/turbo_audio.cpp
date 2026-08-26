#include "turbo_audio.h"
#include "turbo_config.h"

#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#ifdef __APPLE__
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace {

constexpr int DeviceRate = 44100;
constexpr int DeviceChannels = 2;
constexpr int DeviceSamples = 1024;
constexpr double Pi = 3.14159265358979323846;

struct Voice {
	TurboSfx type = TurboSfx::Thump;
	bool active = false;
	double t = 0.0;
	float gain = 1.0f;
	float phase = 0.0f;
	float low = 0.0f;
	uint32_t noise = 0x9e3779b9U;

	float rand()
	{
		noise = noise * 1664525U + 1013904223U;
		return static_cast<float>(noise >> 8) / 8388608.0f - 1.0f;
	}
};

struct LoopVoice {
	float level = 0.0f;
	float target = 0.0f;
	double t = 0.0;
	float phase = 0.0f;
	float phase2 = 0.0f;
	float low = 0.0f;
	float band = 0.0f;
	uint32_t noise = 0x12345678U;

	float rand()
	{
		noise = noise * 1664525U + 1013904223U;
		return static_cast<float>(noise >> 8) / 8388608.0f - 1.0f;
	}
};

float renderVoice(Voice& voice, double dt)
{
	const double t = voice.t;
	float sample = 0.0f;
	switch (voice.type)
	{
		case TurboSfx::ExhaustPulse:
		{
			if (t > 0.35) { voice.active = false; break; }
			const float env = static_cast<float>(std::exp(-t / 0.07));
			voice.low += 0.12f * (voice.rand() - voice.low);
			voice.phase += static_cast<float>(2.0 * Pi * 70.0 * dt);
			sample = env * (voice.low * 1.6f + 0.5f * std::sin(voice.phase) * static_cast<float>(std::exp(-t / 0.05)));
			break;
		}
		case TurboSfx::CoverClick:
		{
			if (t > 0.12) { voice.active = false; break; }
			const float burst = t < 0.012 ? voice.rand() * 0.9f : 0.0f;
			voice.phase += static_cast<float>(2.0 * Pi * 2600.0 * dt);
			const float ping = std::sin(voice.phase) * static_cast<float>(std::exp(-t / 0.03)) * 0.5f;
			sample = burst + ping;
			break;
		}
		case TurboSfx::ButtonClick:
		{
			if (t > 0.16) { voice.active = false; break; }
			voice.phase += static_cast<float>(2.0 * Pi * 1100.0 * dt);
			const float blip = (std::fmod(voice.phase, static_cast<float>(2.0 * Pi)) < Pi ? 0.6f : -0.6f)
				* static_cast<float>(std::exp(-t / 0.02));

			voice.low += 0.05f * (voice.rand() - voice.low);
			const float thump = std::sin(static_cast<float>(2.0 * Pi * 95.0 * t)) * static_cast<float>(std::exp(-t / 0.05)) * 0.8f;
			sample = blip + thump + voice.low * 0.3f * static_cast<float>(std::exp(-t / 0.01));
			break;
		}
		case TurboSfx::Ignition:
		{
			if (t > 1.4) { voice.active = false; break; }
			const float brightness = static_cast<float>(std::min(1.0, 0.05 + t * 0.9));
			voice.low += brightness * 0.6f * (voice.rand() - voice.low);
			const double frequency = 160.0 * std::exp(-t * 1.6) + 42.0;
			voice.phase += static_cast<float>(2.0 * Pi * frequency * dt);
			const float env = t < 0.08 ? static_cast<float>(t / 0.08) : static_cast<float>(std::exp(-(t - 0.08) / 0.55));
			sample = env * (voice.low * 1.3f + std::sin(voice.phase) * 0.9f);
			break;
		}
		case TurboSfx::Whoosh:
		{
			if (t > 1.1) { voice.active = false; break; }
			const float brightness = static_cast<float>(0.02 + t * 0.5);
			voice.low += brightness * (voice.rand() - voice.low);
			const float env = static_cast<float>(std::sin(std::min(1.0, t / 1.1) * Pi));
			sample = env * voice.low * 1.8f;
			break;
		}
		case TurboSfx::WheelCreak:
		{
			if (t > 0.3) { voice.active = false; break; }
			const double vibrato = 1.0 + 0.08 * std::sin(2.0 * Pi * 23.0 * t);
			voice.phase += static_cast<float>(2.0 * Pi * 310.0 * vibrato * dt);
			sample = std::sin(voice.phase) * 0.35f * static_cast<float>(std::exp(-t / 0.12)) * (0.6f + 0.4f * voice.rand());
			break;
		}
		case TurboSfx::Thump:
		{
			if (t > 0.25) { voice.active = false; break; }
			voice.phase += static_cast<float>(2.0 * Pi * (60.0 + 40.0 * std::exp(-t / 0.03)) * dt);
			sample = std::sin(voice.phase) * static_cast<float>(std::exp(-t / 0.08));
			break;
		}
	}
	voice.t += dt;
	return sample * voice.gain;
}

float renderLoop(TurboLoop type, LoopVoice& loop, double dt)
{
	loop.level += static_cast<float>(std::min(1.0, dt * 6.0)) * (loop.target - loop.level);
	if (loop.level < 0.001f && loop.target <= 0.0f)
		return 0.0f;
	float sample = 0.0f;
	switch (type)
	{
		case TurboLoop::EngineRumble:
		{
			loop.phase += static_cast<float>(2.0 * Pi * 38.0 * dt);
			loop.phase2 += static_cast<float>(2.0 * Pi * 6.5 * dt);
			loop.low += 0.05f * (loop.rand() - loop.low);
			const float tremor = 0.75f + 0.25f * std::sin(loop.phase2);
			sample = (std::sin(loop.phase) * 0.55f + loop.low * 0.9f) * tremor;
			break;
		}
		case TurboLoop::TurboEngine:
		{
			const double rise = std::min(1.0, loop.t / 3.0);
			loop.phase += static_cast<float>(2.0 * Pi * (85.0 + 40.0 * rise) * dt);
			loop.phase2 += static_cast<float>(2.0 * Pi * (170.0 + 80.0 * rise) * dt);
			loop.low += 0.35f * (loop.rand() - loop.low);
			loop.band += 0.08f * (loop.low - loop.band);
			sample = std::sin(loop.phase) * 0.45f + std::sin(loop.phase2) * 0.2f + (loop.low - loop.band) * 0.9f;
			break;
		}
		case TurboLoop::AirShockwave:
		{
			loop.low += 0.5f * (loop.rand() - loop.low);
			loop.band += 0.02f * (loop.low - loop.band);
			loop.phase2 += static_cast<float>(2.0 * Pi * 0.9 * dt);
			sample = (loop.low - loop.band) * (0.6f + 0.4f * std::sin(loop.phase2));
			break;
		}
	}
	loop.t += dt;
	return sample * loop.level;
}

} // namespace

struct TurboAudio::Impl {
	const TurboConfig& config;
	SDL_AudioDeviceID device = 0;
	bool pacedDevice = false;     // false for SDL's dummy/disk drivers, which do not run in real time
	SDL_AudioSpec spec{};
	std::vector<float> music;          // interleaved stereo at DeviceRate
	double musicWindowStart = 0.0;     // song time of music[0]
	bool musicIsLoaded = false;
	bool playing = false;
	size_t musicPosition = 0;          // frames consumed by the callback
	float musicGain = 1.0f;
	float fadeStep = 0.0f;             // gain change per frame when fading
	uint64_t clockCounter = 0;         // SDL_GetPerformanceCounter at last callback
	size_t clockFrames = 0;            // musicPosition at last callback
	double fallbackStart = 0.0;
	uint64_t fallbackCounter = 0;
	Voice voices[12];
	LoopVoice loops[3];

	explicit Impl(const TurboConfig& config) : config(config) {}

	static void callback(void* userdata, Uint8* stream, int length)
	{
		static_cast<Impl*>(userdata)->mix(reinterpret_cast<float*>(stream), length / static_cast<int>(sizeof(float) * DeviceChannels));
	}

	void mix(float* out, int frames)
	{
		const double dt = 1.0 / DeviceRate;
		const float sfxGain = config.sfxVolume;
		for (int frame = 0; frame < frames; ++frame)
		{
			float left = 0.0f;
			float right = 0.0f;
			if (playing && musicIsLoaded)
			{
				if (musicPosition + 1 < music.size() / DeviceChannels)
				{
					left = music[musicPosition * 2] * musicGain * config.musicVolume;
					right = music[musicPosition * 2 + 1] * musicGain * config.musicVolume;
					++musicPosition;
				}
				else
					playing = false;
				if (fadeStep != 0.0f)
				{
					musicGain += fadeStep;
					if (musicGain <= 0.0f)
					{
						musicGain = 0.0f;
						fadeStep = 0.0f;
						playing = false;
					}
				}
			}
			float effects = 0.0f;
			for (Voice& voice : voices)
			{
				if (voice.active)
					effects += renderVoice(voice, dt);
			}
			effects += renderLoop(TurboLoop::EngineRumble, loops[0], dt);
			effects += renderLoop(TurboLoop::TurboEngine, loops[1], dt);
			effects += renderLoop(TurboLoop::AirShockwave, loops[2], dt);
			effects *= sfxGain;
			// Soft clip so stacked effects never crackle.
			effects = std::tanh(effects);
			out[frame * 2] = std::max(-1.0f, std::min(1.0f, left + effects));
			out[frame * 2 + 1] = std::max(-1.0f, std::min(1.0f, right + effects));
		}
		clockCounter = SDL_GetPerformanceCounter();
		clockFrames = musicPosition;
	}
};

TurboAudio::TurboAudio(const TurboConfig& config) : impl(new Impl(config))
{
	if (std::getenv("MOPHUN_DISABLE_AUDIO") != nullptr && std::getenv("MOPHUN_TURBO_FORCE_AUDIO") == nullptr)
		return;
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
	{
		std::cerr << "Turbo audio unavailable: " << SDL_GetError() << std::endl;
		return;
	}
	SDL_AudioSpec want{};
	want.freq = DeviceRate;
	want.format = AUDIO_F32SYS;
	want.channels = DeviceChannels;
	want.samples = DeviceSamples;
	want.callback = &Impl::callback;
	want.userdata = impl.get();
	impl->device = SDL_OpenAudioDevice(nullptr, 0, &want, &impl->spec, 0);
	if (impl->device == 0)
	{
		std::cerr << "Turbo audio device could not be opened: " << SDL_GetError() << std::endl;
		return;
	}
	const char* driver = SDL_GetCurrentAudioDriver();
	impl->pacedDevice = driver != nullptr && std::strcmp(driver, "dummy") != 0 && std::strcmp(driver, "disk") != 0;
	SDL_PauseAudioDevice(impl->device, 0);
}

TurboAudio::~TurboAudio()
{
	if (impl->device != 0)
		SDL_CloseAudioDevice(impl->device);
}

bool TurboAudio::deviceAvailable() const
{
	return impl->device != 0;
}

bool TurboAudio::musicLoaded() const
{
	return impl->musicIsLoaded;
}

bool TurboAudio::loadMusic(const std::string& path, std::string& error)
{
	error.clear();
	const double windowStart = std::max(0.0, static_cast<double>(impl->config.musicStartOffset) - 1.0);
	const double windowLength = 1.0 + (impl->config.musicDropOffset - impl->config.musicStartOffset)
		+ impl->config.turboDuration + impl->config.turboExpireDuration
		+ impl->config.musicFadeOutDuration + 30.0;
#ifdef __APPLE__
	CFURLRef url = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault,
		reinterpret_cast<const UInt8*>(path.c_str()), static_cast<CFIndex>(path.size()), false);
	if (url == nullptr)
	{
		error = "invalid path";
		return false;
	}
	ExtAudioFileRef file = nullptr;
	OSStatus status = ExtAudioFileOpenURL(url, &file);
	CFRelease(url);
	if (status != noErr || file == nullptr)
	{
		error = "ExtAudioFileOpenURL failed (" + std::to_string(status) + ")";
		return false;
	}
	AudioStreamBasicDescription fileFormat{};
	UInt32 propertySize = sizeof(fileFormat);
	status = ExtAudioFileGetProperty(file, kExtAudioFileProperty_FileDataFormat, &propertySize, &fileFormat);
	if (status != noErr)
	{
		ExtAudioFileDispose(file);
		error = "unable to read the file format";
		return false;
	}
	AudioStreamBasicDescription client{};
	client.mSampleRate = DeviceRate;
	client.mFormatID = kAudioFormatLinearPCM;
	client.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
	client.mBitsPerChannel = 32;
	client.mChannelsPerFrame = DeviceChannels;
	client.mBytesPerFrame = sizeof(float) * DeviceChannels;
	client.mFramesPerPacket = 1;
	client.mBytesPerPacket = client.mBytesPerFrame;
	status = ExtAudioFileSetProperty(file, kExtAudioFileProperty_ClientDataFormat, sizeof(client), &client);
	if (status != noErr)
	{
		ExtAudioFileDispose(file);
		error = "unable to set the decoding format";
		return false;
	}
	// ExtAudioFileSeek positions in source frames.
	const SInt64 seekFrame = static_cast<SInt64>(windowStart * fileFormat.mSampleRate);
	status = ExtAudioFileSeek(file, seekFrame);
	if (status != noErr)
	{
		ExtAudioFileDispose(file);
		error = "seek failed";
		return false;
	}
	const size_t wantedFrames = static_cast<size_t>(windowLength * DeviceRate);
	std::vector<float> pcm;
	pcm.reserve(wantedFrames * DeviceChannels);
	std::vector<float> chunk(4096 * DeviceChannels);
	while (pcm.size() / DeviceChannels < wantedFrames)
	{
		AudioBufferList buffers{};
		buffers.mNumberBuffers = 1;
		buffers.mBuffers[0].mNumberChannels = DeviceChannels;
		buffers.mBuffers[0].mDataByteSize = static_cast<UInt32>(chunk.size() * sizeof(float));
		buffers.mBuffers[0].mData = chunk.data();
		UInt32 frames = 4096;
		status = ExtAudioFileRead(file, &frames, &buffers);
		if (status != noErr || frames == 0)
			break;
		pcm.insert(pcm.end(), chunk.begin(), chunk.begin() + frames * DeviceChannels);
	}
	ExtAudioFileDispose(file);
	if (pcm.empty())
	{
		error = "no audio decoded";
		return false;
	}
	if (impl->device != 0)
		SDL_LockAudioDevice(impl->device);
	impl->music.swap(pcm);
	impl->musicWindowStart = windowStart;
	impl->musicIsLoaded = true;
	impl->playing = false;
	if (impl->device != 0)
		SDL_UnlockAudioDevice(impl->device);
	return true;
#else
	(void)path;
	(void)windowLength;
	error = "music decoding is only implemented on macOS (AudioToolbox)";
	return false;
#endif
}

void TurboAudio::startMusic()
{
	const double start = impl->config.musicStartOffset;
	impl->fallbackStart = start;
	impl->fallbackCounter = SDL_GetPerformanceCounter();
	if (impl->device == 0 || !impl->musicIsLoaded)
		return;
	SDL_LockAudioDevice(impl->device);
	impl->musicPosition = static_cast<size_t>(std::max(0.0, start - impl->musicWindowStart) * DeviceRate);
	impl->musicGain = 1.0f;
	impl->fadeStep = 0.0f;
	impl->playing = true;
	impl->clockCounter = SDL_GetPerformanceCounter();
	impl->clockFrames = impl->musicPosition;
	SDL_UnlockAudioDevice(impl->device);
}

void TurboAudio::fadeOutMusic(float seconds)
{
	if (impl->device == 0)
		return;
	SDL_LockAudioDevice(impl->device);
	if (impl->playing)
		impl->fadeStep = -1.0f / static_cast<float>(std::max(0.05f, seconds) * DeviceRate);
	SDL_UnlockAudioDevice(impl->device);
}

void TurboAudio::stopMusic()
{
	if (impl->device == 0)
		return;
	SDL_LockAudioDevice(impl->device);
	impl->playing = false;
	SDL_UnlockAudioDevice(impl->device);
}

bool TurboAudio::musicPlaying() const
{
	return impl->playing;
}

double TurboAudio::musicPosition() const
{
	const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
	if (impl->device == 0 || !impl->musicIsLoaded || !impl->pacedDevice)
	{
		const double elapsed = static_cast<double>(SDL_GetPerformanceCounter() - impl->fallbackCounter) / frequency;
		return impl->fallbackStart + elapsed;
	}
	SDL_LockAudioDevice(impl->device);
	const size_t frames = impl->clockFrames;
	const uint64_t counter = impl->clockCounter;
	SDL_UnlockAudioDevice(impl->device);
	// The callback has just filled a buffer that starts playing when the
	// previous one ends, so the listener is roughly one buffer behind.
	const double sinceCallback = static_cast<double>(SDL_GetPerformanceCounter() - counter) / frequency;
	const double latency = static_cast<double>(impl->spec.samples) / DeviceRate;
	return impl->musicWindowStart + static_cast<double>(frames) / DeviceRate + sinceCallback - latency
		+ impl->config.audioLatencyCompensation;
}

void TurboAudio::playSfx(TurboSfx effect, float gain)
{
	if (impl->device == 0)
		return;
	SDL_LockAudioDevice(impl->device);
	Voice* slot = nullptr;
	for (Voice& voice : impl->voices)
	{
		if (!voice.active)
		{
			slot = &voice;
			break;
		}
	}
	if (slot == nullptr)
		slot = &impl->voices[0];
	*slot = Voice();
	slot->type = effect;
	slot->active = true;
	slot->gain = gain;
	SDL_UnlockAudioDevice(impl->device);
}

void TurboAudio::setLoop(TurboLoop loop, float level)
{
	if (impl->device == 0)
		return;
	SDL_LockAudioDevice(impl->device);
	LoopVoice& voice = impl->loops[static_cast<int>(loop)];
	if (voice.target <= 0.0f && level > 0.0f)
		voice.t = 0.0;
	voice.target = std::max(0.0f, level);
	SDL_UnlockAudioDevice(impl->device);
}

void TurboAudio::stopAllLoops()
{
	if (impl->device == 0)
		return;
	SDL_LockAudioDevice(impl->device);
	for (LoopVoice& voice : impl->loops)
		voice.target = 0.0f;
	SDL_UnlockAudioDevice(impl->device);
}
