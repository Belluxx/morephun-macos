#pragma once
#include <cstdint>
#include "binary_io.h"

struct PIPInstruction {
	uint8_t opcode;
	uint8_t dest;
	uint8_t source;
	uint8_t extra;
	uint16_t word;
};

inline PIPInstruction decodePIPInstruction(const uint8_t* bytes)
{
	return {bytes[0], bytes[1], bytes[2], bytes[3], readLittleU16(bytes + 2)};
}

inline int decodeImmediate(uint32_t val) { return (val & 0x7fffffff) | ((val << 1) & 0x80000000); }
