#pragma once

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "mophunmod/pool_table.h"

namespace mophunmod {

// PIP2 bytecode opcodes. Keeping these as an enum makes it impossible to pass a
// register where an opcode is expected while retaining the architecture names.
enum Opcode : uint8_t {
	BREAKPOINT = 0x00, NOP = 0x01, ADD = 0x02, AND = 0x03,
	MUL = 0x04, DIV = 0x05, DIVU = 0x06, OR = 0x07,
	XOR = 0x08, SUB = 0x09, SLL = 0x0a, SRA = 0x0b,
	SRL = 0x0c, NOT = 0x0d, NEG = 0x0e, EXSB = 0x0f,
	EXSH = 0x10, MOV = 0x11, ADDB = 0x12, SUBB = 0x13,
	ANDB = 0x14, ORB = 0x15, MOVB = 0x16, ADDH = 0x17,
	SUBH = 0x18, ANDH = 0x19, ORH = 0x1a, MOVH = 0x1b,
	SLLi = 0x1c, SRAi = 0x1d, SRLi = 0x1e, ADDQ = 0x1f,
	MULQ = 0x20, ADDBi = 0x21, ANDBi = 0x22, ORBi = 0x23,
	SLLB = 0x24, SRLB = 0x25, SRAB = 0x26, ADDHi = 0x27,
	ANDHi = 0x28, SLLH = 0x29, SRLH = 0x2a, SRAH = 0x2b,
	BEQI = 0x2c, BNEI = 0x2d, BGEI = 0x2e, BGEUI = 0x2f,
	BGTI = 0x30, BGTUI = 0x31, BLEI = 0x32, BLEUI = 0x33,
	BLTI = 0x34, BLTUI = 0x35, BEQIB = 0x36, BNEIB = 0x37,
	BGEIB = 0x38, BGEUIB = 0x39, BGTIB = 0x3a, BGTUIB = 0x3b,
	BLEIB = 0x3c, BLEUIB = 0x3d, BLTIB = 0x3e, BLTUIB = 0x3f,
	LDQ = 0x40, JPr = 0x41, CALLr = 0x42, STORE = 0x43,
	RESTORE = 0x44, RET = 0x45, KILLTASK = 0x46, SLEEP = 0x47,
	SYSCPY = 0x48, SYSSET = 0x49, ADDi = 0x4a, ANDi = 0x4b,
	MULi = 0x4c, DIVi = 0x4d, DIVUi = 0x4e, ORi = 0x4f,
	XORi = 0x50, SUBi = 0x51, STBd = 0x52, STHd = 0x53,
	STWd = 0x54, LDBd = 0x55, LDBHd = 0x56, LDWd = 0x57,
	LDBUd = 0x58, LDHUd = 0x59, LDI = 0x5a, JPl = 0x5b,
	CALLl = 0x5c, BEQ = 0x5d, BNE = 0x5e, BGE = 0x5f,
	BGEU = 0x60, BGT = 0x61, BGTU = 0x62, BLE = 0x63,
	BLEU = 0x64, BLT = 0x65, BLTU = 0x66, SYSCALL4 = 0x67,
	SYSCALL0 = 0x68, SYSCALL1 = 0x69, SYSCALL2 = 0x6a, SYSCALL3 = 0x6b,
	STBi = 0x6c, STHi = 0x6d, STWi = 0x6e, LDBi = 0x6f,
	LDHi = 0x70, LDWi = 0x71, LDBUi = 0x72, LDHUi = 0x73
};

enum Register : uint8_t {
	zero = 0x00, sp = 0x04, ra = 0x08, fp = 0x0c,
	s0 = 0x10, s1 = 0x14, s2 = 0x18, s3 = 0x1c,
	s4 = 0x20, s5 = 0x24, s6 = 0x28, s7 = 0x2c,
	p0 = 0x30, p1 = 0x34, p2 = 0x38, p3 = 0x3c,
	g0 = 0x40, g1 = 0x44, g2 = 0x48, g3 = 0x4c,
	g4 = 0x50, g5 = 0x54, g6 = 0x58, g7 = 0x5c,
	g8 = 0x60, g9 = 0x64, g10 = 0x68, g11 = 0x6c,
	g12 = 0x70, g13 = 0x74, r0 = 0x78, r1 = 0x7c, pc = 0x80
};

class AssemblerError : public std::runtime_error {
	public:
		explicit AssemblerError(const std::string& message) : std::runtime_error(message) {}
};

struct PipLabel {
	explicit PipLabel(const std::string& value) : name(value) {}
	std::string name;
};

uint32_t encodePipImmediate(int32_t value);

// Immutable result of assembly. Symbols become observable only after every fixup
// has been checked and resolved.
class PipProgram {
	public:
		const std::vector<uint8_t>& bytes() const { return bytes_; }
		uint32_t baseOffset() const { return baseOffset_; }
		uint32_t symbolOffset(const std::string& name) const;
		uint32_t symbolOffset(const PipLabel& label) const { return symbolOffset(label.name); }

	private:
		friend class PipAssembler;
		PipProgram(uint32_t baseOffset, std::vector<uint8_t>&& bytes,
			const std::map<std::string, uint32_t>& symbols) :
			baseOffset_(baseOffset), bytes_(std::move(bytes)), symbols_(symbols) {}

		uint32_t baseOffset_;
		std::vector<uint8_t> bytes_;
		std::map<std::string, uint32_t> symbols_;
};

class PipAssembler {
	public:
		explicit PipAssembler(uint32_t baseOffset = 0) : baseOffset_(baseOffset) {}

		void label(const std::string& name);
		void label(const PipLabel& value) { label(value.name); }

		void op(Opcode opcode, Register destination = zero, Register source = zero,
			uint8_t extra = 0);
		void ldq(Register destination, int16_t value);
		void immediate(Opcode opcode, Register destination, Register source, int32_t value);
		void pool(Opcode opcode, Register destination, Register source, PoolId id);
		void callPool(PoolId id) { pool(CALLl, zero, zero, id); }

		void call(const std::string& target);
		void call(const PipLabel& target) { call(target.name); }
		void jump(const std::string& target);
		void jump(const PipLabel& target) { jump(target.name); }
		void branch(Opcode opcode, Register left, Register right, const std::string& target);
		void branch(Opcode opcode, Register left, Register right, const PipLabel& target)
		{
			branch(opcode, left, right, target.name);
		}
		void branchImmediate(Opcode opcode, Register value, int8_t comparison,
			const std::string& target);
		void branchImmediate(Opcode opcode, Register value, int8_t comparison,
			const PipLabel& target)
		{
			branchImmediate(opcode, value, comparison, target.name);
		}

		uint32_t baseOffset() const { return baseOffset_; }
		std::size_t size() const { return code_.size(); }
		PipProgram finish() const;

	private:
		enum class FixupKind { Short, Long };
		struct Fixup {
			FixupKind kind;
			uint32_t instruction;
			uint32_t patchOffset;
			std::string target;
		};

		void longFixup(Opcode opcode, Register destination, Register source,
			const std::string& target);

		uint32_t baseOffset_;
		std::vector<uint8_t> code_;
		std::map<std::string, uint32_t> labels_;
		std::vector<Fixup> fixups_;
};

} // namespace mophunmod
