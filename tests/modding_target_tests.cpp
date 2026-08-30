#include "mophunmod/mpn_image.h"
#include "mophunmod/patch_builder.h"
#include "mophunmod/pip_assembler.h"
#include "mophunmod/pool_table.h"
#include "mophunmod/target.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool require(bool condition, const std::string& message)
{
	if (!condition)
		std::cerr << "Modding target test failed: " << message << '\n';
	return condition;
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
	using namespace mophunmod;
	std::vector<uint8_t> bytes(MpnHeaderSize, 0);
	bytes[0] = 'V'; bytes[1] = 'M'; bytes[2] = 'G'; bytes[3] = 'P';
	bytes[8] = 32;
	writeU32(bytes, 12, 8);
	writeU32(bytes, 16, 1);
	writeU32(bytes, 20, 4);
	writeU32(bytes, 32, 2);
	writeU32(bytes, 36, 7);
	bytes.insert(bytes.end(), {NOP, 0, 0, 0, NOP, 0, 0, 0});
	bytes.push_back(0xaa);
	PoolTable pool;
	pool.append(PoolEntry::code(0));
	pool.append(PoolEntry::syscall(0));
	const std::vector<uint8_t> poolBytes = pool.serialize();
	bytes.insert(bytes.end(), poolBytes.begin(), poolBytes.end());
	bytes.insert(bytes.end(), {'v', 'N', 'a', 'm', 'e', 'd', 0});
	return bytes;
}

mophunmod::TargetFingerprint syntheticFingerprint()
{
	mophunmod::TargetFingerprint fingerprint;
	fingerprint.stackSize = 32;
	fingerprint.codeSize = 8;
	fingerprint.dataSize = 1;
	fingerprint.bssSize = 4;
	fingerprint.poolSize = 2;
	fingerprint.stringSize = 7;
	return fingerprint;
}

mophunmod::TargetProfile syntheticProfile()
{
	using namespace mophunmod;
	return TargetProfile("synthetic-v1", "Synthetic test target", syntheticFingerprint(), {
		TargetSymbol::fixedPool("game.update", 1, PoolEntry::code(0)),
		TargetSymbol::importedPool("os.named", "vNamed"),
		TargetSymbol::codeOffset("game.update.code", 0, {NOP, 0, 0, 0}),
		TargetSymbol::dataOffset("game.data", 0),
		TargetSymbol::bssOffset("game.state", 0),
		TargetSymbol::constant("game.rate", 15)
	});
}

template <typename Function>
bool throwsTargetError(Function function)
{
	try
	{
		function();
	}
	catch (const mophunmod::TargetError&)
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
	const TargetProfile profile = syntheticProfile();
	TargetCatalog catalog({profile});

	const TargetDetection detection = catalog.detect(image);
	success = require(detection.compatibility == TargetCompatibility::Compatible &&
		detection.profile != nullptr && detection.profile->id() == "synthetic-v1",
		"a structurally valid image is detected") && success;
	const ResolvedTarget target = detection.profile->resolve(image);
	success = require(target.pool("game.update") == 1 && target.pool("os.named") == 2,
		"fixed and import-discovered pool symbols resolve") && success;
	success = require(target.codeOffset("game.update.code") == 0 &&
		target.dataOffset("game.data") == 0 && target.bssOffset("game.state") == 0 &&
		target.constant("game.rate") == 15,
		"typed offsets and metadata resolve") && success;
	success = require(throwsTargetError([&] { target.pool("game.rate"); }),
		"using a symbol through the wrong kind is rejected") && success;
	success = require(throwsTargetError([&] { target.constant("missing"); }),
		"missing symbols produce a target error") && success;

	std::vector<uint8_t> wrongSignature = input;
	wrongSignature[MpnHeaderSize] = BREAKPOINT;
	const MpnImage wrongSignatureImage = MpnImage::parse(wrongSignature);
	success = require(catalog.detect(wrongSignatureImage).compatibility ==
		TargetCompatibility::Compatible,
		"structural detection can inspect an encrypted or otherwise encoded image") && success;
	success = require(catalog.detect(wrongSignatureImage, true).compatibility ==
		TargetCompatibility::Unsupported,
		"detection can validate signatures for known-plaintext input") && success;
	success = require(throwsTargetError([&] { profile.resolve(wrongSignatureImage); }),
		"patch-time resolution checks plaintext code signatures") && success;
	success = require(profile.resolve(wrongSignatureImage, false).pool("game.update") == 1,
		"inspection can explicitly defer plaintext signature checks") && success;

	std::vector<uint8_t> wrongHook = input;
	const std::size_t firstPoolValue = MpnHeaderSize + 8 + 1 + 4;
	writeU32(wrongHook, firstPoolValue, 4);
	const TargetDetection wrongHookDetection = catalog.detect(MpnImage::parse(wrongHook));
	success = require(wrongHookDetection.compatibility == TargetCompatibility::Unsupported &&
		wrongHookDetection.profile != nullptr &&
		wrongHookDetection.message.find("game.update") != std::string::npos,
		"a target-shaped image with the wrong hook fails with its symbol name") && success;

	std::vector<uint8_t> wrongHeader = input;
	writeU32(wrongHeader, 20, 8);
	const TargetDetection wrongHeaderDetection = catalog.detect(MpnImage::parse(wrongHeader));
	success = require(wrongHeaderDetection.compatibility == TargetCompatibility::Unsupported &&
		wrongHeaderDetection.profile == nullptr &&
		wrongHeaderDetection.message.find("BSS size") != std::string::npos,
		"an incompatible release is rejected with a header mismatch") && success;

	PatchBuilder patch(image);
	patch.markModified(profile.id(), "test-mod");
	patch.markModified(profile.id(), "test-mod");
	const MpnImage modified = patch.finishImage();
	ModificationInfo marker;
	success = require(findModificationMarker(modified, marker) &&
		marker.targetId == profile.id() && marker.modId == "test-mod",
		"a modification marker records target and mod IDs") && success;
	const TargetDetection modifiedDetection = catalog.detect(modified);
	success = require(modifiedDetection.compatibility ==
		TargetCompatibility::PreviouslyModified && modifiedDetection.profile != nullptr,
		"marked images are recognized before changed section sizes are compared") && success;
	success = require(throwsTargetError([&] { profile.resolve(modified); }),
		"previously modified input cannot be resolved for reapplication") && success;

	success = require(throwsTargetError([&] { catalog.add(profile); }),
		"catalog target IDs must be unique") && success;
	success = require(throwsTargetError([&] {
		TargetProfile invalid("invalid-v1", "Invalid", syntheticFingerprint(), {
			TargetSymbol::fixedPool("outside", 3, PoolEntry::code(0))
		});
		(void)invalid;
	}), "profile symbols are checked against their fingerprint") && success;
	const TargetProfile* vrally = builtInTargets().find("vrally2-rc14eu-m5");
	success = require(vrally != nullptr &&
		vrally->displayName().find("V-Rally 2") != std::string::npos &&
		vrally->symbols().size() >= 10,
		"the supported V-Rally release is registered as a built-in target") && success;

	return success ? 0 : 1;
}
