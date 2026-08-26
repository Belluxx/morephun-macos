#include "binary_io.h"
#include "interpreter.h"
#include "pool.h"
#include "vmgp_header.h"

#include <cstdint>
#include <iostream>


namespace {

bool require(bool condition, const char* message)
{
	if (!condition)
		std::cerr << "Binary format test failed: " << message << std::endl;
	return condition;
}

} // namespace

int main()
{
	bool success = true;

	uint8_t integers[6] = {};
	writeLittleU16(integers, 0x1234);
	writeLittleU32(integers + 2, 0x89abcdefU);
	success = require(readLittleU16(integers) == 0x1234, "16-bit little-endian round trip") && success;
	success = require(readLittleU32(integers + 2) == 0x89abcdefU,
		"32-bit little-endian round trip") && success;

	const uint8_t instructionBytes[] = {0x73, 0x20, 0x34, 0x12};
	const PIPInstruction instruction = decodePIPInstruction(instructionBytes);
	success = require(instruction.opcode == 0x73 && instruction.dest == 0x20 &&
		instruction.source == 0x34 && instruction.extra == 0x12 && instruction.word == 0x1234,
		"PIP instruction decoding") && success;

	uint8_t poolBytes[PoolItemSize] = {};
	writeLittleU32(poolBytes, (0x123456U << 8) | (0x2U << 4) | 0x4U);
	writeLittleU32(poolBytes + 4, 0xfedcba98U);
	const PoolItem pool = decodePoolItemBytes(poolBytes);
	success = require(pool.segment_1 == 4 && pool.segment_0 == 2 &&
		pool.segmentoffset == 0x123456U && pool.extra == 0xfedcba98U,
		"pool item decoding") && success;

	uint8_t headerBytes[sizeof(VMGPHeader)] = {};
	headerBytes[0] = 'V';
	headerBytes[1] = 'M';
	headerBytes[2] = 'G';
	headerBytes[3] = 'P';
	writeLittleU32(headerBytes + 4, 0x10203040U);
	writeLittleU16(headerBytes + 8, 0x5060U);
	writeLittleU16(headerBytes + 10, 0x7080U);
	writeLittleU32(headerBytes + 32, 9);
	writeLittleU32(headerBytes + 36, 10);
	const VMGPHeader header = decodeVMGPHeader(headerBytes);
	success = require(header.magicNo[0] == 'V' && header.heapSize == 0x10203040U &&
		header.stackSize == 0x5060U && header.flags == 0x7080U &&
		header.poolSize == 9 && header.stringSize == 10,
		"VMGP header decoding") && success;

	return success ? 0 : 1;
}
