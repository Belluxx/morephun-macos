#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "binary_io.h"


constexpr size_t PoolItemSize = 8;

struct PoolItem {
	uint8_t segment_1;
	uint8_t segment_0;
	uint32_t segmentoffset;
	uint32_t extra;
};

inline PoolItem decodePoolItemBytes(const uint8_t* bytes)
{
	const uint32_t descriptor = readLittleU32(bytes);
	return {
		static_cast<uint8_t>(descriptor & 0xfU),
		static_cast<uint8_t>((descriptor >> 4) & 0xfU),
		descriptor >> 8,
		readLittleU32(bytes + 4)
	};
}

struct PoolData {
	bool isSyscall = false;
	uint32_t value;
	std::function<void()> fun;
};
