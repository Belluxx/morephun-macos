#include "audio.h"

#include <SDL.h>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <vector>

#ifdef __APPLE__
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

#ifdef MOPHUN_HAVE_FLUIDSYNTH
#include <fluidsynth.h>
#endif

struct Audio::Impl {
	SDL_AudioDeviceID waveDevice = 0;
#ifdef __APPLE__
	MusicSequence sequence = nullptr;
	MusicPlayer player = nullptr;
	AUGraph graph = nullptr;
#endif
#ifdef MOPHUN_HAVE_FLUIDSYNTH
	fluid_settings_t* settings = nullptr;
	fluid_synth_t* synth = nullptr;
	fluid_player_t* player = nullptr;
	fluid_audio_driver_t* driver = nullptr;
#endif
};

namespace {

#ifdef __APPLE__
std::string statusDescription(const char* operation, OSStatus status)
{
	std::ostringstream description;
	description << operation << " failed (OSStatus " << status << ')';
	return description.str();
}

bool configureLooping(MusicSequence sequence, std::string& error)
{
	UInt32 trackCount = 0;
	OSStatus status = MusicSequenceGetTrackCount(sequence, &trackCount);
	if (status != noErr)
	{
		error = statusDescription("MusicSequenceGetTrackCount", status);
		return false;
	}

	MusicTimeStamp sequenceLength = 0;
	for (UInt32 index = 0; index < trackCount; ++index)
	{
		MusicTrack track = nullptr;
		status = MusicSequenceGetIndTrack(sequence, index, &track);
		if (status != noErr)
		{
			error = statusDescription("MusicSequenceGetIndTrack", status);
			return false;
		}

		MusicTimeStamp trackLength = 0;
		UInt32 propertySize = sizeof(trackLength);
		status = MusicTrackGetProperty(track, kSequenceTrackProperty_TrackLength,
			&trackLength, &propertySize);
		if (status != noErr)
		{
			error = statusDescription("MusicTrackGetProperty", status);
			return false;
		}
		if (trackLength > sequenceLength)
			sequenceLength = trackLength;
	}

	if (sequenceLength <= 0)
	{
		error = "MIDI sequence contains no playable track data";
		return false;
	}

	for (UInt32 index = 0; index < trackCount; ++index)
	{
		MusicTrack track = nullptr;
		status = MusicSequenceGetIndTrack(sequence, index, &track);
		if (status != noErr)
		{
			error = statusDescription("MusicSequenceGetIndTrack", status);
			return false;
		}

		status = MusicTrackSetProperty(track, kSequenceTrackProperty_TrackLength,
			&sequenceLength, sizeof(sequenceLength));
		if (status != noErr)
		{
			error = statusDescription("MusicTrackSetProperty(track length)", status);
			return false;
		}

		// Core Audio defines zero loops as repeat forever. Giving every track the
		// same duration keeps format-1 MIDI tracks synchronized at the boundary.
		MusicTrackLoopInfo loopInfo = {sequenceLength, 0};
		status = MusicTrackSetProperty(track, kSequenceTrackProperty_LoopInfo,
			&loopInfo, sizeof(loopInfo));
		if (status != noErr)
		{
			error = statusDescription("MusicTrackSetProperty(loop)", status);
			return false;
		}
	}
	return true;
}
#endif

#ifdef MOPHUN_HAVE_FLUIDSYNTH
bool readableFile(const std::string& path)
{
	std::ifstream input(path, std::ios::binary);
	return input.good();
}

std::string findSoundFont()
{
	const char* const overridePath = std::getenv("MOPHUN_SOUNDFONT");
	if (overridePath != nullptr && overridePath[0] != '\0')
		return readableFile(overridePath) ? overridePath : std::string();

	const char* const candidates[] = {
		"/usr/share/sounds/sf2/default-GM.sf2",
		"/usr/share/sounds/sf2/FluidR3_GM.sf2",
		"/usr/share/soundfonts/default.sf2",
		"/usr/share/soundfonts/FluidR3_GM.sf2",
		"/usr/local/share/soundfonts/default.sf2"
	};
	for (const char* candidate : candidates)
	{
		if (readableFile(candidate))
			return candidate;
	}
	return std::string();
}
#endif

} // namespace

Audio::Audio() : impl(new Impl())
{
}

Audio::~Audio()
{
	stop();
}

bool Audio::midiSupported() const
{
#ifdef __APPLE__
	return std::getenv("MOPHUN_DISABLE_AUDIO") == nullptr;
#elif defined(MOPHUN_HAVE_FLUIDSYNTH)
	return std::getenv("MOPHUN_DISABLE_AUDIO") == nullptr && !findSoundFont().empty();
#else
	return false;
#endif
}

bool Audio::playMidi(const uint8_t* data, size_t size, bool loop, std::string& error)
{
	stop();
	error.clear();

	if (std::getenv("MOPHUN_DISABLE_AUDIO") != nullptr)
	{
		error = "MIDI audio is disabled";
		return false;
	}
#if !defined(__APPLE__) && !defined(MOPHUN_HAVE_FLUIDSYNTH)
	error = "MIDI audio is unavailable on this platform";
	return false;
#endif
	if (data == nullptr || size < 14)
	{
		error = "MIDI resource size is invalid";
		return false;
	}

#ifdef __APPLE__
	if (size > static_cast<size_t>(std::numeric_limits<CFIndex>::max()))
	{
		error = "MIDI resource is too large";
		return false;
	}

	auto require = [&error](OSStatus status, const char* operation) {
		if (status == noErr)
			return true;
		error = statusDescription(operation, status);
		return false;
	};

	if (!require(NewMusicSequence(&impl->sequence), "NewMusicSequence"))
	{
		stop();
		return false;
	}

	CFDataRef midiData = CFDataCreate(kCFAllocatorDefault, data, static_cast<CFIndex>(size));
	if (midiData == nullptr)
	{
		error = "Unable to copy MIDI resource";
		stop();
		return false;
	}
	const OSStatus loadStatus = MusicSequenceFileLoadData(impl->sequence, midiData,
		kMusicSequenceFile_MIDIType, 0);
	CFRelease(midiData);
	if (!require(loadStatus, "MusicSequenceFileLoadData"))
	{
		stop();
		return false;
	}

	if (loop && !configureLooping(impl->sequence, error))
	{
		stop();
		return false;
	}

	if (!require(NewAUGraph(&impl->graph), "NewAUGraph"))
	{
		stop();
		return false;
	}

	AudioComponentDescription component{};
	component.componentType = kAudioUnitType_MusicDevice;
	component.componentSubType = kAudioUnitSubType_DLSSynth;
	component.componentManufacturer = kAudioUnitManufacturer_Apple;
	AUNode synthNode = 0;
	if (!require(AUGraphAddNode(impl->graph, &component, &synthNode), "AUGraphAddNode(DLS synth)"))
	{
		stop();
		return false;
	}

	component = AudioComponentDescription{};
	component.componentType = kAudioUnitType_Output;
	component.componentSubType = kAudioUnitSubType_DefaultOutput;
	component.componentManufacturer = kAudioUnitManufacturer_Apple;
	AUNode outputNode = 0;
	if (!require(AUGraphAddNode(impl->graph, &component, &outputNode), "AUGraphAddNode(output)") ||
		!require(AUGraphConnectNodeInput(impl->graph, synthNode, 0, outputNode, 0),
			"AUGraphConnectNodeInput") ||
		!require(AUGraphOpen(impl->graph), "AUGraphOpen") ||
		!require(MusicSequenceSetAUGraph(impl->sequence, impl->graph), "MusicSequenceSetAUGraph") ||
		!require(AUGraphInitialize(impl->graph), "AUGraphInitialize") ||
		!require(AUGraphStart(impl->graph), "AUGraphStart") ||
		!require(NewMusicPlayer(&impl->player), "NewMusicPlayer") ||
		!require(MusicPlayerSetSequence(impl->player, impl->sequence), "MusicPlayerSetSequence") ||
		!require(MusicPlayerPreroll(impl->player), "MusicPlayerPreroll") ||
		!require(MusicPlayerStart(impl->player), "MusicPlayerStart"))
	{
		stop();
		return false;
	}
	return true;
#elif defined(MOPHUN_HAVE_FLUIDSYNTH)
	const std::string soundFont = findSoundFont();
	if (soundFont.empty())
	{
		const char* const overridePath = std::getenv("MOPHUN_SOUNDFONT");
		error = overridePath != nullptr && overridePath[0] != '\0'
			? std::string("Unable to read SoundFont: ") + overridePath
			: "No General MIDI SoundFont found; set MOPHUN_SOUNDFONT to an .sf2 file";
		return false;
	}

	impl->settings = new_fluid_settings();
	if (impl->settings == nullptr)
	{
		error = "Unable to create FluidSynth settings";
		stop();
		return false;
	}
	fluid_settings_setint(impl->settings, "player.reset-synth", 0);
	const char* const audioDriver = std::getenv("MOPHUN_AUDIO_DRIVER");
	if (audioDriver != nullptr && audioDriver[0] != '\0')
		fluid_settings_setstr(impl->settings, "audio.driver", audioDriver);

	impl->synth = new_fluid_synth(impl->settings);
	if (impl->synth == nullptr)
	{
		error = "Unable to create the FluidSynth synthesizer";
		stop();
		return false;
	}
	if (fluid_synth_sfload(impl->synth, soundFont.c_str(), 1) == FLUID_FAILED)
	{
		error = "Unable to load SoundFont: " + soundFont;
		stop();
		return false;
	}

	impl->player = new_fluid_player(impl->synth);
	if (impl->player == nullptr ||
		fluid_player_add_mem(impl->player, data, size) == FLUID_FAILED)
	{
		error = "FluidSynth could not parse the MIDI resource";
		stop();
		return false;
	}
	if (loop)
		fluid_player_set_loop(impl->player, -1);

	// FluidSynth requires the audio driver to be created after every object it
	// may access from its rendering thread.
	impl->driver = new_fluid_audio_driver(impl->settings, impl->synth);
	if (impl->driver == nullptr)
	{
		error = "Unable to open the FluidSynth audio output";
		stop();
		return false;
	}
	if (fluid_player_play(impl->player) == FLUID_FAILED)
	{
		error = "FluidSynth could not start MIDI playback";
		stop();
		return false;
	}
	return true;
#else
	(void)data;
	(void)size;
	(void)loop;
	return false;
#endif
}

bool Audio::playWave(const uint8_t* data, size_t size, bool loop, std::string& error)
{
	stop();
	error.clear();
	if (std::getenv("MOPHUN_DISABLE_AUDIO") != nullptr)
	{
		error = "wave audio is disabled";
		return false;
	}
	if (loop)
	{
		error = "looped wave playback is not implemented";
		return false;
	}
	if (data == nullptr || size < 44 || size > static_cast<size_t>(std::numeric_limits<int>::max()))
	{
		error = "wave resource size is invalid";
		return false;
	}
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
	{
		error = std::string("SDL audio initialization failed: ") + SDL_GetError();
		return false;
	}

	SDL_RWops* source = SDL_RWFromConstMem(data, static_cast<int>(size));
	if (source == nullptr)
	{
		error = std::string("unable to open wave memory: ") + SDL_GetError();
		return false;
	}
	SDL_AudioSpec sourceSpec{};
	Uint8* sourceBytes = nullptr;
	Uint32 sourceLength = 0;
	if (SDL_LoadWAV_RW(source, 1, &sourceSpec, &sourceBytes, &sourceLength) == nullptr)
	{
		error = std::string("unable to decode wave resource: ") + SDL_GetError();
		return false;
	}

	SDL_AudioSpec requested{};
	requested.freq = 44100;
	requested.format = AUDIO_S16SYS;
	requested.channels = 2;
	requested.samples = 2048;
	SDL_AudioSpec obtained{};
	impl->waveDevice = SDL_OpenAudioDevice(nullptr, 0, &requested, &obtained,
		SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_FORMAT_CHANGE |
		SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
	if (impl->waveDevice == 0)
	{
		error = std::string("unable to open wave audio device: ") + SDL_GetError();
		SDL_FreeWAV(sourceBytes);
		return false;
	}

	SDL_AudioCVT converter{};
	if (SDL_BuildAudioCVT(&converter, sourceSpec.format, sourceSpec.channels,
		sourceSpec.freq, obtained.format, obtained.channels, obtained.freq) < 0)
	{
		error = std::string("unable to configure wave conversion: ") + SDL_GetError();
		SDL_FreeWAV(sourceBytes);
		stop();
		return false;
	}

	const Uint8* queueBytes = sourceBytes;
	Uint32 queueLength = sourceLength;
	std::vector<Uint8> converted;
	if (converter.needed)
	{
		if (sourceLength > static_cast<Uint32>(std::numeric_limits<int>::max()) ||
			converter.len_mult <= 0 ||
			sourceLength > std::numeric_limits<size_t>::max() /
				static_cast<size_t>(converter.len_mult))
		{
			error = "converted wave resource is too large";
			SDL_FreeWAV(sourceBytes);
			stop();
			return false;
		}
		converted.resize(static_cast<size_t>(sourceLength) * converter.len_mult);
		std::memcpy(converted.data(), sourceBytes, sourceLength);
		converter.buf = converted.data();
		converter.len = static_cast<int>(sourceLength);
		if (SDL_ConvertAudio(&converter) != 0)
		{
			error = std::string("unable to convert wave resource: ") + SDL_GetError();
			SDL_FreeWAV(sourceBytes);
			stop();
			return false;
		}
		queueBytes = converted.data();
		queueLength = static_cast<Uint32>(converter.len_cvt);
	}

	const int queueResult = SDL_QueueAudio(impl->waveDevice, queueBytes, queueLength);
	SDL_FreeWAV(sourceBytes);
	if (queueResult != 0)
	{
		error = std::string("unable to queue wave resource: ") + SDL_GetError();
		stop();
		return false;
	}
	SDL_PauseAudioDevice(impl->waveDevice, 0);
	return true;
}

void Audio::stop()
{
	if (impl->waveDevice != 0)
	{
		SDL_ClearQueuedAudio(impl->waveDevice);
		SDL_CloseAudioDevice(impl->waveDevice);
		impl->waveDevice = 0;
	}
#ifdef __APPLE__
	if (impl->player != nullptr)
	{
		MusicPlayerStop(impl->player);
		MusicPlayerSetSequence(impl->player, nullptr);
		DisposeMusicPlayer(impl->player);
		impl->player = nullptr;
	}
	if (impl->graph != nullptr)
	{
		AUGraphStop(impl->graph);
		AUGraphUninitialize(impl->graph);
		AUGraphClose(impl->graph);
	}
	if (impl->sequence != nullptr)
	{
		MusicSequenceSetAUGraph(impl->sequence, nullptr);
		DisposeMusicSequence(impl->sequence);
		impl->sequence = nullptr;
	}
	if (impl->graph != nullptr)
	{
		DisposeAUGraph(impl->graph);
		impl->graph = nullptr;
	}
#endif
#ifdef MOPHUN_HAVE_FLUIDSYNTH
	if (impl->player != nullptr)
		fluid_player_stop(impl->player);
	// The audio driver owns the rendering thread, so it must be destroyed
	// before the player and synthesizer it can access.
	if (impl->driver != nullptr)
	{
		delete_fluid_audio_driver(impl->driver);
		impl->driver = nullptr;
	}
	if (impl->player != nullptr)
	{
		delete_fluid_player(impl->player);
		impl->player = nullptr;
	}
	if (impl->synth != nullptr)
	{
		delete_fluid_synth(impl->synth);
		impl->synth = nullptr;
	}
	if (impl->settings != nullptr)
	{
		delete_fluid_settings(impl->settings);
		impl->settings = nullptr;
	}
#endif
}
