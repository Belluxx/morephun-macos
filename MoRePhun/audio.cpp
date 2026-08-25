#include "audio.h"

#include <cstdlib>
#include <limits>
#include <sstream>

#ifdef __APPLE__
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

struct Audio::Impl {
#ifdef __APPLE__
	MusicSequence sequence = nullptr;
	MusicPlayer player = nullptr;
	AUGraph graph = nullptr;
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
#else
	return false;
#endif
}

bool Audio::playMidi(const uint8_t* data, size_t size, bool loop, std::string& error)
{
	stop();
	error.clear();

	if (!midiSupported())
	{
		error = "MIDI audio is disabled or unavailable on this platform";
		return false;
	}
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
#else
	(void)data;
	(void)size;
	(void)loop;
	return false;
#endif
}

void Audio::stop()
{
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
}
