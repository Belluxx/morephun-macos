#include "mophunmod/mpn_image.h"

#include "mophunmod/pool_table.h"

#include <algorithm>
#include <limits>

namespace mophunmod {
namespace {

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

std::size_t checkedAdd(std::size_t left, uint64_t right, const char* section)
{
	if (right > std::numeric_limits<std::size_t>::max() - left)
		throw MpnError(std::string("MPN ") + section + " size overflows the host address space");
	return left + static_cast<std::size_t>(right);
}

std::vector<uint8_t> copyRange(const uint8_t* bytes, std::size_t begin, std::size_t end)
{
	return std::vector<uint8_t>(bytes + begin, bytes + end);
}

} // namespace

MpnImage MpnImage::parse(const std::vector<uint8_t>& bytes)
{
	return parse(bytes.data(), bytes.size());
}

MpnImage MpnImage::parse(const uint8_t* bytes, std::size_t size)
{
	if (size < MpnHeaderSize)
		throw MpnError("MPN is smaller than the 40-byte VMGP header");
	if (bytes == nullptr)
		throw MpnError("MPN input pointer is null");
	if (!std::equal(bytes, bytes + 4, "VMGP"))
		throw MpnError("MPN does not have the VMGP magic number");

	MpnImage image;
	std::copy_n(bytes, MpnHeaderSize, image.headerBytes_.begin());
	std::copy_n(reinterpret_cast<const char*>(bytes), 4, image.header_.magic.begin());
	image.header_.heapSize = readU32(bytes + 4);
	image.header_.stackSize = readU16(bytes + 8);
	image.header_.flags = readU16(bytes + 10);
	image.header_.codeSize = readU32(bytes + 12);
	image.header_.dataSize = readU32(bytes + 16);
	image.header_.bssSize = readU32(bytes + 20);
	image.header_.resourceSize = readU32(bytes + 24);
	image.header_.directorySize = readU32(bytes + 28);
	image.header_.poolSize = readU32(bytes + 32);
	image.header_.stringSize = readU32(bytes + 36);

	if ((image.header_.flags & MpnCompressedSectionsFlag) != 0)
		throw MpnError("Compressed MPN sections are not supported");
	if ((image.header_.codeSize & 3U) != 0)
		throw MpnError("MPN code section size is not instruction-aligned");

	std::size_t offset = MpnHeaderSize;
	auto consume = [&](uint64_t length, const char* name) {
		const std::size_t end = checkedAdd(offset, length, name);
		if (end > size)
			throw MpnError(std::string("MPN ") + name + " section exceeds the file size");
		const std::size_t begin = offset;
		offset = end;
		return copyRange(bytes, begin, end);
	};

	image.code_ = consume(image.header_.codeSize, "code");
	image.data_ = consume(image.header_.dataSize, "data");
	image.resources_ = consume(image.header_.resourceSize, "resource");
	image.pool_ = consume(static_cast<uint64_t>(image.header_.poolSize) * MpnPoolEntrySize,
		"pool");
	image.strings_ = consume(image.header_.stringSize, "string");
	if (!image.strings_.empty() && image.strings_.back() != 0)
		throw MpnError("MPN string section is not NUL-terminated");
	image.trailingData_ = copyRange(bytes, offset, size);
	image.validateReferences();
	return image;
}

std::size_t MpnImage::dataFileOffset() const
{
	return MpnHeaderSize + code_.size();
}

std::size_t MpnImage::resourceFileOffset() const
{
	return dataFileOffset() + data_.size();
}

std::size_t MpnImage::poolFileOffset() const
{
	return resourceFileOffset() + resources_.size();
}

std::size_t MpnImage::stringFileOffset() const
{
	return poolFileOffset() + pool_.size();
}

std::size_t MpnImage::describedFileSize() const
{
	return stringFileOffset() + strings_.size();
}

void MpnImage::validateReferences() const
{
	const PoolTable pool(pool_);
	for (std::size_t index = 0; index < pool.size(); ++index)
	{
		const PoolId id = static_cast<PoolId>(index + 1);
		const PoolEntry& entry = pool.at(id);
		const uint8_t mode = entry.addressingMode();
		bool valid = true;
		const char* target = "unknown";
		if (mode == 4)
		{
			target = "relocation data";
			valid = (entry.argument == 1 || entry.argument == 2 || entry.argument == 4) &&
				entry.value <= data_.size() && data_.size() - entry.value >= sizeof(uint32_t);
		}
		else if (mode == 8)
		{
			target = "base pool entry";
			valid = entry.argument != 0 && entry.argument <= pool.size();
		}
		else
		{
			switch (entry.segment())
			{
			case PoolSegment::String:
			{
				target = "string";
				valid = entry.argument < strings_.size();
				if (valid)
				{
					const auto begin = strings_.begin() + entry.argument;
					valid = std::find(begin, strings_.end(), 0) != strings_.end();
				}
				break;
			}
			case PoolSegment::Code:
				target = "code";
				valid = entry.value < code_.size() && (entry.value & 3U) == 0;
				break;
			case PoolSegment::Data:
				target = "data";
				valid = entry.value < data_.size();
				break;
			case PoolSegment::Bss:
				target = "BSS";
				valid = entry.value < header_.bssSize;
				break;
			case PoolSegment::ImmediateFloat:
				break;
			default:
				break;
			}
		}
		if (!valid)
			throw MpnError("Pool entry " + std::to_string(id) + " has an out-of-range " +
				target + " reference");
	}

	// Relative pool entries recursively resolve their base entry at runtime. Reject
	// cycles here so malformed input cannot recurse indefinitely during VM startup.
	std::vector<uint8_t> states(pool.size(), 0);
	for (std::size_t start = 0; start < pool.size(); ++start)
	{
		if (states[start] != 0)
			continue;
		std::vector<std::size_t> path;
		std::size_t current = start;
		bool reachedTerminal = false;
		while (states[current] == 0)
		{
			states[current] = 1;
			path.push_back(current);
			const PoolEntry& entry = pool.at(static_cast<PoolId>(current + 1));
			if (entry.addressingMode() != 8)
			{
				reachedTerminal = true;
				break;
			}
			current = entry.argument - 1U;
		}
		if (!reachedTerminal && states[current] == 1 &&
			std::find(path.begin(), path.end(), current) != path.end())
			throw MpnError("Relative pool entry cycle includes pool ID " +
				std::to_string(current + 1));
		for (const std::size_t index : path)
			states[index] = 2;
	}
}

std::vector<uint8_t> MpnImage::serialize() const
{
	if (code_.size() > std::numeric_limits<uint32_t>::max() ||
		data_.size() > std::numeric_limits<uint32_t>::max() ||
		resources_.size() > std::numeric_limits<uint32_t>::max() ||
		strings_.size() > std::numeric_limits<uint32_t>::max() ||
		pool_.size() % MpnPoolEntrySize != 0 ||
		pool_.size() / MpnPoolEntrySize > std::numeric_limits<uint32_t>::max())
		throw MpnError("Modified MPN section size cannot be represented in its header");

	std::array<uint8_t, MpnHeaderSize> encodedHeader = headerBytes_;
	writeU32(encodedHeader.data() + 12, static_cast<uint32_t>(code_.size()));
	writeU32(encodedHeader.data() + 16, static_cast<uint32_t>(data_.size()));
	writeU32(encodedHeader.data() + 20, header_.bssSize);
	writeU32(encodedHeader.data() + 24, static_cast<uint32_t>(resources_.size()));
	writeU32(encodedHeader.data() + 32,
		static_cast<uint32_t>(pool_.size() / MpnPoolEntrySize));
	writeU32(encodedHeader.data() + 36, static_cast<uint32_t>(strings_.size()));

	std::size_t total = MpnHeaderSize;
	auto includeSize = [&](std::size_t size) {
		if (size > std::numeric_limits<std::size_t>::max() - total)
			throw MpnError("Serialized MPN is too large for the host address space");
		total += size;
	};
	includeSize(code_.size());
	includeSize(data_.size());
	includeSize(resources_.size());
	includeSize(pool_.size());
	includeSize(strings_.size());
	includeSize(trailingData_.size());
	std::vector<uint8_t> output;
	output.reserve(total);
	output.insert(output.end(), encodedHeader.begin(), encodedHeader.end());
	output.insert(output.end(), code_.begin(), code_.end());
	output.insert(output.end(), data_.begin(), data_.end());
	output.insert(output.end(), resources_.begin(), resources_.end());
	output.insert(output.end(), pool_.begin(), pool_.end());
	output.insert(output.end(), strings_.begin(), strings_.end());
	output.insert(output.end(), trailingData_.begin(), trailingData_.end());
	return output;
}

} // namespace mophunmod
