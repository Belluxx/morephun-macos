#include "mophunmod/mpn_image.h"
#include "mophunmod/patch_builder.h"
#include "mophunmod/pip_assembler.h"
#include "mophunmod/pool_table.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool require(bool condition, const std::string& message)
{
	if (!condition)
		std::cerr << "Modding patch test failed: " << message << '\n';
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
	writeU32(bytes, 12, 8);
	writeU32(bytes, 16, 3);
	writeU32(bytes, 20, 5);
	writeU32(bytes, 24, 4);
	writeU32(bytes, 32, 2);
	writeU32(bytes, 36, 4);
	const uint8_t code[] = {NOP, 0, 0, 0, NOP, 0, 0, 0};
	bytes.insert(bytes.end(), code, code + sizeof(code));
	bytes.insert(bytes.end(), {1, 2, 3});
	bytes.insert(bytes.end(), {4, 0, 0, 0});
	PoolTable entries;
	entries.append(PoolEntry::code(0));
	entries.append(PoolEntry::syscall(0));
	const std::vector<uint8_t> poolBytes = entries.serialize();
	bytes.insert(bytes.end(), poolBytes.begin(), poolBytes.end());
	bytes.insert(bytes.end(), {'o', 'l', 'd', 0});
	bytes.insert(bytes.end(), {0xaa, 0xbb});
	return bytes;
}

std::vector<uint8_t> applySyntheticPatch(const std::vector<uint8_t>& input)
{
	using namespace mophunmod;
	PatchBuilder patch(MpnImage::parse(input));
	const CodeHook hook = patch.reserveCodeHook(1);
	if (hook.originalPoolId() != 3)
		throw std::runtime_error("unexpected preserved pool ID");
	const SectionAllocation data = patch.allocateData({9, 8}, 4);
	patch.alignData(8);
	patch.addReference(data);
	const SectionAllocation bss = patch.allocateBss(4, 4);
	patch.addReference(bss);
	const PoolId imported = patch.importSyscall("vTest");
	if (patch.importSyscall("vTest") != imported)
		throw std::runtime_error("syscall import was not interned");
	const SectionAllocation code = patch.allocateCode({NOP, 0, 0, 0});
	patch.bindCodeHook(hook, code.offset);
	return patch.serialize();
}

template <typename Function>
bool throwsModdingError(Function function)
{
	try
	{
		function();
	}
	catch (const std::runtime_error&)
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
	const std::vector<uint8_t> output = applySyntheticPatch(input);
	const MpnImage patched = MpnImage::parse(output);
	const PoolTable pool(patched.poolBytes());

	success = require(input == syntheticMpn(), "patching does not mutate caller input") && success;
	success = require(patched.header().codeSize == 12 && patched.header().dataSize == 8 &&
		patched.header().bssSize == 12 && patched.header().poolSize == 6 &&
		patched.header().stringSize == 10, "all allocated section sizes reach the header") && success;
	success = require(patched.data() == std::vector<uint8_t>({1, 2, 3, 0, 9, 8, 0, 0}),
		"initialized-data allocation preserves and pads existing bytes") && success;
	success = require(patched.trailingData() == std::vector<uint8_t>({0xaa, 0xbb}),
		"serialization preserves unknown trailing data") && success;
	success = require(pool.at(1) == PoolEntry::code(8), "hook points at allocated guest code") && success;
	success = require(pool.at(3) == PoolEntry::code(0),
		"hook retains a callable pool entry for the original target") && success;
	success = require(pool.at(4) == PoolEntry::data(4) && pool.at(5) == PoolEntry::bss(8),
		"data and BSS references use allocator offsets") && success;
	success = require(pool.at(6) == PoolEntry::syscall(4) &&
		std::string(patched.strings().begin() + 4, patched.strings().end()) ==
			std::string("vTest\0", 6), "syscall import appends its name and pool entry") && success;
	success = require(applySyntheticPatch(input) == output, "patch output is deterministic") && success;

	PatchBuilder combined(MpnImage::parse(input));
	success = require(combined.internString("old") == 0 && combined.header().stringSize == 4,
		"string interning reuses an existing string") && success;
	success = require(combined.findImportedSyscall("old") == 2 &&
		combined.importSyscall("old") == 2 && combined.poolSize() == 2,
		"syscall importing reuses an existing named import") && success;
	const PoolId saved = combined.replacePoolEntryRetainingOriginal(1, PoolEntry::code(4));
	const PoolTable combinedPool(combined.finishImage().poolBytes());
	success = require(saved == 3 && combinedPool.at(1) == PoolEntry::code(4) &&
		combinedPool.at(saved) == PoolEntry::code(0),
		"combined replacement helper preserves the original") && success;

	PatchBuilder invalid(MpnImage::parse(input));
	success = require(throwsModdingError([&] {
		invalid.addReference({SectionKind::Data, 3, 1});
	}), "out-of-section allocation references are rejected") && success;
	const CodeHook invalidHook = invalid.reserveCodeHook(1);
	success = require(throwsModdingError([&] { invalid.reserveCodeHook(1); }),
		"the same pool entry cannot have two pending hooks") && success;
	success = require(throwsModdingError([&] { invalid.bindCodeHook(invalidHook, 8); }),
		"out-of-section hook targets are rejected") && success;
	success = require(throwsModdingError([&] { invalid.finishImage(); }),
		"an unbound hook prevents serialization") && success;
	success = require(input == syntheticMpn(), "failed operations leave the source bytes untouched") && success;

	PatchBuilder hookOwner(MpnImage::parse(input));
	const CodeHook ownedHook = hookOwner.reserveCodeHook(1);
	PatchBuilder foreignBuilder(MpnImage::parse(input));
	const SectionAllocation foreignCode = foreignBuilder.allocateCode({NOP, 0, 0, 0});
	success = require(throwsModdingError([&] {
		foreignBuilder.bindCodeHook(ownedHook, foreignCode.offset);
	}), "hook handles cannot be used with another patch builder") && success;

	return success ? 0 : 1;
}
