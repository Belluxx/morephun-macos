#pragma once

#include <cstdint>


inline uint16_t readLittleU16(const uint8_t* bytes)
{
	return static_cast<uint16_t>(bytes[0]) |
		static_cast<uint16_t>(bytes[1]) << 8;
}

inline uint32_t readLittleU32(const uint8_t* bytes)
{
	return static_cast<uint32_t>(bytes[0]) |
		static_cast<uint32_t>(bytes[1]) << 8 |
		static_cast<uint32_t>(bytes[2]) << 16 |
		static_cast<uint32_t>(bytes[3]) << 24;
}

inline void writeLittleU16(uint8_t* bytes, uint16_t value)
{
	bytes[0] = static_cast<uint8_t>(value);
	bytes[1] = static_cast<uint8_t>(value >> 8);
}

inline void writeLittleU32(uint8_t* bytes, uint32_t value)
{
	bytes[0] = static_cast<uint8_t>(value);
	bytes[1] = static_cast<uint8_t>(value >> 8);
	bytes[2] = static_cast<uint8_t>(value >> 16);
	bytes[3] = static_cast<uint8_t>(value >> 24);
}
