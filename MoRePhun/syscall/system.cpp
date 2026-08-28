#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <vector>
#include "system.h"
#include "../mophun_os.h"
#include "../registers.h"

namespace {

void writeU16(uint8_t* destination, uint16_t value)
{
	destination[0] = static_cast<uint8_t>(value);
	destination[1] = static_cast<uint8_t>(value >> 8);
}

void writeU32(uint8_t* destination, uint32_t value)
{
	destination[0] = static_cast<uint8_t>(value);
	destination[1] = static_cast<uint8_t>(value >> 8);
	destination[2] = static_cast<uint8_t>(value >> 16);
	destination[3] = static_cast<uint8_t>(value >> 24);
}

uint32_t readU32(const uint8_t* source)
{
	return static_cast<uint32_t>(source[0]) |
		(static_cast<uint32_t>(source[1]) << 8) |
		(static_cast<uint32_t>(source[2]) << 16) |
		(static_cast<uint32_t>(source[3]) << 24);
}

bool waveResourceSize(const uint8_t* source, uint32_t available, uint32_t& size)
{
	if (available < 12 || std::memcmp(source, "RIFF", 4) != 0 ||
		std::memcmp(source + 8, "WAVE", 4) != 0)
		return false;
	const uint32_t payload = readU32(source + 4);
	if (payload > available - 8 || payload < 4)
		return false;
	size = payload + 8;
	return true;
}

class BitReader {
	public:
		BitReader(const uint8_t* data, uint32_t size) : data(data), bitSize(size * 8U) {}

		bool read(uint32_t count, uint32_t& result)
		{
			if (count > 32 || bitPosition + count > bitSize)
				return false;
			result = 0;
			for (uint32_t i = 0; i < count; ++i)
			{
				const uint8_t byte = data[bitPosition >> 3];
				result = (result << 1) | ((byte >> (7U - (bitPosition & 7U))) & 1U);
				++bitPosition;
			}
			return true;
		}

	private:
		const uint8_t* data;
		uint32_t bitSize;
		uint32_t bitPosition = 0;
};

int decompressLz(const uint8_t* source, uint32_t sourceSize, uint8_t* destination,
	uint32_t destinationSize, uint8_t extendedOffsetBits, uint8_t maxOffsetBits)
{
	BitReader bits(source, sourceSize);
	uint32_t destinationPosition = 0;
	while (destinationPosition < destinationSize)
	{
		uint32_t flag = 0;
		if (!bits.read(1, flag))
			return -1;
		if (flag == 0)
		{
			uint32_t literal = 0;
			if (!bits.read(8, literal))
				return -1;
			destination[destinationPosition++] = static_cast<uint8_t>(literal);
			continue;
		}

		uint32_t prefixLength = 0;
		uint32_t prefixBit = 0;
		while (prefixLength < maxOffsetBits)
		{
			if (!bits.read(1, prefixBit))
				return -1;
			if (prefixBit == 0)
				break;
			++prefixLength;
		}

		uint32_t copyLength = 2;
		if (prefixLength != 0)
		{
			uint32_t lengthBits = 0;
			if (!bits.read(prefixLength, lengthBits))
				return -1;
			copyLength = (lengthBits | (1U << prefixLength)) + 1;
		}

		uint32_t offsetBits = 0;
		if (!bits.read(copyLength == 2 ? 8 : extendedOffsetBits, offsetBits))
			return -1;
		const uint32_t backOffset = offsetBits + (copyLength == 2 ? 2U : copyLength);
		if (backOffset > destinationPosition)
			return -1;

		copyLength = std::min(copyLength, destinationSize - destinationPosition);
		for (uint32_t i = 0; i < copyLength; ++i)
			destination[destinationPosition + i] = destination[destinationPosition - backOffset + i];
		destinationPosition += copyLength;
	}
	return static_cast<int>(destinationPosition);
}

} // namespace

void MophunOS::vDecompress()
{
	const uint32_t sourceAddress = mophunVM->readReg(p0);
	const uint32_t destinationAddress = mophunVM->readReg(p1);
	const uint32_t streamHandle = mophunVM->readReg(p2);
	const uint8_t* compressed = nullptr;
	uint32_t available = 0;
	std::vector<uint8_t> fileData;
	StreamSlot* stream = nullptr;

	if (sourceAddress != 0)
	{
		compressed = mophunVM->getRamAddress(sourceAddress);
		available = RAM_SIZE - sourceAddress;
	}
	else
	{
		auto found = osdata.streamSlots.find(streamHandle);
		if (found == osdata.streamSlots.end())
		{
			mophunVM->writeReg(r0, static_cast<uint32_t>(-1));
			return;
		}
		stream = &found->second;
		uint8_t header[22];
		if (stream->read(header, sizeof(header), mophunVM->getRamAddress(0)) != sizeof(header))
		{
			mophunVM->writeReg(r0, static_cast<uint32_t>(-1));
			return;
		}
		const uint32_t payloadSize = readU32(header + 8);
		if (payloadSize > RAM_SIZE)
		{
			mophunVM->writeReg(r0, static_cast<uint32_t>(-1));
			return;
		}
		fileData.resize(sizeof(header) + payloadSize);
		std::memcpy(fileData.data(), header, sizeof(header));
		if (stream->read(fileData.data() + sizeof(header), payloadSize,
			mophunVM->getRamAddress(0)) != payloadSize)
		{
			mophunVM->writeReg(r0, static_cast<uint32_t>(-1));
			return;
		}
		compressed = fileData.data();
		available = static_cast<uint32_t>(fileData.size());
	}

	if (available < 22 || compressed[0] != 'L' || compressed[1] != 'Z')
	{
		mophunVM->writeReg(r0, static_cast<uint32_t>(-1));
		return;
	}

	const uint8_t maxOffsetBits = compressed[2];
	const uint8_t extendedOffsetBits = compressed[3];
	const uint32_t destinationSize = readU32(compressed + 4);
	const uint32_t payloadSize = readU32(compressed + 8);
	if (payloadSize > available - 22 || destinationAddress > RAM_SIZE || destinationSize > RAM_SIZE - destinationAddress)
	{
		mophunVM->writeReg(r0, static_cast<uint32_t>(-1));
		return;
	}

	const int result = decompressLz(compressed + 22, payloadSize,
		mophunVM->getRamAddress(destinationAddress), destinationSize,
		extendedOffsetBits, maxOffsetBits);
	mophunVM->writeReg(r0, static_cast<uint32_t>(result));
}

void MophunOS::vCheckDataCert()
{
	mophunVM->writeReg(r0, 1);
}

void MophunOS::vCheckIMEI()
{
	mophunVM->writeReg(r0, 1);
}

void MophunOS::vMsgBox()
{
	mophunVM->writeReg(r0, 1);
}

void MophunOS::vMsgBoxU()
{
	vMsgBox();
}

void MophunOS::vPlayResource()
{
	constexpr uint32_t soundTypeMask = 0xf;
	constexpr uint32_t midi = 2;
	constexpr uint32_t loop = 0x100;
	constexpr uint32_t streamSource = 0x200;
	constexpr uint32_t stop = 0x400;

	const uint32_t sourceValue = mophunVM->readReg(p0);
	const uint32_t length = mophunVM->readReg(p1);
	const uint32_t flags = mophunVM->readReg(p2);
	const bool trace = std::getenv("MOPHUN_TRACE_AUDIO") != nullptr;
	if (trace)
	{
		std::cout << "vPlayResource(data=0x" << std::hex << sourceValue
			<< ", length=0x" << length << ", flags=0x" << flags << ')'
			<< std::dec << std::endl;
	}

	if ((flags & stop) != 0)
	{
		audio->stop();
		mophunVM->writeReg(r0, 1);
		return;
	}

	// Mophun permits one vPlayResource sound at a time. A new request stops
	// the previous one even if the replacement resource later proves invalid.
	audio->stop();
	if ((flags & soundTypeMask) != midi)
	{
		std::cerr << "Unsupported vPlayResource sound type: "
			<< (flags & soundTypeMask) << std::endl;
		mophunVM->writeReg(r0, 0);
		return;
	}
	if (length < 14 || length > RAM_SIZE)
	{
		mophunVM->writeReg(r0, 0);
		return;
	}

	const uint8_t* source = nullptr;
	std::vector<uint8_t> streamData;
	if ((flags & streamSource) != 0)
	{
		auto found = osdata.streamSlots.find(sourceValue);
		if (found == osdata.streamSlots.end())
		{
			mophunVM->writeReg(r0, 0);
			return;
		}

		StreamSlot& stream = found->second;
		streamData.resize(length);
		if (stream.read(streamData.data(), length, mophunVM->getRamAddress(0)) != length)
		{
			mophunVM->writeReg(r0, 0);
			return;
		}
		source = streamData.data();
	}
	else
	{
		if (sourceValue > RAM_SIZE || length > RAM_SIZE - sourceValue)
		{
			mophunVM->writeReg(r0, 0);
			return;
		}
		source = mophunVM->getRamAddress(sourceValue);
	}

	std::string error;
	const bool played = audio->playMidi(source, length, (flags & loop) != 0, error);
	if (!played)
		std::cerr << "MIDI playback failed: " << error << std::endl;
	else if (trace)
		std::cout << "MIDI playback started (" << length << " bytes, loop="
			<< (((flags & loop) != 0) ? "yes" : "no") << ')' << std::endl;
	mophunVM->writeReg(r0, played ? 1 : 0);
}

void MophunOS::vSoundInit()
{
	// SND_OK. The SDL device is opened lazily on the first PLAY command so
	// initialization remains harmless on headless runs.
	mophunVM->writeReg(r0, 0);
}

void MophunOS::vSoundGetHandle()
{
	const uint32_t address = mophunVM->readReg(p0);
	uint32_t size = 0;
	if (address >= RAM_SIZE ||
		!waveResourceSize(mophunVM->getRamAddress(address), RAM_SIZE - address, size))
	{
		mophunVM->writeReg(r0, static_cast<uint32_t>(-1));
		return;
	}
	const uint32_t handle = ++osdata.soundCounter;
	osdata.soundSlots[handle] = {address, size};
	mophunVM->writeReg(r0, handle);
}

void MophunOS::vSoundCtrlEx()
{
	constexpr uint32_t play = 1;
	constexpr uint32_t stop = 2;
	constexpr uint32_t status = 8;
	const uint32_t handle = mophunVM->readReg(p0);
	const uint32_t command = mophunVM->readReg(p1);
	const uint32_t parameters = mophunVM->readReg(p2);
	auto found = osdata.soundSlots.find(handle);
	if (command == stop)
	{
		soundAudio->stop();
		mophunVM->writeReg(r0, 0);
		return;
	}
	if (command == status)
	{
		mophunVM->writeReg(r0, found == osdata.soundSlots.end() ?
			static_cast<uint32_t>(-1) : 0);
		return;
	}
	if (command != play || found == osdata.soundSlots.end())
	{
		mophunVM->writeReg(r0, static_cast<uint32_t>(-1));
		return;
	}

	const bool loop = (parameters & 0xffffU) == 0xffffU;
	std::string error;
	const SoundSlot& sound = found->second;
	if (!soundAudio->playWave(mophunVM->getRamAddress(sound.address), sound.size, loop, error))
	{
		if (std::getenv("MOPHUN_DISABLE_AUDIO") == nullptr)
			std::cerr << "Wave playback failed: " << error << std::endl;
		mophunVM->writeReg(r0, static_cast<uint32_t>(-1));
		return;
	}
	mophunVM->writeReg(r0, 0);
}

void MophunOS::vSoundCtrl()
{
	// The two-argument form shares the command implementation; parameters are
	// ignored by every control code valid for vSoundCtrl.
	mophunVM->writeReg(p2, 0);
	vSoundCtrlEx();
}

void MophunOS::vSoundDisposeHandle()
{
	const uint32_t handle = mophunVM->readReg(p0);
	auto found = osdata.soundSlots.find(handle);
	if (found == osdata.soundSlots.end())
	{
		mophunVM->writeReg(r0, static_cast<uint32_t>(-1));
		return;
	}
	soundAudio->stop();
	osdata.soundSlots.erase(found);
	mophunVM->writeReg(r0, 0);
}

void MophunOS::vGetCaps()
{
	constexpr uint32_t video = 0;
	constexpr uint32_t input = 1;
	constexpr uint32_t sound = 2;
	constexpr uint32_t communication = 3;
	constexpr uint32_t system = 4;
	constexpr uint16_t screenWidth = 128;
	constexpr uint16_t screenHeight = 160;

	const uint32_t query = mophunVM->readReg(p0);
	uint8_t* const caps = mophunVM->getRamAddress(mophunVM->readReg(p1));

	switch (query)
	{
		case video:
			writeU16(caps + 0, 8);
			// Orientation + 3D capability bits, with the T610 RGB332 format.
			writeU16(caps + 2, 0x3007);
			writeU16(caps + 4, screenWidth);
			writeU16(caps + 6, screenHeight);
			break;
		case input:
			// Legacy releases only expect the common four-byte caps header.
			writeU16(caps + 0, 4);
			writeU16(caps + 2, 0);
			break;
		case sound:
			writeU16(caps + 0, 4);
			// SCAPS_WAVE plus SCAPS_MIDI when a synthesizer is available.
			writeU16(caps + 2, static_cast<uint16_t>(0x2 |
				(audio->midiSupported() ? 0x8 : 0)));
			break;
		case communication:
			writeU16(caps + 0, 4);
			writeU16(caps + 2, 0x27);
			break;
		case system:
			writeU16(caps + 0, 12);
			writeU16(caps + 2, 0x25);
			// Upper half is the T610 model (2), lower half Sony Ericsson (3).
			writeU32(caps + 4, 0x00020003);
			writeU32(caps + 8, 0);
			break;
		default:
			mophunVM->writeReg(r0, 0);
			return;
	}

	mophunVM->writeReg(r0, 1);
}

void MophunOS::vGetRandom()
{
	uint32_t rnd = rand() % (VRAND_MAX + 1);
	mophunVM->writeReg(r0, rnd);
}

void MophunOS::vSetRandom()
{
	srand(mophunVM->readReg(p0));
}

void MophunOS::vSysCtl()
{
	mophunVM->writeReg(r0, 1);
}

void MophunOS::vUID()
{
	mophunVM->writeReg(r0, 0xdeadbeef);
}

void MophunOS::vTerminateVMGP(void)
{
	std::cout << "vTerminateVMGP -> Program ended!" << std::endl;
	status = false;
}
