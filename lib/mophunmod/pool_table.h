#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace mophunmod {

using PoolId = uint32_t;

enum class PoolSegment : uint8_t {
	String = 0,
	Code = 1,
	Data = 2,
	Bss = 4,
	ImmediateFloat = 6
};

class PoolError : public std::runtime_error {
	public:
		explicit PoolError(const std::string& message) : std::runtime_error(message) {}
};

struct PoolEntry {
	uint8_t type = 0;
	uint32_t argument = 0;
	uint32_t value = 0;

	static PoolEntry syscall(uint32_t stringOffset);
	static PoolEntry code(uint32_t codeOffset);
	static PoolEntry data(uint32_t dataOffset);
	static PoolEntry bss(uint32_t bssOffset);

	PoolSegment segment() const { return static_cast<PoolSegment>(type >> 4); }
	uint8_t addressingMode() const { return type & 0x0fU; }
	bool isCallable() const
	{
		return segment() == PoolSegment::String || segment() == PoolSegment::Code;
	}

	bool operator==(const PoolEntry& other) const
	{
		return type == other.type && argument == other.argument && value == other.value;
	}
	bool operator!=(const PoolEntry& other) const { return !(*this == other); }
};

class PoolTable {
	public:
		PoolTable() = default;
		explicit PoolTable(const std::vector<uint8_t>& bytes);

		std::size_t size() const { return entries_.size(); }
		bool empty() const { return entries_.empty(); }
		const PoolEntry& at(PoolId id) const;
		PoolEntry& at(PoolId id);

		PoolId append(const PoolEntry& entry);
		void replace(PoolId id, const PoolEntry& replacement);
		PoolId preserve(PoolId id);
		PoolId replaceAndPreserve(PoolId id, const PoolEntry& replacement);
		void swap(PoolTable& other) noexcept { entries_.swap(other.entries_); }

		std::vector<uint8_t> serialize() const;

	private:
		static void validate(const PoolEntry& entry);
		std::vector<PoolEntry> entries_;
};

} // namespace mophunmod
