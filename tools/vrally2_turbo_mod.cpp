#include "binary_io.h"
#include "opcodes.h"
#include "pool.h"
#include "registers.h"
#include "rom_decrypt.h"
#include "vmgp_header.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr uint32_t ExpectedCodeSize = 0x82f0;
constexpr uint32_t ExpectedDataSize = 0x0a44;
constexpr uint32_t ExpectedBssSize = 0xcdd0;
constexpr uint32_t ExpectedResourceSize = 0xd1e8;
constexpr uint32_t ExpectedPoolSize = 0x1d2;
constexpr uint32_t ExpectedStringSize = 0x25b;
constexpr uint32_t CarUpdatePoolId = 185;
constexpr uint32_t FlipScreenPoolId = 9;
constexpr uint32_t CarUpdateCodeOffset = 0x3dc4;
constexpr uint32_t TurboStateSize = 28;

constexpr uint32_t StateCharge = 0;
constexpr uint32_t StatePhase = 4;
constexpr uint32_t StateActiveFrames = 8;
constexpr uint32_t StatePreviousSpeed = 12;
constexpr uint32_t StateCollisionCooldown = 16;
constexpr uint32_t StatePreviousKeys = 20;
constexpr uint32_t StateInRace = 24;

constexpr uint32_t CarStarted = 0;
constexpr uint32_t CarTargetSpeed = 0x24;
constexpr uint32_t CarSpeed = 0x28;

constexpr uint32_t KeyDown = 0x02;
constexpr uint32_t KeyFire2 = 0x100;

uint32_t encodeImmediate(int32_t value)
{
	return static_cast<uint32_t>(value) | 0x80000000U;
}

void appendU32(std::vector<uint8_t>& bytes, uint32_t value)
{
	bytes.push_back(static_cast<uint8_t>(value));
	bytes.push_back(static_cast<uint8_t>(value >> 8));
	bytes.push_back(static_cast<uint8_t>(value >> 16));
	bytes.push_back(static_cast<uint8_t>(value >> 24));
}

void appendPoolItem(std::vector<uint8_t>& bytes, uint8_t type, uint32_t argument1,
	uint32_t argument2)
{
	appendU32(bytes, static_cast<uint32_t>(type) | (argument1 << 8));
	appendU32(bytes, argument2);
}

class Assembler {
	public:
		explicit Assembler(uint32_t baseOffset) : baseOffset(baseOffset) {}

		void label(const std::string& name)
		{
			if (!labels.emplace(name, static_cast<uint32_t>(code.size())).second)
				throw std::runtime_error("Duplicate assembly label: " + name);
		}

		uint32_t labelOffset(const std::string& name) const
		{
			auto found = labels.find(name);
			if (found == labels.end())
				throw std::runtime_error("Unknown assembly label: " + name);
			return baseOffset + found->second;
		}

		void op(uint8_t opcode, uint8_t destination = zero, uint8_t source = zero,
			uint8_t extra = zero)
		{
			code.push_back(opcode);
			code.push_back(destination);
			code.push_back(source);
			code.push_back(extra);
		}

		void ldq(uint8_t destination, int16_t value)
		{
			op(LDQ, destination, static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8));
		}

		void immediate(uint8_t opcode, uint8_t destination, uint8_t source, int32_t value)
		{
			op(opcode, destination, source);
			appendU32(code, encodeImmediate(value));
		}

		void pool(uint8_t opcode, uint8_t destination, uint8_t source, uint32_t id)
		{
			op(opcode, destination, source);
			appendU32(code, id);
		}

		void callPool(uint32_t id) { pool(CALLl, zero, zero, id); }

		void call(const std::string& target)
		{
			const uint32_t instruction = static_cast<uint32_t>(code.size());
			op(CALLl);
			appendU32(code, 0);
			fixups.push_back({FixupKind::Long, instruction, instruction + 4, target});
		}

		void jump(const std::string& target)
		{
			const uint32_t instruction = static_cast<uint32_t>(code.size());
			op(JPl);
			appendU32(code, 0);
			fixups.push_back({FixupKind::Long, instruction, instruction + 4, target});
		}

		void branch(uint8_t opcode, uint8_t left, uint8_t right, const std::string& target)
		{
			const uint32_t instruction = static_cast<uint32_t>(code.size());
			op(opcode, left, right);
			appendU32(code, 0);
			fixups.push_back({FixupKind::Long, instruction, instruction + 4, target});
		}

		void branchImmediate(uint8_t opcode, uint8_t value, int8_t comparison,
			const std::string& target)
		{
			const uint32_t instruction = static_cast<uint32_t>(code.size());
			op(opcode, value, static_cast<uint8_t>(comparison), 0);
			fixups.push_back({FixupKind::Short, instruction, instruction + 3, target});
		}

		std::vector<uint8_t> finish()
		{
			for (const Fixup& fixup : fixups)
			{
				auto found = labels.find(fixup.target);
				if (found == labels.end())
					throw std::runtime_error("Undefined assembly label: " + fixup.target);
				const int32_t displacement = static_cast<int32_t>(found->second) -
					static_cast<int32_t>(fixup.instruction);
				if (fixup.kind == FixupKind::Short)
				{
					if (displacement % 4 != 0 || displacement / 4 < -128 || displacement / 4 > 127)
						throw std::runtime_error("Short branch is out of range: " + fixup.target);
					code[fixup.patchOffset] = static_cast<uint8_t>(displacement / 4);
				}
				else
					writeLittleU32(code.data() + fixup.patchOffset, encodeImmediate(displacement));
			}
			return code;
		}

	private:
		enum class FixupKind { Short, Long };
		struct Fixup {
			FixupKind kind;
			uint32_t instruction;
			uint32_t patchOffset;
			std::string target;
		};

		uint32_t baseOffset;
		std::vector<uint8_t> code;
		std::map<std::string, uint32_t> labels;
		std::vector<Fixup> fixups;
};

std::vector<uint8_t> readFile(const std::string& path)
{
	std::ifstream input(path, std::ios::binary | std::ios::ate);
	if (!input)
		throw std::runtime_error("Unable to open input MPN: " + path);
	const std::streamoff length = input.tellg();
	if (length < static_cast<std::streamoff>(sizeof(VMGPHeader)))
		throw std::runtime_error("Input is too small to be an MPN");
	std::vector<uint8_t> bytes(static_cast<size_t>(length));
	input.seekg(0);
	if (!input.read(reinterpret_cast<char*>(bytes.data()), length))
		throw std::runtime_error("Unable to read input MPN: " + path);
	return bytes;
}

void writeFile(const std::string& path, const std::vector<uint8_t>& bytes)
{
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	if (!output || !output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()))
		throw std::runtime_error("Unable to write modded MPN: " + path);
}

void requireTargetHeader(const VMGPHeader& header)
{
	if (std::string(header.magicNo, 4) != "VMGP" || header.flags != 0 ||
		header.codeSize != ExpectedCodeSize || header.dataSize != ExpectedDataSize ||
		header.bssSize != ExpectedBssSize || header.resSize != ExpectedResourceSize ||
		header.poolSize != ExpectedPoolSize || header.stringSize != ExpectedStringSize)
		throw std::runtime_error("Input is not the supported V-Rally 2 RC14EU M5 executable");
}

std::vector<uint8_t> buildGuestCode(uint32_t originalUpdatePoolId,
	uint32_t originalFlipPoolId, uint32_t turboStatePoolId, Assembler& assembler)
{
	// All gameplay state is guest BSS. The wrapper keeps the game's ABI and return value intact.
	assembler.label("TurboCarUpdateWrapper");
	assembler.op(STORE, ra, s2);
	assembler.op(MOV, s0, p0);
	assembler.callPool(originalUpdatePoolId);
	assembler.op(MOV, s1, r0);
	assembler.op(MOV, p0, s0);
	assembler.call("TurboUpdate");
	assembler.op(MOV, r0, s1);
	assembler.op(RET, s3, s2);

	assembler.label("TurboUpdate");
	assembler.op(STORE, ra, s6);
	assembler.op(MOV, s1, p0);                         // s1 = car
	assembler.pool(LDI, s0, zero, turboStatePoolId);  // s0 = TurboState
	assembler.immediate(LDBUd, r0, s1, CarStarted);
	assembler.ldq(p0, 1);
	assembler.branch(BNE, r0, p0, "TurboReset");
	assembler.ldq(r0, 1);
	assembler.immediate(STWd, r0, s0, StateInRace);
	assembler.callPool(10); // vGetButtonData
	assembler.op(MOV, s2, r0);
	assembler.immediate(LDWd, s3, s0, StatePhase);
	assembler.branchImmediate(BEQI, s3, 2, "TurboActive");

	// Meter update. Charge is stored in tenths of a percent (0..1000).
	assembler.immediate(LDWd, s4, s0, StateCollisionCooldown);
	assembler.branchImmediate(BLEI, s4, 0, "CooldownDone");
	assembler.op(ADDQ, s4, s4, static_cast<uint8_t>(-1));
	assembler.immediate(STWd, s4, s0, StateCollisionCooldown);
	assembler.label("CooldownDone");
	assembler.immediate(LDWd, s5, s0, StateCharge);
	assembler.immediate(LDWd, p0, s1, CarSpeed);
	assembler.immediate(LDWd, p1, s1, CarTargetSpeed);
	assembler.branchImmediate(BLTI, p0, 0, "RateReverse");
	assembler.immediate(ANDi, r0, s2, KeyDown);
	assembler.branchImmediate(BNEI, r0, 0, "RateBrake");
	assembler.branchImmediate(BLEI, p1, 0, "RateSpeedBands");
	assembler.immediate(LDI, p2, zero, 40000);
	assembler.branch(BLT, p1, p2, "RateOffroad");
	assembler.label("RateSpeedBands");
	assembler.immediate(LDI, p2, zero, 43200);
	assembler.branch(BGE, p0, p2, "RateMaximum");
	assembler.immediate(LDI, p2, zero, 33600);
	assembler.branch(BGE, p0, p2, "RateHigh");
	assembler.immediate(LDI, p2, zero, 19200);
	assembler.branch(BLT, p0, p2, "RateLow");
	assembler.jump("RateDone");
	assembler.label("RateReverse");
	assembler.op(ADDQ, s5, s5, static_cast<uint8_t>(-16));
	assembler.jump("RateDone");
	assembler.label("RateBrake");
	assembler.op(ADDQ, s5, s5, static_cast<uint8_t>(-3));
	assembler.jump("RateDone");
	assembler.label("RateOffroad");
	assembler.op(ADDQ, s5, s5, static_cast<uint8_t>(-9));
	assembler.jump("RateDone");
	assembler.label("RateMaximum");
	assembler.op(ADDQ, s5, s5, 6);
	assembler.jump("RateDone");
	assembler.label("RateHigh");
	assembler.op(ADDQ, s5, s5, 3);
	assembler.jump("RateDone");
	assembler.label("RateLow");
	assembler.op(ADDQ, s5, s5, static_cast<uint8_t>(-5));
	assembler.label("RateDone");

	// Detect a sudden non-braking speed loss as a collision.
	assembler.branchImmediate(BGTI, s4, 0, "CollisionDone");
	assembler.immediate(ANDi, r0, s2, KeyDown);
	assembler.branchImmediate(BNEI, r0, 0, "CollisionDone");
	assembler.immediate(LDWd, p2, s0, StatePreviousSpeed);
	assembler.branchImmediate(BLEI, p2, 0, "CollisionDone");
	assembler.op(SUB, r0, p2, p0);
	assembler.immediate(LDI, p1, zero, 9600);
	assembler.branch(BLE, r0, p1, "CollisionDone");
	assembler.immediate(ADDi, s5, s5, -300);
	assembler.ldq(s4, 15);
	assembler.immediate(STWd, s4, s0, StateCollisionCooldown);
	assembler.label("CollisionDone");

	// Clamp and store charge.
	assembler.branchImmediate(BLTI, s5, 0, "ChargeZero");
	assembler.immediate(LDI, p1, zero, 1000);
	assembler.branch(BLE, s5, p1, "ChargeClamped");
	assembler.op(MOV, s5, p1);
	assembler.jump("ChargeClamped");
	assembler.label("ChargeZero");
	assembler.ldq(s5, 0);
	assembler.label("ChargeClamped");
	assembler.immediate(STWd, s5, s0, StateCharge);

	// Fire 2 (Space in MoRePhun) activates only on a fresh press at full charge.
	assembler.immediate(LDI, p1, zero, 1000);
	assembler.branch(BLT, s5, p1, "TurboFinish");
	assembler.immediate(ANDi, r0, s2, KeyFire2);
	assembler.branchImmediate(BEQI, r0, 0, "TurboFinish");
	assembler.immediate(LDWd, p0, s0, StatePreviousKeys);
	assembler.immediate(ANDi, p0, p0, KeyFire2);
	assembler.branchImmediate(BNEI, p0, 0, "TurboFinish");
	assembler.ldq(r0, 2);
	assembler.immediate(STWd, r0, s0, StatePhase);
	assembler.ldq(r0, 120); // eight seconds at V-Rally's 15 Hz race update
	assembler.immediate(STWd, r0, s0, StateActiveFrames);
	assembler.ldq(r0, 0);
	assembler.immediate(STWd, r0, s0, StateCharge);
	assembler.jump("TurboApplyBoost");

	assembler.label("TurboActive");
	assembler.label("TurboApplyBoost");
	assembler.immediate(LDWd, p0, s1, CarTargetSpeed);
	assembler.immediate(LDI, p1, zero, 40000);
	assembler.branch(BLT, p0, p1, "BoostTimer");
	assembler.op(MULQ, p0, p0, 7);
	assembler.immediate(DIVi, p0, p0, 4);
	assembler.immediate(LDI, p1, zero, 84000);
	assembler.branch(BLE, p0, p1, "StoreBoost");
	assembler.op(MOV, p0, p1);
	assembler.label("StoreBoost");
	assembler.immediate(STWd, p0, s1, CarTargetSpeed);
	assembler.label("BoostTimer");
	assembler.immediate(LDWd, s4, s0, StateActiveFrames);
	assembler.op(ADDQ, s4, s4, static_cast<uint8_t>(-1));
	assembler.immediate(STWd, s4, s0, StateActiveFrames);
	assembler.branchImmediate(BGTI, s4, 0, "TurboFinish");
	assembler.ldq(r0, 0);
	assembler.immediate(STWd, r0, s0, StatePhase);

	assembler.label("TurboFinish");
	assembler.immediate(STWd, s2, s0, StatePreviousKeys);
	assembler.immediate(LDWd, r0, s1, CarSpeed);
	assembler.immediate(STWd, r0, s0, StatePreviousSpeed);
	assembler.op(RET, s7, s6);

	assembler.label("TurboReset");
	assembler.ldq(p0, TurboStateSize);
	assembler.op(SYSSET, s0, zero, p0);
	assembler.op(RET, s7, s6);

	// The existing vFlipScreen pool entry points here. The HUD is therefore guest-rendered
	// immediately before the real OS flip without any emulator graphics hook.
	assembler.label("TurboFlipWrapper");
	assembler.op(STORE, zero, sp);
	assembler.call("TurboDrawHud");
	assembler.callPool(originalFlipPoolId);
	assembler.op(RET, zero, sp);

	assembler.label("TurboDrawHud");
	assembler.op(STORE, ra, s5);
	assembler.pool(LDI, s0, zero, turboStatePoolId);
	assembler.immediate(LDWd, r0, s0, StateInRace);
	assembler.branchImmediate(BEQI, r0, 0, "HudReturn");

	assembler.ldq(p0, 0);
	assembler.ldq(p1, 0);
	assembler.ldq(p2, 127);
	assembler.ldq(p3, 159);
	assembler.callPool(23); // vSetClipWindow

	assembler.ldq(p0, 0x03ff); // cyan RGB555
	assembler.call("TurboSetColor");
	assembler.ldq(p0, 94);
	assembler.ldq(p1, 136);
	assembler.ldq(p2, 127);
	assembler.ldq(p3, 145);
	assembler.callPool(8); // vFillRect

	assembler.ldq(p0, 0); // black
	assembler.call("TurboSetColor");
	assembler.ldq(p0, 95);
	assembler.ldq(p1, 137);
	assembler.ldq(p2, 126);
	assembler.ldq(p3, 144);
	assembler.callPool(8);

	assembler.immediate(LDWd, s1, s0, StatePhase);
	assembler.branchImmediate(BEQI, s1, 2, "HudActive");
	assembler.immediate(LDWd, s2, s0, StateCharge);
	assembler.op(MULQ, s2, s2, 30);
	assembler.immediate(DIVi, s2, s2, 1000);
	assembler.immediate(LDWd, r0, s0, StateCharge);
	assembler.immediate(LDI, p0, zero, 1000);
	assembler.branch(BGE, r0, p0, "HudReadyColor");
	assembler.ldq(p0, 0x001f); // blue RGB555
	assembler.jump("HudColorReady");
	assembler.label("HudReadyColor");
	assembler.ldq(p0, 0x7fff); // white RGB555
	assembler.jump("HudColorReady");
	assembler.label("HudActive");
	assembler.immediate(LDWd, s2, s0, StateActiveFrames);
	assembler.op(MULQ, s2, s2, 30);
	assembler.immediate(DIVi, s2, s2, 120);
	assembler.ldq(p0, 0x7fe0); // yellow RGB555
	assembler.label("HudColorReady");
	assembler.branchImmediate(BLEI, s2, 0, "HudReturn");
	assembler.call("TurboSetColor");
	assembler.op(ADDQ, p2, s2, 94);
	assembler.ldq(p0, 95);
	assembler.ldq(p1, 138);
	assembler.ldq(p3, 143);
	assembler.callPool(8);
	assembler.label("HudReturn");
	assembler.op(RET, s6, s5);

	assembler.label("TurboSetColor");
	assembler.op(STORE, zero, sp);
	assembler.ldq(p1, 1);
	assembler.op(SLLi, p1, p1, 31);
	assembler.op(OR, p0, p0, p1);
	assembler.callPool(24); // vSetForeColor
	assembler.op(RET, zero, sp);

	return assembler.finish();
}

std::vector<uint8_t> buildModdedMpn(std::vector<uint8_t> input)
{
	const VMGPHeader header = decodeVMGPHeader(input.data());
	requireTargetHeader(header);
	std::string decryptError;
	if (!decryptCommercialCode(input, header, decryptError))
		throw std::runtime_error("Unable to decrypt the commercial code: " + decryptError);

	const uint32_t oldDataOffset = sizeof(VMGPHeader) + header.codeSize;
	const uint32_t oldResourceOffset = oldDataOffset + header.dataSize;
	const uint32_t oldPoolOffset = oldResourceOffset + header.resSize;
	const uint32_t oldStringOffset = oldPoolOffset + header.poolSize * PoolItemSize;
	if (oldStringOffset + header.stringSize > input.size())
		throw std::runtime_error("Input MPN sections exceed the file size");

	const uint8_t expectedUpdateStart[] = {STORE, ra, s2, 0};
	if (!std::equal(expectedUpdateStart, expectedUpdateStart + sizeof(expectedUpdateStart),
		input.begin() + sizeof(VMGPHeader) + CarUpdateCodeOffset))
		throw std::runtime_error("Car update signature does not match the supported executable");

	uint8_t* const oldPool = input.data() + oldPoolOffset;
	const PoolItem flipItem = decodePoolItemBytes(oldPool + (FlipScreenPoolId - 1) * PoolItemSize);
	const PoolItem updateItem = decodePoolItemBytes(oldPool + (CarUpdatePoolId - 1) * PoolItemSize);
	if (flipItem.segment_0 != 0 || flipItem.segment_1 != 2 ||
		updateItem.segment_0 != 1 || updateItem.extra != CarUpdateCodeOffset)
		throw std::runtime_error("V-Rally hook address table entries do not match");

	const uint32_t originalUpdatePoolId = header.poolSize + 1;
	const uint32_t originalFlipPoolId = header.poolSize + 2;
	const uint32_t turboStatePoolId = header.poolSize + 3;
	Assembler assembler(header.codeSize);
	const std::vector<uint8_t> guestCode = buildGuestCode(originalUpdatePoolId,
		originalFlipPoolId, turboStatePoolId, assembler);

	std::vector<uint8_t> newPool(oldPool, oldPool + header.poolSize * PoolItemSize);
	writeLittleU32(newPool.data() + (CarUpdatePoolId - 1) * PoolItemSize, 0x11);
	writeLittleU32(newPool.data() + (CarUpdatePoolId - 1) * PoolItemSize + 4,
		assembler.labelOffset("TurboCarUpdateWrapper"));
	writeLittleU32(newPool.data() + (FlipScreenPoolId - 1) * PoolItemSize, 0x11);
	writeLittleU32(newPool.data() + (FlipScreenPoolId - 1) * PoolItemSize + 4,
		assembler.labelOffset("TurboFlipWrapper"));
	appendPoolItem(newPool, 0x11, 0, CarUpdateCodeOffset);
	appendPoolItem(newPool, 0x02, flipItem.segmentoffset, 0);
	appendPoolItem(newPool, 0x41, 0, header.bssSize);

	std::vector<uint8_t> output;
	output.reserve(input.size() + guestCode.size() + 3 * PoolItemSize);
	output.insert(output.end(), input.begin(), input.begin() + sizeof(VMGPHeader));
	output.insert(output.end(), input.begin() + sizeof(VMGPHeader), input.begin() + oldDataOffset);
	output.insert(output.end(), guestCode.begin(), guestCode.end());
	output.insert(output.end(), input.begin() + oldDataOffset, input.begin() + oldPoolOffset);
	output.insert(output.end(), newPool.begin(), newPool.end());
	output.insert(output.end(), input.begin() + oldStringOffset,
		input.begin() + oldStringOffset + header.stringSize);

	writeLittleU32(output.data() + 12, header.codeSize + static_cast<uint32_t>(guestCode.size()));
	writeLittleU32(output.data() + 20, header.bssSize + TurboStateSize);
	writeLittleU32(output.data() + 32, header.poolSize + 3);
	return output;
}

} // namespace

int main(int argc, char* argv[])
{
	if (argc != 3)
	{
		std::cerr << "Usage: " << argv[0] << " <original-vrally2.mpn> <modded-vrally2.mpn>\n";
		return 2;
	}
	try
	{
		const std::vector<uint8_t> output = buildModdedMpn(readFile(argv[1]));
		writeFile(argv[2], output);
		std::cout << "Native turbo mod written to " << argv[2] << " (" << output.size()
			<< " bytes)\n";
	}
	catch (const std::exception& error)
	{
		std::cerr << "Turbo mod build failed: " << error.what() << '\n';
		return 1;
	}
	return 0;
}
