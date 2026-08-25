#pragma once

#include <cstdint>

#pragma pack(push, 1)
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
#pragma pack(pop)

static_assert(sizeof(VMGPHeader) == 40, "Invalid VMGP header size");
