#pragma once

#include "mophunmod/mpn_image.h"
#include "mophunmod/pool_table.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mophunmod {

class TargetError : public std::runtime_error {
	public:
		explicit TargetError(const std::string& message) : std::runtime_error(message) {}
};

// Target symbols deliberately distinguish pool IDs from section offsets and
// numeric metadata. This catches accidental use of, for example, a code offset
// as an assembler pool operand.
enum class TargetSymbolKind {
	PoolEntry,
	CodeOffset,
	DataOffset,
	BssOffset,
	Constant
};

enum class TargetSymbolSource {
	Fixed,
	ImportedSyscall
};

class TargetSymbol {
	public:
		static TargetSymbol importedPool(const std::string& name,
			const std::string& syscallName);
		static TargetSymbol fixedPool(const std::string& name, PoolId id,
			const PoolEntry& expectedEntry);
		static TargetSymbol codeOffset(const std::string& name, uint32_t offset,
			const std::vector<uint8_t>& signature = std::vector<uint8_t>());
		static TargetSymbol dataOffset(const std::string& name, uint32_t offset);
		static TargetSymbol bssOffset(const std::string& name, uint32_t offset);
		static TargetSymbol constant(const std::string& name, uint32_t value);

		const std::string& name() const { return name_; }
		TargetSymbolKind kind() const { return kind_; }
		TargetSymbolSource source() const { return source_; }
		uint32_t fixedValue() const { return fixedValue_; }
		const std::string& importedName() const { return importedName_; }
		const std::vector<uint8_t>& signature() const { return signature_; }

	private:
		TargetSymbol(const std::string& name, TargetSymbolKind kind,
			TargetSymbolSource source, uint32_t fixedValue,
			const std::string& importedName, const std::vector<uint8_t>& signature,
			bool hasExpectedPoolEntry, const PoolEntry& expectedPoolEntry);

		friend class TargetProfile;
		std::string name_;
		TargetSymbolKind kind_;
		TargetSymbolSource source_;
		uint32_t fixedValue_;
		std::string importedName_;
		std::vector<uint8_t> signature_;
		bool hasExpectedPoolEntry_;
		PoolEntry expectedPoolEntry_;
};

// Exact fingerprints are intentionally practical rather than cryptographic:
// they reject other releases before any write while named symbols and code
// signatures provide the more meaningful compatibility checks.
struct TargetFingerprint {
	uint32_t heapSize = 0;
	uint16_t stackSize = 0;
	uint16_t flags = 0;
	uint32_t codeSize = 0;
	uint32_t dataSize = 0;
	uint32_t bssSize = 0;
	uint32_t resourceSize = 0;
	uint32_t directorySize = 0;
	uint32_t poolSize = 0;
	uint32_t stringSize = 0;

	bool matches(const MpnHeader& header) const;
	std::string mismatch(const MpnHeader& header) const;
};

class TargetProfile;

struct ResolvedSymbol {
	std::string name;
	TargetSymbolKind kind;
	TargetSymbolSource source;
	uint32_t value;
	std::string sourceName;
};

class ResolvedTarget {
	public:
		const TargetProfile& profile() const { return *profile_; }
		PoolId pool(const std::string& name) const;
		uint32_t codeOffset(const std::string& name) const;
		uint32_t dataOffset(const std::string& name) const;
		uint32_t bssOffset(const std::string& name) const;
		uint32_t constant(const std::string& name) const;
		const std::vector<ResolvedSymbol>& symbols() const { return symbols_; }

	private:
		friend class TargetProfile;
		ResolvedTarget(const TargetProfile* profile, std::vector<ResolvedSymbol> symbols) :
			profile_(profile), symbols_(std::move(symbols)) {}
		uint32_t value(const std::string& name, TargetSymbolKind kind) const;

		const TargetProfile* profile_;
		std::vector<ResolvedSymbol> symbols_;
};

class TargetProfile {
	public:
		TargetProfile(const std::string& id, const std::string& displayName,
			const TargetFingerprint& fingerprint, const std::vector<TargetSymbol>& symbols);

		const std::string& id() const { return id_; }
		const std::string& displayName() const { return displayName_; }
		const TargetFingerprint& fingerprint() const { return fingerprint_; }
		const std::vector<TargetSymbol>& symbols() const { return symbols_; }
		bool matchesFingerprint(const MpnImage& image) const;

		// Code signatures describe plaintext instructions. Callers inspecting an
		// encrypted commercial executable can skip them; patchers should validate
		// again after decryption with the default setting.
		ResolvedTarget resolve(const MpnImage& image,
			bool validateCodeSignatures = true) const;

	private:
		std::string id_;
		std::string displayName_;
		TargetFingerprint fingerprint_;
		std::vector<TargetSymbol> symbols_;
};

struct ModificationInfo {
	std::string targetId;
	std::string modId;
};

std::string makeModificationMarker(const std::string& targetId,
	const std::string& modId);
bool findModificationMarker(const MpnImage& image, ModificationInfo& info);

enum class TargetCompatibility {
	Compatible,
	PreviouslyModified,
	Unsupported
};

struct TargetDetection {
	TargetCompatibility compatibility;
	const TargetProfile* profile;
	std::string message;
	ModificationInfo modification;
};

class TargetCatalog {
	public:
		TargetCatalog() = default;
		explicit TargetCatalog(const std::vector<TargetProfile>& profiles);

		void add(const TargetProfile& profile);
		const std::vector<TargetProfile>& profiles() const { return profiles_; }
		const TargetProfile* find(const std::string& id) const;
		TargetDetection detect(const MpnImage& image,
			bool validateCodeSignatures = false) const;

	private:
		std::vector<TargetProfile> profiles_;
};

const TargetCatalog& builtInTargets();

const char* targetSymbolKindName(TargetSymbolKind kind);
const char* targetCompatibilityName(TargetCompatibility compatibility);

} // namespace mophunmod
