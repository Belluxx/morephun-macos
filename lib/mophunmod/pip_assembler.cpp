#include "mophunmod/pip_assembler.h"

#include <limits>
#include <utility>

namespace mophunmod {
namespace {

void appendU32(std::vector<uint8_t>& bytes, uint32_t value)
{
	bytes.push_back(static_cast<uint8_t>(value));
	bytes.push_back(static_cast<uint8_t>(value >> 8));
	bytes.push_back(static_cast<uint8_t>(value >> 16));
	bytes.push_back(static_cast<uint8_t>(value >> 24));
}

void writeU32(uint8_t* bytes, uint32_t value)
{
	bytes[0] = static_cast<uint8_t>(value);
	bytes[1] = static_cast<uint8_t>(value >> 8);
	bytes[2] = static_cast<uint8_t>(value >> 16);
	bytes[3] = static_cast<uint8_t>(value >> 24);
}

} // namespace

uint32_t encodePipImmediate(int32_t value)
{
	constexpr int32_t MinimumImmediate = -0x40000000;
	constexpr int32_t MaximumImmediate = 0x3fffffff;
	if (value < MinimumImmediate || value > MaximumImmediate)
		throw AssemblerError("PIP2 immediate is outside the signed 31-bit range");
	return static_cast<uint32_t>(value) | 0x80000000U;
}

void PipAssembler::label(const std::string& name)
{
	if (name.empty())
		throw AssemblerError("Assembly label name cannot be empty");
	if (code_.size() > std::numeric_limits<uint32_t>::max())
		throw AssemblerError("Assembled code exceeds the PIP2 address range");
	if (!labels_.emplace(name, static_cast<uint32_t>(code_.size())).second)
		throw AssemblerError("Duplicate assembly label: " + name);
}

uint32_t PipProgram::symbolOffset(const std::string& name) const
{
	const auto found = symbols_.find(name);
	if (found == symbols_.end())
		throw AssemblerError("Unknown assembled symbol: " + name);
	const uint64_t absolute = static_cast<uint64_t>(baseOffset_) + found->second;
	if (absolute > std::numeric_limits<uint32_t>::max())
		throw AssemblerError("Assembled symbol address overflows 32 bits: " + name);
	return static_cast<uint32_t>(absolute);
}

void PipAssembler::op(Opcode opcode, Register destination, Register source, uint8_t extra)
{
	if (code_.size() > std::numeric_limits<uint32_t>::max() - 4U)
		throw AssemblerError("Assembled code exceeds the PIP2 address range");
	code_.push_back(static_cast<uint8_t>(opcode));
	code_.push_back(static_cast<uint8_t>(destination));
	code_.push_back(static_cast<uint8_t>(source));
	code_.push_back(extra);
}

void PipAssembler::ldq(Register destination, int16_t value)
{
	op(LDQ, destination, static_cast<Register>(static_cast<uint8_t>(value)),
		static_cast<uint8_t>(static_cast<uint16_t>(value) >> 8));
}

void PipAssembler::immediate(Opcode opcode, Register destination, Register source,
	int32_t value)
{
	op(opcode, destination, source);
	appendU32(code_, encodePipImmediate(value));
}

void PipAssembler::pool(Opcode opcode, Register destination, Register source, PoolId id)
{
	if (id == 0 || id >= 0x80000000U)
		throw AssemblerError("PIP2 pool ID must be between 1 and 0x7fffffff");
	op(opcode, destination, source);
	appendU32(code_, id);
}

void PipAssembler::longFixup(Opcode opcode, Register destination, Register source,
	const std::string& target)
{
	const uint32_t instruction = static_cast<uint32_t>(code_.size());
	op(opcode, destination, source);
	appendU32(code_, 0);
	fixups_.push_back({FixupKind::Long, instruction, instruction + 4, target});
}

void PipAssembler::call(const std::string& target)
{
	longFixup(CALLl, zero, zero, target);
}

void PipAssembler::jump(const std::string& target)
{
	longFixup(JPl, zero, zero, target);
}

void PipAssembler::branch(Opcode opcode, Register left, Register right,
	const std::string& target)
{
	longFixup(opcode, left, right, target);
}

void PipAssembler::branchImmediate(Opcode opcode, Register value, int8_t comparison,
	const std::string& target)
{
	const uint32_t instruction = static_cast<uint32_t>(code_.size());
	op(opcode, value, static_cast<Register>(static_cast<uint8_t>(comparison)), 0);
	fixups_.push_back({FixupKind::Short, instruction, instruction + 3, target});
}

PipProgram PipAssembler::finish() const
{
	std::vector<uint8_t> resolved = code_;
	for (const Fixup& fixup : fixups_)
	{
		const auto found = labels_.find(fixup.target);
		if (found == labels_.end())
			throw AssemblerError("Undefined assembly label: " + fixup.target);
		const int64_t displacement = static_cast<int64_t>(found->second) - fixup.instruction;
		if (fixup.kind == FixupKind::Short)
		{
			if (displacement % 4 != 0 || displacement / 4 < -128 || displacement / 4 > 127)
				throw AssemblerError("Short branch is out of range: " + fixup.target);
			resolved[fixup.patchOffset] = static_cast<uint8_t>(displacement / 4);
		}
		else
		{
			if (displacement < -0x40000000LL || displacement > 0x3fffffffLL)
				throw AssemblerError("Long branch is out of range: " + fixup.target);
			writeU32(resolved.data() + fixup.patchOffset,
				encodePipImmediate(static_cast<int32_t>(displacement)));
		}
	}
	return PipProgram(baseOffset_, std::move(resolved), labels_);
}

} // namespace mophunmod
