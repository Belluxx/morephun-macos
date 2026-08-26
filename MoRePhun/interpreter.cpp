#include "interpreter.h"
#include "mophun_vm.h"
#include "registers.h"
#include "opcodes.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <thread>

void MophunVM::emulate()
{
	auto read32 = [this](uint32_t reg) -> uint32_t {
		if (reg == zero)
			return 0;
		return registers[static_cast<unsigned char>(reg)];
	};
	auto write32 = [this](uint32_t reg, uint32_t value) {
		if (reg != zero)
			registers[static_cast<unsigned char>(reg)] = value;
	};
	auto read16 = [&read32](uint32_t reg) -> uint16_t {
		const uint32_t shift = (reg & 3U) * 8U;
		return static_cast<uint16_t>(read32(reg & ~3U) >> shift);
	};
	auto write16 = [&read32, &write32](uint32_t reg, uint16_t value) {
		const uint32_t base = reg & ~3U;
		const uint32_t shift = (reg & 3U) * 8U;
		const uint32_t mask = 0xffffU << shift;
		write32(base, (read32(base) & ~mask) | (static_cast<uint32_t>(value) << shift));
	};
	auto read8 = [&read32](uint32_t reg) -> uint8_t {
		const uint32_t shift = (reg & 3U) * 8U;
		return static_cast<uint8_t>(read32(reg & ~3U) >> shift);
	};
	auto write8 = [&read32, &write32](uint32_t reg, uint8_t value) {
		const uint32_t base = reg & ~3U;
		const uint32_t shift = (reg & 3U) * 8U;
		const uint32_t mask = 0xffU << shift;
		write32(base, (read32(base) & ~mask) | (static_cast<uint32_t>(value) << shift));
	};
	auto readMemory16 = [this](uint32_t address) -> uint16_t {
		return readLittleU16(memory.ram.data() + address);
	};
	auto readMemory32 = [this](uint32_t address) -> uint32_t {
		return readLittleU32(memory.ram.data() + address);
	};
	auto writeMemory16 = [this](uint32_t address, uint16_t value) {
		writeLittleU16(memory.ram.data() + address, value);
	};
	auto writeMemory32 = [this](uint32_t address, uint32_t value) {
		writeLittleU32(memory.ram.data() + address, value);
	};
	auto fetchImmediate = [this, &read32, &write32, &readMemory32]() -> uint32_t {
		const uint32_t immediateAddress = read32(pc);
		const uint32_t infoWord = readMemory32(immediateAddress);
		write32(pc, immediateAddress + sizeof(uint32_t));
		if ((infoWord & 0x80000000U) != 0)
			return static_cast<uint32_t>(decodeImmediate(infoWord));
		if (infoWord == 0 || infoWord > poolDataList.size())
		{
			std::cerr << "Invalid pool item " << infoWord << " at PC 0x"
				<< std::hex << immediateAddress << std::dec << std::endl;
			return 0;
		}
		return poolDataList[infoWord - 1].value;
	};

	const uint32_t instructionPc = read32(pc);
	const PIPInstruction instruction = decodePIPInstruction(memory.ram.data() + instructionPc);
	write32(pc, instructionPc + sizeof(uint32_t));

	auto branchShort = [&read32, &write32, &instruction](bool condition) {
		if (condition)
		{
			const int32_t offset = (static_cast<int8_t>(instruction.extra) - 1) * 4;
			write32(pc, static_cast<uint32_t>(static_cast<int64_t>(read32(pc)) + offset));
		}
	};
	auto branchLong = [&read32, &write32, &fetchImmediate, instructionPc](bool condition) {
		if (condition)
			write32(pc, instructionPc + fetchImmediate());
		else
			write32(pc, read32(pc) + sizeof(uint32_t));
	};

	const uint32_t d = instruction.dest;
	const uint32_t s = instruction.source;
	const uint32_t t = instruction.extra;

	switch (instruction.opcode)
	{
		case BREAKPOINT:
		case NOP:
			break;
		case ADD: write32(d, read32(s) + read32(t)); break;
		case AND: write32(d, read32(s) & read32(t)); break;
		case MUL: write32(d, read32(s) * read32(t)); break;
		case DIV:
			write32(d, read32(t) == 0 ? 0 : static_cast<uint32_t>(static_cast<int32_t>(read32(s)) / static_cast<int32_t>(read32(t))));
			break;
		case DIVU: write32(d, read32(t) == 0 ? 0 : read32(s) / read32(t)); break;
		case OR: write32(d, read32(s) | read32(t)); break;
		case XOR: write32(d, read32(s) ^ read32(t)); break;
		case SUB: write32(d, read32(s) - read32(t)); break;
		case SLL: write32(d, read32(s) << (read32(t) & 31U)); break;
		case SRA: write32(d, static_cast<uint32_t>(static_cast<int32_t>(read32(s)) >> (read32(t) & 31U))); break;
		case SRL: write32(d, read32(s) >> (read32(t) & 31U)); break;
		case NOT: write32(d, ~read32(s)); break;
		case NEG: write32(d, static_cast<uint32_t>(-static_cast<int32_t>(read32(s)))); break;
		case EXSB: write32(d, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(read8(s))))); break;
		case EXSH: write32(d, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(read16(s))))); break;
		case MOV: write32(d, read32(s)); break;
		case ADDB: write8(d, static_cast<uint8_t>(read8(s) + read8(t))); break;
		case SUBB: write8(d, static_cast<uint8_t>(read8(s) - read8(t))); break;
		case ANDB: write8(d, static_cast<uint8_t>(read8(s) & read8(t))); break;
		case ORB: write8(d, static_cast<uint8_t>(read8(s) | read8(t))); break;
		case MOVB: write8(d, read8(s)); break;
		case ADDH: write16(d, static_cast<uint16_t>(read16(s) + read16(t))); break;
		case SUBH: write16(d, static_cast<uint16_t>(read16(s) - read16(t))); break;
		case ANDH: write16(d, static_cast<uint16_t>(read16(s) & read16(t))); break;
		case ORH: write16(d, static_cast<uint16_t>(read16(s) | read16(t))); break;
		case MOVH: write16(d, read16(s)); break;
		case SLLi: write32(d, read32(s) << (t & 31U)); break;
		case SRAi: write32(d, static_cast<uint32_t>(static_cast<int32_t>(read32(s)) >> (t & 31U))); break;
		case SRLi: write32(d, read32(s) >> (t & 31U)); break;
		case ADDQ: write32(d, read32(s) + static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(t)))); break;
		case MULQ: write32(d, read32(s) * t); break;
		case ADDBi: write8(d, static_cast<uint8_t>(read8(s) + t)); break;
		case ANDBi: write8(d, static_cast<uint8_t>(read8(s) & t)); break;
		case ORBi: write8(d, static_cast<uint8_t>(read8(s) | t)); break;
		case SLLB: write8(d, static_cast<uint8_t>(read8(s) << (t & 7U))); break;
		case SRLB: write8(d, static_cast<uint8_t>(read8(s) >> (t & 7U))); break;
		case SRAB: write8(d, static_cast<uint8_t>(static_cast<int8_t>(read8(s)) >> (t & 7U))); break;
		case ADDHi: write16(d, static_cast<uint16_t>(read16(s) + static_cast<int8_t>(t))); break;
		case ANDHi: write16(d, static_cast<uint16_t>(read16(s) & t)); break;
		case SLLH: write16(d, static_cast<uint16_t>(read16(s) << (t & 15U))); break;
		case SRLH: write16(d, static_cast<uint16_t>(read16(s) >> (t & 15U))); break;
		case SRAH: write16(d, static_cast<uint16_t>(static_cast<int16_t>(read16(s)) >> (t & 15U))); break;

		case BEQI: branchShort(read32(d) == static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(s)))); break;
		case BNEI: branchShort(read32(d) != static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(s)))); break;
		case BGEI: branchShort(static_cast<int32_t>(read32(d)) >= static_cast<int8_t>(s)); break;
		case BGEUI: branchShort(read32(d) >= s); break;
		case BGTI: branchShort(static_cast<int32_t>(read32(d)) > static_cast<int8_t>(s)); break;
		case BGTUI: branchShort(read32(d) > s); break;
		case BLEI: branchShort(static_cast<int32_t>(read32(d)) <= static_cast<int8_t>(s)); break;
		case BLEUI: branchShort(read32(d) <= s); break;
		case BLTI: branchShort(static_cast<int32_t>(read32(d)) < static_cast<int8_t>(s)); break;
		case BLTUI: branchShort(read32(d) < s); break;
		case BEQIB: branchShort(read8(d) == s); break;
		case BNEIB: branchShort(read8(d) != s); break;
		case BGEIB: branchShort(static_cast<int8_t>(read8(d)) >= static_cast<int8_t>(s)); break;
		case BGEUIB: branchShort(read8(d) >= s); break;
		case BGTIB: branchShort(static_cast<int8_t>(read8(d)) > static_cast<int8_t>(s)); break;
		case BGTUIB: branchShort(read8(d) > s); break;
		case BLEIB: branchShort(static_cast<int8_t>(read8(d)) <= static_cast<int8_t>(s)); break;
		case BLEUIB: branchShort(read8(d) <= s); break;
		case BLTIB: branchShort(static_cast<int8_t>(read8(d)) < static_cast<int8_t>(s)); break;
		case BLTUIB: branchShort(read8(d) < s); break;

		case LDQ: write32(d, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(instruction.word)))); break;
		case JPr: write32(pc, read32(d)); break;
		case CALLr:
			write32(ra, read32(pc));
			write32(pc, read32(d));
			break;
		case STORE:
		{
			uint32_t currentSp = read32(sp);
			if (d == 0)
			{
				currentSp -= sizeof(uint32_t);
				writeMemory32(currentSp, read32(ra));
			}
			else
			{
				for (uint32_t reg = d, end = d + s; reg < end; reg += sizeof(uint32_t))
				{
					currentSp -= sizeof(uint32_t);
					writeMemory32(currentSp, read32(reg));
				}
			}
			write32(sp, currentSp);
			break;
		}
		case RESTORE:
		case RET:
		{
			uint32_t currentSp = read32(sp);
			if (d == 0)
			{
				write32(ra, readMemory32(currentSp));
				currentSp += sizeof(uint32_t);
			}
			else
			{
				const int32_t end = static_cast<int32_t>(d) - static_cast<int32_t>(s);
				for (int32_t reg = static_cast<int32_t>(d); reg > end; reg -= sizeof(uint32_t))
				{
					write32(static_cast<uint32_t>(reg), readMemory32(currentSp));
					currentSp += sizeof(uint32_t);
				}
			}
			write32(sp, currentSp);
			if (instruction.opcode == RET)
				write32(pc, read32(ra));
			break;
		}
		case KILLTASK:
		case SLEEP:
			std::this_thread::yield();
			break;
		case SYSCPY:
			std::memmove(memory.ram.data() + read32(d), memory.ram.data() + read32(s), read32(t));
			break;
		case SYSSET:
			std::memset(memory.ram.data() + read32(d), read8(s), read32(t));
			break;

		case ADDi: write32(d, read32(s) + fetchImmediate()); break;
		case ANDi: write32(d, read32(s) & fetchImmediate()); break;
		case MULi: write32(d, read32(s) * fetchImmediate()); break;
		case DIVi:
		{
			const int32_t divisor = static_cast<int32_t>(fetchImmediate());
			write32(d, divisor == 0 ? 0 : static_cast<uint32_t>(static_cast<int32_t>(read32(s)) / divisor));
			break;
		}
		case DIVUi:
		{
			const uint32_t divisor = fetchImmediate();
			write32(d, divisor == 0 ? 0 : read32(s) / divisor);
			break;
		}
		case ORi: write32(d, read32(s) | fetchImmediate()); break;
		case XORi: write32(d, read32(s) ^ fetchImmediate()); break;
		case SUBi: write32(d, read32(s) - fetchImmediate()); break;
		case STBd: memory.ram[read32(s) + fetchImmediate()] = read8(d); break;
		case STHd: writeMemory16(read32(s) + fetchImmediate(), read16(d)); break;
		case STWd: writeMemory32(read32(s) + fetchImmediate(), read32(d)); break;
		case LDBd: write32(d, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(memory.ram[read32(s) + fetchImmediate()])))); break;
		case LDBHd: write32(d, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(readMemory16(read32(s) + fetchImmediate()))))); break;
		case LDWd: write32(d, readMemory32(read32(s) + fetchImmediate())); break;
		case LDBUd: write32(d, memory.ram[read32(s) + fetchImmediate()]); break;
		case LDHUd: write32(d, readMemory16(read32(s) + fetchImmediate())); break;
		case LDI: write32(d, fetchImmediate()); break;
		case JPl:
		{
			const uint32_t displacement = fetchImmediate();
			write32(pc, instructionPc + displacement);
			break;
		}
		case CALLl:
		{
			const uint32_t infoAddress = read32(pc);
			const uint32_t infoWord = readMemory32(infoAddress);
			if ((infoWord & 0x80000000U) != 0)
			{
				write32(ra, infoAddress + sizeof(uint32_t));
				write32(pc, instructionPc + static_cast<uint32_t>(decodeImmediate(infoWord)));
			}
			else if (infoWord != 0 && infoWord <= poolDataList.size())
			{
				PoolData& poolData = poolDataList[infoWord - 1];
				if (poolData.isSyscall)
				{
					write32(pc, infoAddress + sizeof(uint32_t));
					poolData.fun();
				}
				else
				{
					write32(ra, infoAddress + sizeof(uint32_t));
					write32(pc, poolData.value);
				}
			}
			else
			{
				std::cerr << "Invalid call pool item " << infoWord << " at PC 0x"
					<< std::hex << instructionPc << std::dec << std::endl;
			}
			break;
		}
		case BEQ: branchLong(read32(d) == read32(s)); break;
		case BNE: branchLong(read32(d) != read32(s)); break;
		case BGE: branchLong(static_cast<int32_t>(read32(d)) >= static_cast<int32_t>(read32(s))); break;
		case BGEU: branchLong(read32(d) >= read32(s)); break;
		case BGT: branchLong(static_cast<int32_t>(read32(d)) > static_cast<int32_t>(read32(s))); break;
		case BGTU: branchLong(read32(d) > read32(s)); break;
		case BLE: branchLong(static_cast<int32_t>(read32(d)) <= static_cast<int32_t>(read32(s))); break;
		case BLEU: branchLong(read32(d) <= read32(s)); break;
		case BLT: branchLong(static_cast<int32_t>(read32(d)) < static_cast<int32_t>(read32(s))); break;
		case BLTU: branchLong(read32(d) < read32(s)); break;

		default:
			std::cerr << "Unknown opcode 0x" << std::hex
				<< static_cast<uint32_t>(instruction.opcode) << " at PC 0x"
				<< instructionPc << std::dec << std::endl;
			break;
	}
}
