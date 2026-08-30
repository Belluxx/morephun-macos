#include "mophunmod/patch_builder.h"

#include "mophunmod/target.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace mophunmod {

PatchBuilder::PatchBuilder(const MpnImage& image) : image_(image), pool_(image.pool_) {}

PatchBuilder::PatchBuilder(MpnImage&& image) : image_(std::move(image)), pool_(image_.pool_) {}

uint32_t PatchBuilder::alignedOffset(uint64_t size, uint32_t alignment, const char* section)
{
	if (alignment == 0 || (alignment & (alignment - 1U)) != 0)
		throw MpnError(std::string(section) + " alignment must be a non-zero power of two");
	const uint64_t aligned = (size + alignment - 1U) & ~static_cast<uint64_t>(alignment - 1U);
	if (aligned > std::numeric_limits<uint32_t>::max())
		throw MpnError(std::string(section) + " allocation offset exceeds 32 bits");
	return static_cast<uint32_t>(aligned);
}

uint32_t PatchBuilder::nextCodeOffset(uint32_t alignment) const
{
	return alignedOffset(image_.code_.size(), alignment, "Code");
}

uint32_t PatchBuilder::nextDataOffset(uint32_t alignment) const
{
	return alignedOffset(image_.data_.size(), alignment, "Data");
}

uint32_t PatchBuilder::nextBssOffset(uint32_t alignment) const
{
	return alignedOffset(image_.header_.bssSize, alignment, "BSS");
}

uint32_t PatchBuilder::appendSection(std::vector<uint8_t>& section,
	const std::vector<uint8_t>& bytes, uint32_t alignment, uint8_t padding,
	const char* sectionName)
{
	const uint32_t offset = alignedOffset(section.size(), alignment, sectionName);
	const uint64_t finalSize = static_cast<uint64_t>(offset) + bytes.size();
	if (finalSize > std::numeric_limits<uint32_t>::max())
		throw MpnError(std::string(sectionName) + " allocation exceeds the MPN section limit");

	std::vector<uint8_t> updated = section;
	updated.resize(offset, padding);
	updated.insert(updated.end(), bytes.begin(), bytes.end());
	section.swap(updated);
	return offset;
}

SectionAllocation PatchBuilder::allocateCode(const std::vector<uint8_t>& bytes,
	uint32_t alignment, uint8_t padding)
{
	if ((alignment & 3U) != 0)
		throw MpnError("Code alignment must be a multiple of four");
	if ((bytes.size() & 3U) != 0)
		throw MpnError("Allocated PIP2 code size must be a multiple of four");
	const uint32_t offset = appendSection(image_.code_, bytes, alignment, padding, "Code");
	image_.header_.codeSize = static_cast<uint32_t>(image_.code_.size());
	return {SectionKind::Code, offset, static_cast<uint32_t>(bytes.size())};
}

SectionAllocation PatchBuilder::allocateData(const std::vector<uint8_t>& bytes,
	uint32_t alignment, uint8_t padding)
{
	const uint32_t offset = appendSection(image_.data_, bytes, alignment, padding, "Data");
	image_.header_.dataSize = static_cast<uint32_t>(image_.data_.size());
	return {SectionKind::Data, offset, static_cast<uint32_t>(bytes.size())};
}

SectionAllocation PatchBuilder::allocateBss(uint32_t size, uint32_t alignment)
{
	const uint32_t offset = nextBssOffset(alignment);
	const uint64_t finalSize = static_cast<uint64_t>(offset) + size;
	if (finalSize > std::numeric_limits<uint32_t>::max())
		throw MpnError("BSS allocation exceeds the MPN section limit");
	image_.header_.bssSize = static_cast<uint32_t>(finalSize);
	return {SectionKind::Bss, offset, size};
}

void PatchBuilder::alignData(uint32_t alignment, uint8_t padding)
{
	const uint32_t aligned = nextDataOffset(alignment);
	image_.data_.resize(aligned, padding);
	image_.header_.dataSize = aligned;
}

uint32_t PatchBuilder::allocateString(const std::string& value)
{
	if (value.find('\0') != std::string::npos)
		throw MpnError("Allocated MPN string contains an embedded NUL byte");
	if (image_.strings_.size() > std::numeric_limits<uint32_t>::max())
		throw MpnError("String allocation offset exceeds 32 bits");
	const uint32_t offset = static_cast<uint32_t>(image_.strings_.size());
	const uint64_t finalSize = static_cast<uint64_t>(offset) + value.size() + 1U;
	if (finalSize > std::numeric_limits<uint32_t>::max())
		throw MpnError("String allocation exceeds the MPN section limit");
	std::vector<uint8_t> updated = image_.strings_;
	updated.insert(updated.end(), value.begin(), value.end());
	updated.push_back(0);
	image_.strings_.swap(updated);
	image_.header_.stringSize = static_cast<uint32_t>(finalSize);
	return offset;
}

bool PatchBuilder::findString(const std::string& value, uint32_t& result) const
{
	for (std::size_t offset = 0; offset < image_.strings_.size();)
	{
		const auto begin = image_.strings_.begin() + offset;
		const auto terminator = std::find(begin, image_.strings_.end(), 0);
		if (terminator == image_.strings_.end())
			return false;
		if (static_cast<std::size_t>(terminator - begin) == value.size() &&
			std::equal(value.begin(), value.end(), begin))
		{
			result = static_cast<uint32_t>(offset);
			return true;
		}
		offset = static_cast<std::size_t>(terminator - image_.strings_.begin()) + 1U;
	}
	return false;
}

bool PatchBuilder::stringEquals(uint32_t offset, const std::string& value) const
{
	if (offset >= image_.strings_.size())
		return false;
	const auto begin = image_.strings_.begin() + offset;
	const auto terminator = std::find(begin, image_.strings_.end(), 0);
	return terminator != image_.strings_.end() &&
		static_cast<std::size_t>(terminator - begin) == value.size() &&
		std::equal(value.begin(), value.end(), begin);
}

uint32_t PatchBuilder::internString(const std::string& value)
{
	if (value.find('\0') != std::string::npos)
		throw MpnError("Interned MPN string contains an embedded NUL byte");
	if (!image_.strings_.empty() && image_.strings_.back() != 0)
		throw MpnError("Cannot intern into an unterminated MPN string section");
	uint32_t offset = 0;
	if (findString(value, offset))
		return offset;
	return allocateString(value);
}

void PatchBuilder::markModified(const std::string& targetId, const std::string& modId)
{
	internString(makeModificationMarker(targetId, modId));
}

PoolId PatchBuilder::addPoolEntry(const PoolEntry& entry)
{
	const PoolId id = pool_.append(entry);
	image_.header_.poolSize = static_cast<uint32_t>(pool_.size());
	return id;
}

PoolId PatchBuilder::addReference(const SectionAllocation& allocation)
{
	if (allocation.size == 0)
		throw PoolError("Cannot create a pool reference to an empty allocation");
	const uint64_t end = static_cast<uint64_t>(allocation.offset) + allocation.size;
	switch (allocation.section)
	{
	case SectionKind::Code:
		if (end > image_.code_.size())
			throw PoolError("Code allocation is outside the code section");
		return addPoolEntry(PoolEntry::code(allocation.offset));
	case SectionKind::Data:
		if (end > image_.data_.size())
			throw PoolError("Data allocation is outside the data section");
		return addPoolEntry(PoolEntry::data(allocation.offset));
	case SectionKind::Bss:
		if (end > image_.header_.bssSize)
			throw PoolError("BSS allocation is outside the BSS section");
		return addPoolEntry(PoolEntry::bss(allocation.offset));
	}
	throw PoolError("Unknown allocation section");
}

PoolId PatchBuilder::findImportedSyscall(const std::string& name) const
{
	for (std::size_t index = 0; index < pool_.size(); ++index)
	{
		const PoolId id = static_cast<PoolId>(index + 1);
		const PoolEntry& entry = pool_.at(id);
		if (entry.segment() == PoolSegment::String && stringEquals(entry.argument, name))
			return id;
	}
	return 0;
}

PoolId PatchBuilder::importSyscall(const std::string& name)
{
	if (name.empty())
		throw PoolError("Syscall name cannot be empty");
	if (name.find('\0') != std::string::npos)
		throw PoolError("Syscall name contains an embedded NUL byte");
	const PoolId existing = findImportedSyscall(name);
	if (existing != 0)
		return existing;

	uint32_t stringOffset = 0;
	const bool stringExists = findString(name, stringOffset);
	std::vector<uint8_t> updatedStrings = image_.strings_;
	if (!stringExists)
	{
		if (updatedStrings.size() > 0x00ffffffU)
			throw PoolError("Syscall string offset exceeds the pool entry's 24-bit field");
		stringOffset = static_cast<uint32_t>(updatedStrings.size());
		const uint64_t finalSize = static_cast<uint64_t>(updatedStrings.size()) +
			name.size() + 1U;
		if (finalSize > std::numeric_limits<uint32_t>::max())
			throw MpnError("Syscall name exceeds the MPN string section limit");
		updatedStrings.insert(updatedStrings.end(), name.begin(), name.end());
		updatedStrings.push_back(0);
	}
	if (stringOffset > 0x00ffffffU)
		throw PoolError("Syscall string offset exceeds the pool entry's 24-bit field");

	PoolTable updatedPool = pool_;
	const PoolId id = updatedPool.append(PoolEntry::syscall(stringOffset));
	image_.strings_.swap(updatedStrings);
	pool_.swap(updatedPool);
	image_.header_.stringSize = static_cast<uint32_t>(image_.strings_.size());
	image_.header_.poolSize = static_cast<uint32_t>(pool_.size());
	return id;
}

void PatchBuilder::replacePoolEntry(PoolId id, const PoolEntry& replacement)
{
	pool_.replace(id, replacement);
}

PoolId PatchBuilder::replacePoolEntryRetainingOriginal(PoolId id,
	const PoolEntry& replacement)
{
	const PoolId retained = pool_.replaceAndPreserve(id, replacement);
	image_.header_.poolSize = static_cast<uint32_t>(pool_.size());
	return retained;
}

CodeHook PatchBuilder::reserveCodeHook(PoolId targetPoolId)
{
	const PoolEntry& target = pool_.at(targetPoolId);
	if (!target.isCallable())
		throw PoolError("Pool entry " + std::to_string(targetPoolId) +
			" is not a callable code or syscall target");
	for (const CodeHook& pending : pendingCodeHooks_)
	{
		if (pending.targetPoolId_ == targetPoolId)
			throw PoolError("Pool entry " + std::to_string(targetPoolId) +
				" already has a pending hook");
	}
	if (nextHookToken_ == 0)
		throw PoolError("Code hook token space is exhausted");

	PoolTable updatedPool = pool_;
	const PoolId originalPoolId = updatedPool.preserve(targetPoolId);
	std::vector<CodeHook> updatedHooks = pendingCodeHooks_;
	updatedHooks.push_back(CodeHook(this, targetPoolId, originalPoolId, nextHookToken_));
	pool_.swap(updatedPool);
	pendingCodeHooks_.swap(updatedHooks);
	image_.header_.poolSize = static_cast<uint32_t>(pool_.size());
	return CodeHook(this, targetPoolId, originalPoolId, nextHookToken_++);
}

void PatchBuilder::bindCodeHook(const CodeHook& hook, uint32_t codeOffset)
{
	if (codeOffset >= image_.code_.size())
		throw PoolError("Hook target is outside the code section");
	auto found = std::find_if(pendingCodeHooks_.begin(), pendingCodeHooks_.end(),
		[&](const CodeHook& pending) { return pending.token_ == hook.token_; });
	if (hook.owner_ != this || found == pendingCodeHooks_.end() ||
		found->targetPoolId_ != hook.targetPoolId_ ||
		found->originalPoolId_ != hook.originalPoolId_)
		throw PoolError("Code hook handle is invalid, foreign, or already bound");

	PoolTable updatedPool = pool_;
	updatedPool.replace(hook.targetPoolId_, PoolEntry::code(codeOffset));
	std::vector<CodeHook> updatedHooks = pendingCodeHooks_;
	updatedHooks.erase(updatedHooks.begin() + (found - pendingCodeHooks_.begin()));
	pool_.swap(updatedPool);
	pendingCodeHooks_.swap(updatedHooks);
}

MpnImage PatchBuilder::finishImage() const
{
	if (!pendingCodeHooks_.empty())
		throw PoolError("Pool entry " +
			std::to_string(pendingCodeHooks_.front().targetPoolId_) +
			" has an unbound code hook");
	MpnImage result = image_;
	result.pool_ = pool_.serialize();
	result.header_.poolSize = static_cast<uint32_t>(pool_.size());
	result.validateReferences();
	return result;
}

} // namespace mophunmod
