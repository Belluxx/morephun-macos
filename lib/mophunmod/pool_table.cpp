#include "mophunmod/pool_table.h"

#include <limits>

namespace mophunmod {
namespace {

uint32_t readU32(const uint8_t* bytes)
{
	return static_cast<uint32_t>(bytes[0]) |
		static_cast<uint32_t>(bytes[1]) << 8 |
		static_cast<uint32_t>(bytes[2]) << 16 |
		static_cast<uint32_t>(bytes[3]) << 24;
}

void appendU32(std::vector<uint8_t>& bytes, uint32_t value)
{
	bytes.push_back(static_cast<uint8_t>(value));
	bytes.push_back(static_cast<uint8_t>(value >> 8));
	bytes.push_back(static_cast<uint8_t>(value >> 16));
	bytes.push_back(static_cast<uint8_t>(value >> 24));
}

} // namespace

PoolEntry PoolEntry::syscall(uint32_t stringOffset)
{
	return {0x02, stringOffset, 0};
}

PoolEntry PoolEntry::code(uint32_t codeOffset)
{
	return {0x11, 0, codeOffset};
}

PoolEntry PoolEntry::data(uint32_t dataOffset)
{
	return {0x21, 0, dataOffset};
}

PoolEntry PoolEntry::bss(uint32_t bssOffset)
{
	return {0x41, 0, bssOffset};
}

PoolTable::PoolTable(const std::vector<uint8_t>& bytes)
{
	if (bytes.size() % 8 != 0)
		throw PoolError("Pool byte size is not a multiple of eight");
	entries_.reserve(bytes.size() / 8);
	for (std::size_t offset = 0; offset < bytes.size(); offset += 8)
	{
		const uint32_t descriptor = readU32(bytes.data() + offset);
		entries_.push_back({static_cast<uint8_t>(descriptor), descriptor >> 8,
			readU32(bytes.data() + offset + 4)});
	}
}

const PoolEntry& PoolTable::at(PoolId id) const
{
	if (id == 0 || id > entries_.size())
		throw PoolError("Pool ID " + std::to_string(id) + " is out of range");
	return entries_[id - 1];
}

PoolEntry& PoolTable::at(PoolId id)
{
	if (id == 0 || id > entries_.size())
		throw PoolError("Pool ID " + std::to_string(id) + " is out of range");
	return entries_[id - 1];
}

PoolId PoolTable::append(const PoolEntry& entry)
{
	validate(entry);
	if (entries_.size() >= std::numeric_limits<PoolId>::max())
		throw PoolError("Pool has reached the maximum representable entry count");
	entries_.push_back(entry);
	return static_cast<PoolId>(entries_.size());
}

void PoolTable::replace(PoolId id, const PoolEntry& replacement)
{
	validate(replacement);
	at(id) = replacement;
}

PoolId PoolTable::preserve(PoolId id)
{
	const PoolEntry original = at(id);
	return append(original);
}

PoolId PoolTable::replaceAndPreserve(PoolId id, const PoolEntry& replacement)
{
	const PoolEntry original = at(id);
	validate(replacement);
	const PoolId preserved = append(original);
	at(id) = replacement;
	return preserved;
}

std::vector<uint8_t> PoolTable::serialize() const
{
	if (entries_.size() > std::numeric_limits<std::size_t>::max() / 8U)
		throw PoolError("Serialized pool size overflows the host address space");
	std::vector<uint8_t> bytes;
	bytes.reserve(entries_.size() * 8);
	for (const PoolEntry& entry : entries_)
	{
		validate(entry);
		appendU32(bytes, static_cast<uint32_t>(entry.type) | (entry.argument << 8));
		appendU32(bytes, entry.value);
	}
	return bytes;
}

void PoolTable::validate(const PoolEntry& entry)
{
	if (entry.argument > 0x00ffffffU)
		throw PoolError("Pool entry argument exceeds its 24-bit field");
}

} // namespace mophunmod
