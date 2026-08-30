#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mophunmod/mpn_image.h"
#include "mophunmod/pool_table.h"

namespace mophunmod {

class PatchBuilder;

enum class SectionKind {
	Code,
	Data,
	Bss
};

// Describes the requested bytes of an allocation; alignment padding is owned by
// the surrounding section and is not included in size.
struct SectionAllocation {
	SectionKind section;
	uint32_t offset;
	uint32_t size;
};

// A builder-owned two-phase hook. Reserving it creates a stable callable copy of
// the original pool entry; binding it later installs the assembled code target.
class CodeHook {
	public:
		PoolId targetPoolId() const { return targetPoolId_; }
		PoolId originalPoolId() const { return originalPoolId_; }

	private:
		friend class PatchBuilder;
		CodeHook(const PatchBuilder* owner, PoolId targetPoolId, PoolId originalPoolId,
			uint32_t token) : owner_(owner), targetPoolId_(targetPoolId),
			originalPoolId_(originalPoolId), token_(token) {}

		const PatchBuilder* owner_;
		PoolId targetPoolId_;
		PoolId originalPoolId_;
		uint32_t token_;
};

class PatchBuilder {
	public:
		explicit PatchBuilder(const MpnImage& image);
		explicit PatchBuilder(MpnImage&& image);
		PatchBuilder(const PatchBuilder&) = delete;
		PatchBuilder& operator=(const PatchBuilder&) = delete;
		PatchBuilder(PatchBuilder&&) = delete;
		PatchBuilder& operator=(PatchBuilder&&) = delete;

		const MpnHeader& header() const { return image_.header_; }
		const PoolEntry& poolEntry(PoolId id) const { return pool_.at(id); }
		std::size_t poolSize() const { return pool_.size(); }

		uint32_t nextCodeOffset(uint32_t alignment = 4) const;
		uint32_t nextDataOffset(uint32_t alignment = 1) const;
		uint32_t nextBssOffset(uint32_t alignment = 1) const;

		SectionAllocation allocateCode(const std::vector<uint8_t>& bytes,
			uint32_t alignment = 4,
			uint8_t padding = 0);
		SectionAllocation allocateData(const std::vector<uint8_t>& bytes,
			uint32_t alignment = 1,
			uint8_t padding = 0);
		SectionAllocation allocateBss(uint32_t size, uint32_t alignment = 1);
		void alignData(uint32_t alignment, uint8_t padding = 0);

		uint32_t allocateString(const std::string& value);
		uint32_t internString(const std::string& value);
		PoolId addPoolEntry(const PoolEntry& entry);
		PoolId addReference(const SectionAllocation& allocation);
		PoolId findImportedSyscall(const std::string& name) const;
		PoolId importSyscall(const std::string& name);

		void replacePoolEntry(PoolId id, const PoolEntry& replacement);
		PoolId replacePoolEntryRetainingOriginal(PoolId id, const PoolEntry& replacement);
		CodeHook reserveCodeHook(PoolId targetPoolId);
		void bindCodeHook(const CodeHook& hook, uint32_t codeOffset);

		MpnImage finishImage() const;
		std::vector<uint8_t> serialize() const { return finishImage().serialize(); }

	private:
		static uint32_t alignedOffset(uint64_t size, uint32_t alignment,
			const char* section);
		static uint32_t appendSection(std::vector<uint8_t>& section,
			const std::vector<uint8_t>& bytes, uint32_t alignment, uint8_t padding,
			const char* sectionName);
		bool findString(const std::string& value, uint32_t& offset) const;
		bool stringEquals(uint32_t offset, const std::string& value) const;

		MpnImage image_;
		PoolTable pool_;
		std::vector<CodeHook> pendingCodeHooks_;
		uint32_t nextHookToken_ = 1;
};

} // namespace mophunmod
