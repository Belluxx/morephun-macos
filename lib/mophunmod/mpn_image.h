#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace mophunmod {

constexpr std::size_t MpnHeaderSize = 40;
constexpr std::size_t MpnPoolEntrySize = 8;
constexpr uint16_t MpnCompressedSectionsFlag = 0x8000;

class MpnError : public std::runtime_error {
	public:
		explicit MpnError(const std::string& message) : std::runtime_error(message) {}
};

struct MpnHeader {
	std::array<char, 4> magic;
	uint32_t heapSize;
	uint16_t stackSize;
	uint16_t flags;
	uint32_t codeSize;
	uint32_t dataSize;
	uint32_t bssSize;
	uint32_t resourceSize;
	uint32_t directorySize;
	uint32_t poolSize;
	uint32_t stringSize;
};

// An uncompressed VMGP/MPN split into its file-backed sections. Parsing owns a
// copy of every byte so a failed patch cannot partially alter the caller's input.
class MpnImage {
	public:
		static MpnImage parse(const std::vector<uint8_t>& bytes);
		static MpnImage parse(const uint8_t* bytes, std::size_t size);

		const MpnHeader& header() const { return header_; }
		const std::vector<uint8_t>& code() const { return code_; }
		const std::vector<uint8_t>& data() const { return data_; }
		const std::vector<uint8_t>& resources() const { return resources_; }
		const std::vector<uint8_t>& poolBytes() const { return pool_; }
		const std::vector<uint8_t>& strings() const { return strings_; }
		const std::vector<uint8_t>& trailingData() const { return trailingData_; }

		std::size_t codeFileOffset() const { return MpnHeaderSize; }
		std::size_t dataFileOffset() const;
		std::size_t resourceFileOffset() const;
		std::size_t poolFileOffset() const;
		std::size_t stringFileOffset() const;
		std::size_t describedFileSize() const;

		// Validates the known pool encodings against their referenced sections. Unknown
		// segment encodings remain round-trippable for forward compatibility.
		void validateReferences() const;
		std::vector<uint8_t> serialize() const;

	private:
		friend class PatchBuilder;

		std::array<uint8_t, MpnHeaderSize> headerBytes_;
		MpnHeader header_;
		std::vector<uint8_t> code_;
		std::vector<uint8_t> data_;
		std::vector<uint8_t> resources_;
		std::vector<uint8_t> pool_;
		std::vector<uint8_t> strings_;
		std::vector<uint8_t> trailingData_;
};

} // namespace mophunmod
