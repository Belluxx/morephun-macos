#include "../mophun_os.h"
#include "../registers.h"
#include "stream_io.h"

#include <algorithm>
#include <cstring>

void MophunOS::vStreamOpen()
{
	const uint32_t nameAddress = mophunVM->readReg(p0);
	const uint32_t mode = mophunVM->readReg(p1);
	StreamSlot streamSlot;
	streamSlot.mode = mode & 0xffffU;

	if ((mode & 0xffU) == STREAM_RESOURCE)
	{
		const uint32_t resourceNumber = mode >> STREAM_PORT_SHIFT;
		try
		{
			streamSlot.resource = true;
			streamSlot.resourceAddress = mophunVM->getResourceAddress(resourceNumber);
			streamSlot.size = mophunVM->getResourceSize(resourceNumber);
		}
		catch (const std::exception&)
		{
			mophunVM->writeReg(r0, static_cast<uint32_t>(-1));
			return;
		}
	}
	else if ((mode & 0xffU) == STREAM_FILE && nameAddress != 0)
	{
		const char* name = reinterpret_cast<const char*>(mophunVM->getRamAddress(nameAddress));
		const bool read = (mode & STREAM_READ) != 0;
		const bool write = (mode & STREAM_WRITE) != 0;
		const bool binary = (mode & STREAM_BINARY) != 0;
		const char* fileMode = read && write ? (binary ? "r+b" : "r+")
			: write ? (binary ? "wb" : "w") : (binary ? "rb" : "r");
		streamSlot.fd = fopen(name, fileMode);
		if (streamSlot.fd == nullptr)
		{
			mophunVM->writeReg(r0, static_cast<uint32_t>(-1));
			return;
		}
	}
	else
	{
		mophunVM->writeReg(r0, static_cast<uint32_t>(-1));
		return;
	}

	const uint32_t handle = osdata.streamCounter++;
	osdata.streamSlots[handle] = streamSlot;
	mophunVM->writeReg(r0, handle);
}

void MophunOS::vStreamClose()
{
	const uint32_t handle = mophunVM->readReg(p0);
	auto stream = osdata.streamSlots.find(handle);
	if (stream == osdata.streamSlots.end())
		return;
	if (stream->second.fd != nullptr)
		fclose(stream->second.fd);
	osdata.streamSlots.erase(stream);
}

void MophunOS::vStreamRead()
{
	const uint32_t handle = mophunVM->readReg(p0);
	auto stream = osdata.streamSlots.find(handle);
	if (stream == osdata.streamSlots.end())
	{
		mophunVM->writeReg(r0, static_cast<uint32_t>(-1));
		return;
	}

	uint8_t* const buffer = mophunVM->getRamAddress(mophunVM->readReg(p1));
	const uint32_t requested = mophunVM->readReg(p2);
	uint32_t bytesRead = 0;
	if (stream->second.resource)
	{
		bytesRead = std::min(requested, stream->second.size - stream->second.position);
		std::memcpy(buffer, mophunVM->getRamAddress(stream->second.resourceAddress + stream->second.position), bytesRead);
		stream->second.position += bytesRead;
	}
	else
	{
		bytesRead = static_cast<uint32_t>(fread(buffer, 1, requested, stream->second.fd));
	}
	mophunVM->writeReg(r0, bytesRead);
}

void MophunOS::vStreamReady()
{
	const uint32_t handle = mophunVM->readReg(p0);
	const auto stream = osdata.streamSlots.find(handle);
	mophunVM->writeReg(r0, stream == osdata.streamSlots.end()
		? static_cast<uint32_t>(-1) : stream->second.mode);
}

void MophunOS::vStreamSeek()
{
	const uint32_t handle = mophunVM->readReg(p0);
	const int32_t offset = static_cast<int32_t>(mophunVM->readReg(p1));
	const uint32_t origin = mophunVM->readReg(p2);
	auto stream = osdata.streamSlots.find(handle);
	if (stream == osdata.streamSlots.end())
	{
		mophunVM->writeReg(r0, static_cast<uint32_t>(-1));
		return;
	}

	if (stream->second.resource)
	{
		int64_t newPosition = offset;
		if (origin == 1)
			newPosition += stream->second.position;
		else if (origin == 2)
			newPosition += stream->second.size;
		newPosition = std::max<int64_t>(0, std::min<int64_t>(newPosition, stream->second.size));
		stream->second.position = static_cast<uint32_t>(newPosition);
		mophunVM->writeReg(r0, stream->second.position);
	}
	else
	{
		const int seekOrigin = origin == 1 ? SEEK_CUR : origin == 2 ? SEEK_END : SEEK_SET;
		if (fseek(stream->second.fd, offset, seekOrigin) != 0)
			mophunVM->writeReg(r0, static_cast<uint32_t>(-1));
		else
			mophunVM->writeReg(r0, static_cast<uint32_t>(ftell(stream->second.fd)));
	}
}

void MophunOS::vStreamWrite()
{
	const uint32_t handle = mophunVM->readReg(p0);
	auto stream = osdata.streamSlots.find(handle);
	if (stream == osdata.streamSlots.end() || stream->second.resource || stream->second.fd == nullptr)
	{
		mophunVM->writeReg(r0, static_cast<uint32_t>(-1));
		return;
	}
	const void* buffer = mophunVM->getRamAddress(mophunVM->readReg(p1));
	const uint32_t count = mophunVM->readReg(p2);
	mophunVM->writeReg(r0, static_cast<uint32_t>(fwrite(buffer, 1, count, stream->second.fd)));
}
