#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#define STREAM_FILE 0
#define STREAM_TCP 1
#define STREAM_UDP 2
#define STREAM_BLUETOOTH 3
#define STREAM_BT STREAM_BLUETOOTH
#define STREAM_IR 4
#define STREAM_SMS 5
#define STREAM_CABLE 6
#define STREAM_RESOURCE 7
#define STREAM_HTTP 8
#define STREAM_BT_MULTI 9

#define STREAM_READ 0x0100
#define STREAM_WRITE 0x0200
#define STREAM_READWRITE (STREAM_READ|STREAM_WRITE)
#define STREAM_BINARY 0x0400
#define STREAM_TEXT 0x0000
#define STREAM_CREATE 0x0800
#define STREAM_TRUNC 0x1000
#define STREAM_EXCL 0x2000
#define STREAM_DELETE 0x4000
#define STREAM_ACCEPT 0x8000
#define STREAM_OBEX 0x0400
#define STREAM_MODE_MASK 0xFF00
#define STREAM_PORT_SHIFT 16


struct StreamSlot {
	FILE* fd = nullptr;
	bool resource = false;
	const uint8_t* embeddedData = nullptr;
	uint32_t resourceAddress = 0;
	uint32_t size = 0;
	uint32_t position = 0;
	uint32_t mode = 0;
	std::string path;
	bool deleteOnClose = false;
	bool mountedPack = false;

	bool memoryBacked() const
	{
		return resource || embeddedData != nullptr;
	}

	uint32_t read(void* destination, uint32_t requested, const uint8_t* guestMemory)
	{
		if (memoryBacked())
		{
			if (position > size || (resource && guestMemory == nullptr))
				return 0;
			const uint32_t count = std::min(requested, size - position);
			const uint8_t* source = resource
				? guestMemory + resourceAddress : embeddedData;
			std::memcpy(destination, source + position, count);
			position += count;
			return count;
		}
		return fd == nullptr ? 0 : static_cast<uint32_t>(std::fread(destination, 1, requested, fd));
	}

	int64_t seek(int32_t offset, uint32_t origin)
	{
		if (memoryBacked())
		{
			int64_t newPosition = offset;
			if (origin == 1)
				newPosition += position;
			else if (origin == 2)
				newPosition += size;
			newPosition = std::max<int64_t>(0, std::min<int64_t>(newPosition, size));
			position = static_cast<uint32_t>(newPosition);
			return position;
		}
		if (fd == nullptr)
			return -1;
		const int seekOrigin = origin == 1 ? SEEK_CUR : origin == 2 ? SEEK_END : SEEK_SET;
		if (std::fseek(fd, offset, seekOrigin) != 0)
			return -1;
		return std::ftell(fd);
	}
};
