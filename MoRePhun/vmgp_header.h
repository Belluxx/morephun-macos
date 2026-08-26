#pragma once

#include <cstring>
#include <cstdint>

#include "binary_io.h"

struct VMGPHeader
{
	char magicNo[4];
	uint32_t heapSize;
	uint16_t stackSize;
	uint16_t flags;
	uint32_t codeSize;
	uint32_t dataSize;
	uint32_t bssSize;
	uint32_t resSize;
	uint32_t directorySize;
	uint32_t poolSize;
	uint32_t stringSize;
};

static_assert(sizeof(VMGPHeader) == 40, "Invalid VMGP header size");

inline VMGPHeader decodeVMGPHeader(const uint8_t* bytes)
{
	VMGPHeader header{};
	std::memcpy(header.magicNo, bytes, sizeof(header.magicNo));
	header.heapSize = readLittleU32(bytes + 4);
	header.stackSize = readLittleU16(bytes + 8);
	header.flags = readLittleU16(bytes + 10);
	header.codeSize = readLittleU32(bytes + 12);
	header.dataSize = readLittleU32(bytes + 16);
	header.bssSize = readLittleU32(bytes + 20);
	header.resSize = readLittleU32(bytes + 24);
	header.directorySize = readLittleU32(bytes + 28);
	header.poolSize = readLittleU32(bytes + 32);
	header.stringSize = readLittleU32(bytes + 36);
	return header;
}
