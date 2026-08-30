#include "mophunmod/target.h"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace mophunmod {

TargetProfile makeVrally2Rc14EuM5Target();

namespace {

constexpr const char* ModificationPrefix = "mophunmod:1:target=";
constexpr const char* ModificationSeparator = ":mod=";

bool validIdentifier(const std::string& value)
{
	if (value.empty())
		return false;
	return std::all_of(value.begin(), value.end(), [](char character) {
		return (character >= 'a' && character <= 'z') ||
			(character >= 'A' && character <= 'Z') ||
			(character >= '0' && character <= '9') || character == '-' ||
			character == '_' || character == '.';
	});
}

std::string poolEntryText(const PoolEntry& entry)
{
	std::ostringstream output;
	output << "type 0x" << std::hex << std::setw(2) << std::setfill('0')
		<< static_cast<unsigned>(entry.type) << std::dec << ", argument "
		<< entry.argument << ", value " << entry.value;
	return output.str();
}

std::string readImageString(const MpnImage& image, uint32_t offset)
{
	if (offset >= image.strings().size())
		throw TargetError("String offset " + std::to_string(offset) + " is out of range");
	const auto begin = image.strings().begin() + offset;
	const auto end = std::find(begin, image.strings().end(), 0);
	if (end == image.strings().end())
		throw TargetError("String offset " + std::to_string(offset) + " is unterminated");
	return std::string(begin, end);
}

template <typename Value>
std::string decimalMismatch(const char* field, Value expected, Value actual)
{
	return std::string(field) + " is " + std::to_string(actual) +
		" (expected " + std::to_string(expected) + ")";
}

} // namespace

TargetSymbol::TargetSymbol(const std::string& name, TargetSymbolKind kind,
	TargetSymbolSource source, uint32_t fixedValue, const std::string& importedName,
	const std::vector<uint8_t>& signature, bool hasExpectedPoolEntry,
	const PoolEntry& expectedPoolEntry) : name_(name), kind_(kind), source_(source),
	fixedValue_(fixedValue), importedName_(importedName), signature_(signature),
	hasExpectedPoolEntry_(hasExpectedPoolEntry), expectedPoolEntry_(expectedPoolEntry)
{
	if (name.empty())
		throw TargetError("Target symbol name cannot be empty");
	if (source == TargetSymbolSource::ImportedSyscall &&
		(kind != TargetSymbolKind::PoolEntry || importedName.empty()))
		throw TargetError("Imported target symbol '" + name +
			"' must name a syscall pool entry");
	if (!signature.empty() && kind != TargetSymbolKind::CodeOffset)
		throw TargetError("Only code-offset symbols can have byte signatures");
}

TargetSymbol TargetSymbol::importedPool(const std::string& name,
	const std::string& syscallName)
{
	return TargetSymbol(name, TargetSymbolKind::PoolEntry,
		TargetSymbolSource::ImportedSyscall, 0, syscallName, {}, false, {});
}

TargetSymbol TargetSymbol::fixedPool(const std::string& name, PoolId id,
	const PoolEntry& expectedEntry)
{
	return TargetSymbol(name, TargetSymbolKind::PoolEntry, TargetSymbolSource::Fixed,
		id, "", {}, true, expectedEntry);
}

TargetSymbol TargetSymbol::codeOffset(const std::string& name, uint32_t offset,
	const std::vector<uint8_t>& signature)
{
	return TargetSymbol(name, TargetSymbolKind::CodeOffset, TargetSymbolSource::Fixed,
		offset, "", signature, false, {});
}

TargetSymbol TargetSymbol::dataOffset(const std::string& name, uint32_t offset)
{
	return TargetSymbol(name, TargetSymbolKind::DataOffset, TargetSymbolSource::Fixed,
		offset, "", {}, false, {});
}

TargetSymbol TargetSymbol::bssOffset(const std::string& name, uint32_t offset)
{
	return TargetSymbol(name, TargetSymbolKind::BssOffset, TargetSymbolSource::Fixed,
		offset, "", {}, false, {});
}

TargetSymbol TargetSymbol::constant(const std::string& name, uint32_t value)
{
	return TargetSymbol(name, TargetSymbolKind::Constant, TargetSymbolSource::Fixed,
		value, "", {}, false, {});
}

bool TargetFingerprint::matches(const MpnHeader& header) const
{
	return heapSize == header.heapSize && stackSize == header.stackSize &&
		flags == header.flags && codeSize == header.codeSize &&
		dataSize == header.dataSize && bssSize == header.bssSize &&
		resourceSize == header.resourceSize && directorySize == header.directorySize &&
		poolSize == header.poolSize && stringSize == header.stringSize;
}

std::string TargetFingerprint::mismatch(const MpnHeader& header) const
{
	if (heapSize != header.heapSize)
		return decimalMismatch("heap size", heapSize, header.heapSize);
	if (stackSize != header.stackSize)
		return decimalMismatch("stack size", stackSize, header.stackSize);
	if (flags != header.flags)
		return decimalMismatch("flags", flags, header.flags);
	if (codeSize != header.codeSize)
		return decimalMismatch("code size", codeSize, header.codeSize);
	if (dataSize != header.dataSize)
		return decimalMismatch("data size", dataSize, header.dataSize);
	if (bssSize != header.bssSize)
		return decimalMismatch("BSS size", bssSize, header.bssSize);
	if (resourceSize != header.resourceSize)
		return decimalMismatch("resource size", resourceSize, header.resourceSize);
	if (directorySize != header.directorySize)
		return decimalMismatch("directory size", directorySize, header.directorySize);
	if (poolSize != header.poolSize)
		return decimalMismatch("pool size", poolSize, header.poolSize);
	if (stringSize != header.stringSize)
		return decimalMismatch("string size", stringSize, header.stringSize);
	return "header matches";
}

uint32_t ResolvedTarget::value(const std::string& name, TargetSymbolKind kind) const
{
	const auto found = std::find_if(symbols_.begin(), symbols_.end(),
		[&](const ResolvedSymbol& symbol) { return symbol.name == name; });
	if (found == symbols_.end())
		throw TargetError("Target '" + profile().id() + "' has no symbol named '" +
			name + "'");
	if (found->kind != kind)
		throw TargetError("Target symbol '" + name + "' is " +
			targetSymbolKindName(found->kind) + ", not " + targetSymbolKindName(kind));
	return found->value;
}

PoolId ResolvedTarget::pool(const std::string& name) const
{
	return value(name, TargetSymbolKind::PoolEntry);
}

uint32_t ResolvedTarget::codeOffset(const std::string& name) const
{
	return value(name, TargetSymbolKind::CodeOffset);
}

uint32_t ResolvedTarget::dataOffset(const std::string& name) const
{
	return value(name, TargetSymbolKind::DataOffset);
}

uint32_t ResolvedTarget::bssOffset(const std::string& name) const
{
	return value(name, TargetSymbolKind::BssOffset);
}

uint32_t ResolvedTarget::constant(const std::string& name) const
{
	return value(name, TargetSymbolKind::Constant);
}

TargetProfile::TargetProfile(const std::string& id, const std::string& displayName,
	const TargetFingerprint& fingerprint, const std::vector<TargetSymbol>& symbols) :
	id_(id), displayName_(displayName), fingerprint_(fingerprint), symbols_(symbols)
{
	if (!validIdentifier(id))
		throw TargetError("Target ID '" + id + "' contains unsupported characters");
	if (displayName.empty())
		throw TargetError("Target display name cannot be empty");
	for (std::size_t index = 0; index < symbols_.size(); ++index)
	{
		const TargetSymbol& symbol = symbols_[index];
		if (symbol.source() == TargetSymbolSource::Fixed)
		{
			switch (symbol.kind())
			{
			case TargetSymbolKind::PoolEntry:
				if (symbol.fixedValue() == 0 || symbol.fixedValue() > fingerprint_.poolSize)
					throw TargetError("Target pool symbol '" + symbol.name() +
						"' is outside the fingerprint's pool");
				break;
			case TargetSymbolKind::CodeOffset:
				if (symbol.fixedValue() >= fingerprint_.codeSize ||
					(symbol.fixedValue() & 3U) != 0 ||
					symbol.signature().size() > fingerprint_.codeSize - symbol.fixedValue())
					throw TargetError("Target code symbol '" + symbol.name() +
						"' is outside the fingerprint's aligned code section");
				break;
			case TargetSymbolKind::DataOffset:
				if (symbol.fixedValue() >= fingerprint_.dataSize)
					throw TargetError("Target data symbol '" + symbol.name() +
						"' is outside the fingerprint's data section");
				break;
			case TargetSymbolKind::BssOffset:
				if (symbol.fixedValue() >= fingerprint_.bssSize)
					throw TargetError("Target BSS symbol '" + symbol.name() +
						"' is outside the fingerprint's BSS section");
				break;
			case TargetSymbolKind::Constant:
				break;
			}
		}
		for (std::size_t previous = 0; previous < index; ++previous)
		{
			if (symbols_[previous].name() == symbol.name())
				throw TargetError("Target '" + id + "' defines symbol '" +
					symbol.name() + "' more than once");
		}
	}
}

bool TargetProfile::matchesFingerprint(const MpnImage& image) const
{
	return fingerprint_.matches(image.header());
}

ResolvedTarget TargetProfile::resolve(const MpnImage& image,
	bool validateCodeSignatures) const
{
	ModificationInfo modification;
	if (findModificationMarker(image, modification))
		throw TargetError("Input was already modified by '" + modification.modId +
			"' for target '" + modification.targetId + "'");
	if (!matchesFingerprint(image))
		throw TargetError("Input does not match target '" + id_ + "': " +
			fingerprint_.mismatch(image.header()));

	const PoolTable pool(image.poolBytes());
	std::vector<ResolvedSymbol> resolved;
	resolved.reserve(symbols_.size());
	for (const TargetSymbol& symbol : symbols_)
	{
		uint32_t value = symbol.fixedValue_;
		std::string sourceName;
		if (symbol.source_ == TargetSymbolSource::ImportedSyscall)
		{
			PoolId foundId = 0;
			for (std::size_t index = 0; index < pool.size(); ++index)
			{
				const PoolId id = static_cast<PoolId>(index + 1);
				const PoolEntry& entry = pool.at(id);
				if (entry.type != 0x02 ||
					readImageString(image, entry.argument) != symbol.importedName_)
					continue;
				if (foundId != 0)
					throw TargetError("Target symbol '" + symbol.name_ +
						"' matched duplicate imports named '" + symbol.importedName_ + "'");
				foundId = id;
			}
			if (foundId == 0)
				throw TargetError("Target symbol '" + symbol.name_ +
					"' could not find imported syscall '" + symbol.importedName_ + "'");
			value = foundId;
			sourceName = symbol.importedName_;
		}
		else if (symbol.kind_ == TargetSymbolKind::PoolEntry)
		{
			const PoolEntry& actual = pool.at(value);
			if (symbol.hasExpectedPoolEntry_ && actual != symbol.expectedPoolEntry_)
				throw TargetError("Target symbol '" + symbol.name_ + "' at pool ID " +
					std::to_string(value) + " is " + poolEntryText(actual) +
					" (expected " + poolEntryText(symbol.expectedPoolEntry_) + ")");
		}

		switch (symbol.kind_)
		{
		case TargetSymbolKind::PoolEntry:
			break;
		case TargetSymbolKind::CodeOffset:
			if (value >= image.code().size() || (value & 3U) != 0)
				throw TargetError("Target code symbol '" + symbol.name_ +
					"' is outside the aligned code section");
			if (validateCodeSignatures && !symbol.signature_.empty())
			{
				if (symbol.signature_.size() > image.code().size() - value ||
					!std::equal(symbol.signature_.begin(), symbol.signature_.end(),
						image.code().begin() + value))
					throw TargetError("Code signature for target symbol '" + symbol.name_ +
						"' does not match at offset " + std::to_string(value));
			}
			break;
		case TargetSymbolKind::DataOffset:
			if (value >= image.data().size())
				throw TargetError("Target data symbol '" + symbol.name_ +
					"' is outside the data section");
			break;
		case TargetSymbolKind::BssOffset:
			if (value >= image.header().bssSize)
				throw TargetError("Target BSS symbol '" + symbol.name_ +
					"' is outside the BSS section");
			break;
		case TargetSymbolKind::Constant:
			break;
		}
		resolved.push_back({symbol.name_, symbol.kind_, symbol.source_, value, sourceName});
	}
	return ResolvedTarget(this, std::move(resolved));
}

std::string makeModificationMarker(const std::string& targetId,
	const std::string& modId)
{
	if (!validIdentifier(targetId))
		throw TargetError("Modification target ID '" + targetId +
			"' contains unsupported characters");
	if (!validIdentifier(modId))
		throw TargetError("Modification ID '" + modId +
			"' contains unsupported characters");
	return std::string(ModificationPrefix) + targetId + ModificationSeparator + modId;
}

bool findModificationMarker(const MpnImage& image, ModificationInfo& info)
{
	const std::string prefix(ModificationPrefix);
	const std::string separator(ModificationSeparator);
	for (std::size_t offset = 0; offset < image.strings().size();)
	{
		const auto begin = image.strings().begin() + offset;
		const auto end = std::find(begin, image.strings().end(), 0);
		if (end == image.strings().end())
			return false;
		const std::string value(begin, end);
		if (value.compare(0, prefix.size(), prefix) == 0)
		{
			const std::size_t split = value.find(separator, prefix.size());
			if (split != std::string::npos)
			{
				info.targetId = value.substr(prefix.size(), split - prefix.size());
				info.modId = value.substr(split + separator.size());
				if (!info.targetId.empty() && !info.modId.empty())
					return true;
			}
		}
		offset = static_cast<std::size_t>(end - image.strings().begin()) + 1U;
	}
	return false;
}

TargetCatalog::TargetCatalog(const std::vector<TargetProfile>& profiles)
{
	for (const TargetProfile& profile : profiles)
		add(profile);
}

void TargetCatalog::add(const TargetProfile& profile)
{
	if (find(profile.id()) != nullptr)
		throw TargetError("Target catalog already contains '" + profile.id() + "'");
	profiles_.push_back(profile);
}

const TargetProfile* TargetCatalog::find(const std::string& id) const
{
	const auto found = std::find_if(profiles_.begin(), profiles_.end(),
		[&](const TargetProfile& profile) { return profile.id() == id; });
	return found == profiles_.end() ? nullptr : &*found;
}

TargetDetection TargetCatalog::detect(const MpnImage& image,
	bool validateCodeSignatures) const
{
	ModificationInfo modification;
	if (findModificationMarker(image, modification))
	{
		const TargetProfile* profile = find(modification.targetId);
		return {TargetCompatibility::PreviouslyModified, profile,
			"Input was already modified by '" + modification.modId + "' for target '" +
				modification.targetId + "'", modification};
	}

	for (const TargetProfile& profile : profiles_)
	{
		if (!profile.matchesFingerprint(image))
			continue;
		try
		{
			profile.resolve(image, validateCodeSignatures);
			return {TargetCompatibility::Compatible, &profile,
				"Matched " + profile.displayName(), {}};
		}
		catch (const TargetError& error)
		{
			return {TargetCompatibility::Unsupported, &profile,
				"Input resembles target '" + profile.id() + "' but is incompatible: " +
					error.what(), {}};
		}
		catch (const std::exception& error)
		{
			return {TargetCompatibility::Unsupported, &profile,
				"Input resembles target '" + profile.id() + "' but symbol resolution failed: " +
					error.what(), {}};
		}
	}

	std::string message = "No supported target matches this MPN";
	for (const TargetProfile& profile : profiles_)
		message += "; " + profile.id() + ": " +
			profile.fingerprint().mismatch(image.header());
	return {TargetCompatibility::Unsupported, nullptr, message, {}};
}

const TargetCatalog& builtInTargets()
{
	static const TargetCatalog catalog({makeVrally2Rc14EuM5Target()});
	return catalog;
}

const char* targetSymbolKindName(TargetSymbolKind kind)
{
	switch (kind)
	{
	case TargetSymbolKind::PoolEntry: return "pool entry";
	case TargetSymbolKind::CodeOffset: return "code offset";
	case TargetSymbolKind::DataOffset: return "data offset";
	case TargetSymbolKind::BssOffset: return "BSS offset";
	case TargetSymbolKind::Constant: return "constant";
	}
	return "unknown symbol";
}

const char* targetCompatibilityName(TargetCompatibility compatibility)
{
	switch (compatibility)
	{
	case TargetCompatibility::Compatible: return "compatible";
	case TargetCompatibility::PreviouslyModified: return "previously modified";
	case TargetCompatibility::Unsupported: return "unsupported";
	}
	return "unknown";
}

} // namespace mophunmod
