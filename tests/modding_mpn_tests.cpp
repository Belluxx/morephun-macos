#include "mophunmod/mpn_image.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool require(bool condition, const std::string& message)
{
	if (!condition)
		std::cerr << "Modding MPN test failed: " << message << '\n';
	return condition;
}

void writeU16(std::vector<uint8_t>& bytes, std::size_t offset, uint16_t value)
{
	bytes[offset] = static_cast<uint8_t>(value);
	bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
}

void writeU32(std::vector<uint8_t>& bytes, std::size_t offset, uint32_t value)
{
	bytes[offset] = static_cast<uint8_t>(value);
	bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
	bytes[offset + 2] = static_cast<uint8_t>(value >> 16);
	bytes[offset + 3] = static_cast<uint8_t>(value >> 24);
}

std::vector<uint8_t> syntheticMpn()
{
	std::vector<uint8_t> bytes(mophunmod::MpnHeaderSize, 0);
	bytes[0] = 'V'; bytes[1] = 'M'; bytes[2] = 'G'; bytes[3] = 'P';
	writeU32(bytes, 4, 0x10203040);
	writeU16(bytes, 8, 0x5060);
	writeU16(bytes, 10, 0);
	writeU32(bytes, 12, 8);
	writeU32(bytes, 16, 3);
	writeU32(bytes, 20, 5);
	writeU32(bytes, 24, 4);
	writeU32(bytes, 28, 0x12345678);
	writeU32(bytes, 32, 2);
	writeU32(bytes, 36, 4);

	const uint8_t body[] = {
		0x01, 0, 0, 0, 0x40, 0x30, 7, 0,
		0xaa, 0xbb, 0xcc,
		4, 0, 0, 0,
		0x11, 0, 0, 0, 0, 0, 0, 0,
		0x02, 0, 0, 0, 0, 0, 0, 0,
		'o', 'l', 'd', 0,
		0xde, 0xad, 0, 0
	};
	bytes.insert(bytes.end(), body, body + sizeof(body));
	return bytes;
}

template <typename Function>
bool throwsMpnError(Function function)
{
	try
	{
		function();
	}
	catch (const mophunmod::MpnError&)
	{
		return true;
	}
	return false;
}

} // namespace

int main()
{
	using namespace mophunmod;
	bool success = true;
	const std::vector<uint8_t> input = syntheticMpn();
	const MpnImage image = MpnImage::parse(input);
	success = require(image.header().heapSize == 0x10203040 &&
		image.header().stackSize == 0x5060 && image.header().directorySize == 0x12345678,
		"header fields are decoded") && success;
	success = require(image.code().size() == 8 && image.data().size() == 3 &&
		image.resources().size() == 4 && image.poolBytes().size() == 16 &&
		image.strings().size() == 4, "all file-backed sections are split") && success;
	success = require(image.trailingData() == std::vector<uint8_t>({0xde, 0xad, 0, 0}),
		"unknown trailing bytes are retained") && success;
	success = require(image.dataFileOffset() == 48 && image.resourceFileOffset() == 51 &&
		image.poolFileOffset() == 55 && image.stringFileOffset() == 71 &&
		image.describedFileSize() == 75, "section offsets are exact") && success;
	success = require(image.serialize() == input,
		"an unmodified image serializes byte-for-byte, including padding") && success;

	std::vector<uint8_t> malformed = input;
	malformed.resize(MpnHeaderSize - 1);
	success = require(throwsMpnError([&] { MpnImage::parse(malformed); }),
		"truncated header is rejected") && success;
	malformed = input;
	malformed[0] = 'X';
	success = require(throwsMpnError([&] { MpnImage::parse(malformed); }),
		"bad magic is rejected") && success;
	malformed = input;
	writeU16(malformed, 10, MpnCompressedSectionsFlag);
	success = require(throwsMpnError([&] { MpnImage::parse(malformed); }),
		"compressed sections are rejected") && success;
	malformed = input;
	writeU32(malformed, 12, 7);
	success = require(throwsMpnError([&] { MpnImage::parse(malformed); }),
		"unaligned PIP2 code is rejected") && success;
	malformed = input;
	writeU32(malformed, 16, 0xffffffffU);
	success = require(throwsMpnError([&] { MpnImage::parse(malformed); }),
		"oversized section is rejected without pointer wraparound") && success;
	malformed = input;
	writeU32(malformed, 32, 0xffffffffU);
	success = require(throwsMpnError([&] { MpnImage::parse(malformed); }),
		"oversized pool is rejected without multiplication wraparound") && success;
	malformed = input;
	writeU32(malformed, 59, 8);
	success = require(throwsMpnError([&] { MpnImage::parse(malformed); }),
		"pool code references are bounds-checked") && success;
	malformed = input;
	writeU32(malformed, 59, 2);
	success = require(throwsMpnError([&] { MpnImage::parse(malformed); }),
		"pool code references must be instruction-aligned") && success;
	malformed = input;
	writeU32(malformed, 55, (1U << 8) | 0x18U);
	success = require(throwsMpnError([&] { MpnImage::parse(malformed); }),
		"relative pool reference cycles are rejected") && success;
	malformed = input;
	writeU32(malformed, 63, (4U << 8) | 0x02U);
	success = require(throwsMpnError([&] { MpnImage::parse(malformed); }),
		"pool string references are bounds-checked") && success;
	malformed = input;
	malformed[74] = 1;
	success = require(throwsMpnError([&] { MpnImage::parse(malformed); }),
		"referenced strings must be terminated inside the string section") && success;
	success = require(throwsMpnError([&] { MpnImage::parse(nullptr, MpnHeaderSize); }),
		"null input is rejected") && success;

	return success ? 0 : 1;
}
