#include "rom_decrypt.h"

#include <array>
#include <cstring>

namespace {

constexpr size_t HeaderSize = sizeof(VMGPHeader);
constexpr size_t MetaSize = 0x98;

// Sony Ericsson platform key material recovered from the preservation build of
// the Tuxality emulator. It is used only to decrypt the in-memory code section.
constexpr std::array<uint8_t, 16> SelectorKey = {{
	0xe3, 0x0b, 0x8c, 0x9c, 0x74, 0xc0, 0x26, 0xb4,
	0xcf, 0xba, 0x82, 0x0d, 0xd0, 0x72, 0xb3, 0x28
}};

constexpr std::array<uint8_t, 512> SonyEricssonKeys = {{
	0x59, 0x63, 0x3d, 0xa5, 0x85, 0x55, 0x8c, 0x22, 0x3d, 0xb1, 0x0f, 0x59, 0xef, 0x73, 0xac, 0x1d,
	0x5e, 0x99, 0x00, 0x0c, 0xb7, 0x31, 0xd0, 0xd7, 0x70, 0x13, 0x34, 0x25, 0xb1, 0x2b, 0xc6, 0x17,
	0x02, 0xe3, 0xa5, 0xad, 0xc4, 0x18, 0xc1, 0xba, 0x05, 0x60, 0xec, 0x8b, 0x19, 0xf1, 0x31, 0x01,
	0xd4, 0xfc, 0x00, 0x97, 0x84, 0x90, 0xff, 0x99, 0x3f, 0xff, 0x3a, 0xd9, 0xaa, 0x71, 0x7c, 0x35,
	0xe2, 0x8e, 0xcb, 0x2c, 0x14, 0x3e, 0x87, 0xee, 0x24, 0xc4, 0xdb, 0xf4, 0xe6, 0x23, 0xe8, 0xb5,
	0x9f, 0x32, 0x40, 0xec, 0x68, 0x03, 0xb9, 0x8d, 0xea, 0x5c, 0xb9, 0x26, 0xaa, 0x04, 0x40, 0xaa,
	0xc8, 0x21, 0xee, 0xe7, 0x05, 0x47, 0x6e, 0x55, 0x1b, 0xbe, 0x56, 0x08, 0xf0, 0x74, 0x50, 0xe7,
	0x15, 0xa4, 0xf5, 0x84, 0x01, 0xe9, 0x5e, 0x0a, 0x28, 0x3f, 0x3e, 0xda, 0x72, 0xf7, 0x9a, 0xf0,
	0xfb, 0x95, 0xfc, 0x7c, 0xc4, 0x54, 0x19, 0xfe, 0x73, 0x6e, 0x0a, 0xa9, 0x26, 0x00, 0xd2, 0x43,
	0x05, 0xbe, 0x58, 0xfa, 0xaf, 0xc6, 0xbe, 0xdd, 0xe6, 0x01, 0x9d, 0xc0, 0xf2, 0x43, 0xde, 0xbf,
	0x7f, 0xc5, 0x7a, 0xf0, 0x05, 0x46, 0x66, 0xf0, 0xa2, 0x16, 0x07, 0x1e, 0x0c, 0x05, 0x6a, 0xde,
	0x36, 0x5d, 0x43, 0x2e, 0xcc, 0xcd, 0x2b, 0xba, 0x19, 0xab, 0xb1, 0x1a, 0xf7, 0x92, 0x39, 0x08,
	0x3d, 0x72, 0xad, 0xcd, 0xef, 0x06, 0x73, 0x96, 0x8e, 0x36, 0xe0, 0x0c, 0x72, 0x62, 0x58, 0x2a,
	0x2c, 0x5b, 0x2e, 0x66, 0xe5, 0x1f, 0x0c, 0x0f, 0xb9, 0x8b, 0xfc, 0x77, 0x4d, 0xd1, 0xe3, 0x90,
	0x72, 0x45, 0x3a, 0x2f, 0xba, 0x7f, 0x1a, 0xbc, 0x93, 0xc3, 0x00, 0x8d, 0x35, 0x87, 0xe5, 0xb3,
	0x8d, 0x7b, 0x19, 0x77, 0xd2, 0xcc, 0x35, 0xc5, 0xd0, 0x45, 0xd1, 0x53, 0xa5, 0x82, 0x08, 0xde,
	0x55, 0xb6, 0xdf, 0xe7, 0xc9, 0xfe, 0xbf, 0x41, 0xcc, 0xf9, 0xf7, 0xc3, 0x65, 0x7f, 0x7e, 0x5d,
	0xb5, 0x0f, 0xa7, 0xeb, 0x03, 0x0e, 0xdc, 0xee, 0xf1, 0x72, 0x98, 0x04, 0x0e, 0x1b, 0xeb, 0xc5,
	0x4d, 0xe4, 0x11, 0x7a, 0xf4, 0x68, 0x50, 0x99, 0x10, 0xfd, 0xf3, 0x9a, 0xa8, 0xf4, 0x4c, 0x21,
	0xa0, 0xca, 0x98, 0x23, 0x58, 0x0b, 0xbe, 0x01, 0xe7, 0x37, 0x4b, 0x88, 0x39, 0xd8, 0x6b, 0x7b,
	0x7d, 0x49, 0x76, 0x2a, 0xff, 0xd1, 0xcc, 0xe2, 0x95, 0x50, 0xe7, 0x87, 0x9b, 0x51, 0xa3, 0xf6,
	0x4b, 0x15, 0xa0, 0x0d, 0x05, 0x2b, 0x43, 0x42, 0xe6, 0x17, 0x01, 0x3a, 0x08, 0x1f, 0x59, 0x30,
	0x40, 0xe7, 0xef, 0x05, 0x6a, 0x27, 0x99, 0x03, 0xa7, 0x68, 0x3a, 0x77, 0xac, 0x89, 0xe6, 0xa8,
	0x77, 0x44, 0x66, 0x72, 0x07, 0x82, 0x4b, 0xc8, 0x97, 0x3b, 0xfe, 0x01, 0x6d, 0x1c, 0x5f, 0xa0,
	0x03, 0xf7, 0xea, 0x71, 0x13, 0x51, 0x68, 0x23, 0x3a, 0xec, 0x66, 0x34, 0x8f, 0x0c, 0xc4, 0xfc,
	0xea, 0x31, 0xda, 0x87, 0xe4, 0x0d, 0xff, 0x17, 0x13, 0x24, 0xa9, 0xe3, 0x27, 0xfc, 0x81, 0x80,
	0x1f, 0x35, 0x6f, 0xdb, 0x2c, 0x93, 0xff, 0x66, 0xad, 0xb1, 0xb4, 0x82, 0xf3, 0x3e, 0xa1, 0xab,
	0xa4, 0x82, 0x9d, 0x82, 0x1b, 0xe2, 0x9c, 0xcd, 0xc0, 0xbc, 0xb1, 0x61, 0xeb, 0x28, 0xc1, 0x58,
	0x75, 0xf2, 0x81, 0xb8, 0x81, 0xcf, 0xb6, 0xa5, 0x2d, 0x48, 0xae, 0xbe, 0xd5, 0xd5, 0x8b, 0x36,
	0x5a, 0x60, 0xe9, 0x28, 0xc2, 0xd6, 0x9f, 0xd3, 0xa6, 0x69, 0x60, 0xb2, 0x29, 0x6d, 0xd5, 0x11,
	0xf6, 0xfd, 0xaa, 0xda, 0xf0, 0x13, 0x4d, 0xb5, 0x68, 0x95, 0xfb, 0x21, 0x82, 0x91, 0xc9, 0x8b,
	0x63, 0xed, 0x13, 0x6b, 0x1d, 0xf4, 0x89, 0x21, 0x26, 0x9b, 0x56, 0x87, 0xad, 0xe0, 0x24, 0xef
}};

uint16_t readU16(const uint8_t* bytes)
{
	return static_cast<uint16_t>(bytes[0]) |
		static_cast<uint16_t>(bytes[1]) << 8;
}

uint32_t readU32(const uint8_t* bytes)
{
	return static_cast<uint32_t>(bytes[0]) |
		static_cast<uint32_t>(bytes[1]) << 8 |
		static_cast<uint32_t>(bytes[2]) << 16 |
		static_cast<uint32_t>(bytes[3]) << 24;
}

void writeU32(uint8_t* bytes, uint32_t value)
{
	bytes[0] = static_cast<uint8_t>(value);
	bytes[1] = static_cast<uint8_t>(value >> 8);
	bytes[2] = static_cast<uint8_t>(value >> 16);
	bytes[3] = static_cast<uint8_t>(value >> 24);
}

uint32_t xteaDecryptWord(uint32_t input, const uint8_t* keyBytes)
{
	std::array<uint16_t, 8> key;
	for (size_t index = 0; index < key.size(); ++index)
		key[index] = readU16(keyBytes + index * 2);

	uint32_t sum = 0xc6ef3720;
	uint32_t v0 = static_cast<uint16_t>(input);
	uint32_t v1 = input >> 16;
	for (unsigned round = 0; round < 32; ++round)
	{
		v1 -= (((static_cast<uint16_t>(16 * v0) ^ ((v0 << 16) >> 21)) + v0) ^
			static_cast<uint16_t>(key[2 * ((sum << 19) >> 30)] + sum));
		sum -= 0x9e3779b9;
		v0 -= (((static_cast<uint16_t>(16 * v1) ^ ((v1 << 16) >> 21)) + v1) ^
			static_cast<uint16_t>(key[2 * (sum & 3)] + sum));
	}
	return static_cast<uint32_t>(static_cast<uint16_t>(v1)) << 16 |
		static_cast<uint16_t>(v0);
}

uint32_t decryptBlock(uint32_t block, const uint32_t* key)
{
	uint32_t r4 = block;
	const uint32_t r10 = key[0];
	uint32_t r5 = static_cast<uint16_t>(r4);
	const uint32_t r8 = key[1];
	const uint32_t r7 = key[2];
	const uint32_t r9 = key[3];
	auto mix = [](uint32_t value) {
		const uint32_t low = static_cast<uint16_t>(value);
		return (static_cast<uint16_t>(low << 4) ^ ((value << 16) >> 21)) + value;
	};

	r4 = (r4 >> 16) - (mix(r5) ^ static_cast<uint16_t>(r7 + 0x540f));
	r5 -= mix(r4) ^ static_cast<uint16_t>(r7 + 0xda56);
	r4 -= mix(r5) ^ static_cast<uint16_t>(r9 + 0xda56);
	uint32_t r6 = r5 - (mix(r4) ^ static_cast<uint16_t>(r8 + 0x609d));
	r5 = r4 - (mix(r6) ^ static_cast<uint16_t>(r10 + 0x609d));
	r6 -= mix(r5) ^ static_cast<uint16_t>(r10 + 0xe6e4);
	r4 = r5 - (mix(r6) ^ static_cast<uint16_t>(r10 + 0xe6e4));
	r6 -= mix(r4) ^ static_cast<uint16_t>(r9 + 0x6d2b);
	r5 = r4 - (mix(r6) ^ static_cast<uint16_t>(r8 + 0x6d2b));
	r6 -= mix(r5) ^ static_cast<uint16_t>(r7 + 0xf372);
	r4 = r5 - (mix(r6) ^ static_cast<uint16_t>(r7 + 0xf372));
	r5 = r6 - (mix(r4) ^ static_cast<uint16_t>(r8 + 0x79b9));
	const uint32_t r3 = r4 - (mix(r5) ^ static_cast<uint16_t>(r9 + 0x79b9));
	const uint32_t r2 = r5 - (mix(r3) ^ static_cast<uint16_t>(r10));
	return static_cast<uint32_t>(static_cast<uint16_t>(r2)) |
		static_cast<uint32_t>(static_cast<uint16_t>(r3)) << 16;
}

// The commercial metadata uses a fixed 1024-bit RSA operation with public
// exponent 3. Keeping this tiny purpose-built unsigned integer here avoids a
// runtime dependency on OpenSSL for one modular exponentiation at startup.
using BigNumber = std::array<uint64_t, 16>;

BigNumber readBigNumber(const uint8_t* bytes)
{
	BigNumber value = {{0}};
	for (size_t word = 0; word < value.size(); ++word)
	{
		for (size_t byte = 0; byte < sizeof(uint64_t); ++byte)
			value[word] |= static_cast<uint64_t>(bytes[word * 8 + byte]) << (byte * 8);
	}
	return value;
}

void writeBigNumber(const BigNumber& value, uint8_t* bytes)
{
	for (size_t word = 0; word < value.size(); ++word)
	{
		for (size_t byte = 0; byte < sizeof(uint64_t); ++byte)
			bytes[word * 8 + byte] = static_cast<uint8_t>(value[word] >> (byte * 8));
	}
}

int compare(const BigNumber& left, const BigNumber& right)
{
	for (size_t index = left.size(); index-- > 0; )
	{
		if (left[index] < right[index])
			return -1;
		if (left[index] > right[index])
			return 1;
	}
	return 0;
}

BigNumber subtract(const BigNumber& left, const BigNumber& right)
{
	BigNumber result = {{0}};
	uint64_t borrow = 0;
	for (size_t index = 0; index < result.size(); ++index)
	{
		const uint64_t subtrahend = right[index] + borrow;
		const bool subtrahendOverflow = subtrahend < right[index];
		result[index] = left[index] - subtrahend;
		borrow = subtrahendOverflow || left[index] < subtrahend;
	}
	return result;
}

void addWithoutOverflow(BigNumber& left, const BigNumber& right)
{
	uint64_t carry = 0;
	for (size_t index = 0; index < left.size(); ++index)
	{
		const uint64_t first = left[index] + right[index];
		const bool firstCarry = first < left[index];
		const uint64_t result = first + carry;
		const bool secondCarry = result < first;
		left[index] = result;
		carry = firstCarry || secondCarry;
	}
}

void addModulo(BigNumber& left, const BigNumber& right, const BigNumber& modulus)
{
	// Both values are below modulus. Comparing against modulus - right lets us
	// perform the addition without ever overflowing the fixed 1024-bit value.
	const BigNumber threshold = subtract(modulus, right);
	if (compare(left, threshold) >= 0)
		left = subtract(left, threshold);
	else
		addWithoutOverflow(left, right);
}

BigNumber reduceModulo(const BigNumber& value, const BigNumber& modulus)
{
	BigNumber result = {{0}};
	const BigNumber one = {{1}};
	for (size_t bit = value.size() * 64; bit-- > 0; )
	{
		const BigNumber doubled = result;
		addModulo(result, doubled, modulus);
		if ((value[bit / 64] & (uint64_t{1} << (bit % 64))) != 0)
			addModulo(result, one, modulus);
	}
	return result;
}

BigNumber multiplyModulo(const BigNumber& left, const BigNumber& right,
	const BigNumber& modulus)
{
	BigNumber result = {{0}};
	BigNumber addend = reduceModulo(left, modulus);
	for (size_t bit = 0; bit < right.size() * 64; ++bit)
	{
		if ((right[bit / 64] & (uint64_t{1} << (bit % 64))) != 0)
			addModulo(result, addend, modulus);
		const BigNumber doubled = addend;
		addModulo(addend, doubled, modulus);
	}
	return result;
}

bool decryptRsaMetadata(const uint8_t* encryptedBytes, const uint8_t* modulusBytes,
	uint8_t* decryptedBytes)
{
	const BigNumber modulus = readBigNumber(modulusBytes);
	const BigNumber encrypted = reduceModulo(readBigNumber(encryptedBytes), modulus);
	const BigNumber squared = multiplyModulo(encrypted, encrypted, modulus);
	const BigNumber decrypted = multiplyModulo(squared, encrypted, modulus);
	writeBigNumber(decrypted, decryptedBytes);
	return true;
}

} // namespace

bool decryptCommercialCode(std::vector<uint8_t>& rom, const VMGPHeader& header, std::string& error)
{
	if ((header.flags & 0x8000) != 0)
	{
		error = "encrypted compressed MPN sections are not supported";
		return false;
	}

	const uint64_t resourceStart = HeaderSize + static_cast<uint64_t>(header.codeSize) + header.dataSize;
	if (resourceStart + sizeof(uint32_t) > rom.size())
	{
		error = "resource table is outside the file";
		return false;
	}

	const uint32_t tableSize = readU32(rom.data() + resourceStart);
	if (tableSize < 8 || tableSize % 4 != 0 || tableSize > header.resSize ||
		resourceStart + tableSize > rom.size())
	{
		error = "resource table is invalid";
		return false;
	}

	std::array<uint8_t, MetaSize> meta = {{0}};
	bool foundMeta = false;
	uint32_t resourceOffset = tableSize;
	const uint32_t resourceCount = tableSize / 4 - 1;
	for (uint32_t index = 0; index < resourceCount; ++index)
	{
		const uint32_t nextOffset = index + 1 == resourceCount ?
			header.resSize : readU32(rom.data() + resourceStart + (index + 1) * 4);
		if (resourceOffset > nextOffset || nextOffset > header.resSize)
		{
			error = "resource offsets are invalid";
			return false;
		}

		const uint64_t absoluteOffset = resourceStart + resourceOffset;
		if (nextOffset - resourceOffset >= 4 && absoluteOffset + 4 <= rom.size() &&
			readU32(rom.data() + absoluteOffset) == 0x4154454d)
		{
			if (absoluteOffset + 9 + meta.size() > rom.size() ||
				nextOffset - resourceOffset < 9 + meta.size())
			{
				error = "META resource is truncated";
				return false;
			}
			std::copy_n(rom.data() + absoluteOffset + 9, meta.size(), meta.data());
			foundMeta = true;
			break;
		}
		resourceOffset = nextOffset;
	}

	if (!foundMeta)
	{
		error = "META resource was not found";
		return false;
	}

	const uint32_t selector = xteaDecryptWord(readU32(meta.data() + 0x8c), SelectorKey.data());
	if ((selector & 0xfffffffcU) != 0)
	{
		error = "META key selector did not decrypt";
		return false;
	}

	const uint8_t* modulusBytes = SonyEricssonKeys.data() + selector * 128;
	std::array<uint8_t, 128> decryptedMeta = {{0}};
	if (!decryptRsaMetadata(meta.data(), modulusBytes, decryptedMeta.data()))
	{
		error = "RSA metadata decryption failed";
		return false;
	}
	for (size_t index = 0x26; index < decryptedMeta.size(); ++index)
	{
		if (decryptedMeta[index] != 0 && decryptedMeta[index] != 1 && decryptedMeta[index] != 0xff)
		{
			error = "RSA metadata padding is invalid";
			return false;
		}
	}

	std::array<uint32_t, 4> key;
	for (size_t index = 0; index < key.size(); ++index)
		key[index] = readU32(decryptedMeta.data() + 0x14 + index * 4);

	if (HeaderSize + header.codeSize > rom.size())
	{
		error = "code section is outside the file";
		return false;
	}
	for (uint32_t offset = 0; offset + 4 <= header.codeSize; offset += 4)
	{
		uint8_t* word = rom.data() + HeaderSize + offset;
		writeU32(word, decryptBlock(readU32(word), key.data()));
	}
	return true;
}
