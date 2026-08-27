#include "binary_io.h"
#include "interpreter.h"
#include "opcodes.h"
#include "pool.h"
#include "rom_decrypt.h"
#include "vmgp_header.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> readFile(const std::string& path)
{
	std::ifstream input(path, std::ios::binary | std::ios::ate);
	if (!input)
		throw std::runtime_error("Unable to open " + path);
	const std::streamoff length = input.tellg();
	if (length < static_cast<std::streamoff>(sizeof(VMGPHeader)))
		throw std::runtime_error("File is too small to be an MPN");
	std::vector<uint8_t> bytes(static_cast<size_t>(length));
	input.seekg(0);
	if (!input.read(reinterpret_cast<char*>(bytes.data()), length))
		throw std::runtime_error("Unable to read " + path);
	return bytes;
}

const char* opcodeName(uint8_t opcode)
{
	static const char* names[] = {
		"BREAKPOINT", "NOP", "ADD", "AND", "MUL", "DIV", "DIVU", "OR",
		"XOR", "SUB", "SLL", "SRA", "SRL", "NOT", "NEG", "EXSB",
		"EXSH", "MOV", "ADDB", "SUBB", "ANDB", "ORB", "MOVB", "ADDH",
		"SUBH", "ANDH", "ORH", "MOVH", "SLLi", "SRAi", "SRLi", "ADDQ",
		"MULQ", "ADDBi", "ANDBi", "ORBi", "SLLB", "SRLB", "SRAB", "ADDHi",
		"ANDHi", "SLLH", "SRLH", "SRAH", "BEQI", "BNEI", "BGEI", "BGEUI",
		"BGTI", "BGTUI", "BLEI", "BLEUI", "BLTI", "BLTUI", "BEQIB", "BNEIB",
		"BGEIB", "BGEUIB", "BGTIB", "BGTUIB", "BLEIB", "BLEUIB", "BLTIB", "BLTUIB",
		"LDQ", "JPr", "CALLr", "STORE", "RESTORE", "RET", "KILLTASK", "SLEEP",
		"SYSCPY", "SYSSET", "ADDi", "ANDi", "MULi", "DIVi", "DIVUi", "ORi",
		"XORi", "SUBi", "STBd", "STHd", "STWd", "LDBd", "LDBHd", "LDWd",
		"LDBUd", "LDHUd", "LDI", "JPl", "CALLl", "BEQ", "BNE", "BGE",
		"BGEU", "BGT", "BGTU", "BLE", "BLEU", "BLT", "BLTU", "SYSCALL4",
		"SYSCALL0", "SYSCALL1", "SYSCALL2", "SYSCALL3", "STBi", "STHi", "STWi",
		"LDBi", "LDHi", "LDWi", "LDBUi", "LDHUi"};
	return opcode <= LDHUi ? names[opcode] : "UNKNOWN";
}

std::string regName(uint8_t reg)
{
	static const char* names[] = {
		"zero", "sp", "ra", "fp", "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
		"p0", "p1", "p2", "p3", "g0", "g1", "g2", "g3", "g4", "g5", "g6", "g7",
		"g8", "g9", "g10", "g11", "g12", "g13", "r0", "r1", "pc"};
	if ((reg & 3U) == 0 && reg / 4 < sizeof(names) / sizeof(names[0]))
		return names[reg / 4];
	std::ostringstream output;
	output << "r?" << static_cast<unsigned>(reg);
	return output.str();
}

bool hasLongImmediate(uint8_t opcode)
{
	return (opcode >= ADDi && opcode <= CALLl) || (opcode >= BEQ && opcode <= BLTU);
}

std::string escapedString(const uint8_t* begin, const uint8_t* end)
{
	std::string result;
	while (begin < end && *begin != 0)
	{
		const unsigned char c = *begin++;
		result += c >= 32 && c < 127 ? static_cast<char>(c) : '?';
	}
	return result;
}

struct PoolView {
	PoolItem item;
	std::string name;
};

std::string poolDescription(const std::vector<PoolView>& pool, uint32_t id, unsigned depth = 0)
{
	if (id == 0 || id > pool.size())
		return "invalid-pool";
	if (depth > 8)
		return "relative-cycle";
	const PoolView& view = pool[id - 1];
	std::ostringstream output;
	if (view.item.segment_1 == 8)
		output << poolDescription(pool, view.item.segmentoffset, depth + 1)
			<< "+0x" << std::hex << view.item.extra;
	else
	{
		switch (view.item.segment_0)
		{
			case 0: output << "import " << view.name; break;
			case 1: output << "code+0x" << std::hex << view.item.extra; break;
			case 2: output << "data+0x" << std::hex << view.item.extra; break;
			case 4: output << "bss+0x" << std::hex << view.item.extra; break;
			case 6: output << "constant 0x" << std::hex << view.item.extra; break;
			default: output << "type 0x" << std::hex
				<< static_cast<unsigned>(view.item.segment_0 * 16 + view.item.segment_1)
				<< " arg=0x" << view.item.segmentoffset << " value=0x" << view.item.extra;
		}
		if (!view.name.empty() && view.item.segment_0 != 0)
			output << " (" << view.name << ')';
	}
	return output.str();
}

void printInstruction(const std::vector<uint8_t>& bytes, const VMGPHeader& header,
	const std::vector<PoolView>& pool, uint32_t offset)
{
	const uint32_t absolute = sizeof(VMGPHeader) + offset;
	const PIPInstruction instruction = decodePIPInstruction(bytes.data() + absolute);
	std::cout << std::hex << std::setw(8) << std::setfill('0') << offset << "  "
		<< std::setw(2) << static_cast<unsigned>(instruction.opcode) << ' '
		<< std::setw(2) << static_cast<unsigned>(instruction.dest) << ' '
		<< std::setw(2) << static_cast<unsigned>(instruction.source) << ' '
		<< std::setw(2) << static_cast<unsigned>(instruction.extra) << "  "
		<< std::left << std::setw(11) << std::setfill(' ') << opcodeName(instruction.opcode)
		<< std::right;

	if (instruction.opcode == LDQ)
		std::cout << regName(instruction.dest) << ", " << std::dec
			<< static_cast<int16_t>(instruction.word);
	else if (instruction.opcode == RET || instruction.opcode == RESTORE || instruction.opcode == STORE)
		std::cout << regName(instruction.dest) << ", " << regName(instruction.source);
	else if (instruction.opcode == JPr || instruction.opcode == CALLr)
		std::cout << regName(instruction.dest);
	else if (instruction.opcode >= BEQI && instruction.opcode <= BLTUIB)
	{
		const int32_t destination = static_cast<int32_t>(offset + 4) +
			(static_cast<int8_t>(instruction.extra) - 1) * 4;
		std::cout << regName(instruction.dest) << ", " << std::dec
			<< static_cast<int>(static_cast<int8_t>(instruction.source)) << ", ->0x"
			<< std::hex << destination;
	}
	else
		std::cout << regName(instruction.dest) << ", " << regName(instruction.source)
			<< ", " << regName(instruction.extra);

	if (hasLongImmediate(instruction.opcode) && offset + 8 <= header.codeSize)
	{
		const uint32_t immediate = readLittleU32(bytes.data() + absolute + 4);
		std::cout << "  [0x" << std::hex << immediate;
		if ((immediate & 0x80000000U) != 0)
		{
			const int32_t value = decodeImmediate(immediate);
			std::cout << " = " << std::dec << value;
			if (instruction.opcode == JPl || (instruction.opcode >= BEQ && instruction.opcode <= BLTU))
				std::cout << ", ->0x" << std::hex << static_cast<uint32_t>(offset + value);
		}
		else
			std::cout << " = #" << std::dec << immediate << ' ' << poolDescription(pool, immediate);
		std::cout << ']';
	}
	std::cout << '\n';
}

} // namespace

int main(int argc, char* argv[])
{
	if (argc < 2 || argc > 4)
	{
		std::cerr << "Usage: " << argv[0] << " <rom.mpn> [start-offset [end-offset]]\n";
		return 2;
	}

	try
	{
		std::vector<uint8_t> bytes = readFile(argv[1]);
		const VMGPHeader header = decodeVMGPHeader(bytes.data());
		if (std::string(header.magicNo, 4) != "VMGP")
			throw std::runtime_error("Not a VMGP executable");
		uint32_t knownOpcodes = 0;
		for (uint32_t offset = 0; offset + 4 <= header.codeSize; offset += 4)
			knownOpcodes += bytes[sizeof(VMGPHeader) + offset] <= LDHUi;
		if (header.codeSize != 0 && knownOpcodes * 100 / (header.codeSize / 4) < 60)
		{
			std::string decryptError;
			if (!decryptCommercialCode(bytes, header, decryptError))
				throw std::runtime_error("Unable to decrypt code: " + decryptError);
		}

		const uint32_t resourceOffset = sizeof(VMGPHeader) + header.codeSize + header.dataSize;
		const uint32_t poolOffset = resourceOffset + header.resSize;
		const uint32_t stringOffset = poolOffset + header.poolSize * PoolItemSize;
		const uint32_t stringEnd = stringOffset + header.stringSize;
		if (stringEnd > bytes.size())
			throw std::runtime_error("MPN sections exceed the file size");

		std::vector<PoolView> pool;
		for (uint32_t index = 0; index < header.poolSize; ++index)
		{
			PoolView view{decodePoolItemBytes(bytes.data() + poolOffset + index * PoolItemSize), std::string()};
			if ((view.item.segment_0 == 0 || view.item.segment_1 == 3) &&
				view.item.segmentoffset < header.stringSize)
				view.name = escapedString(bytes.data() + stringOffset + view.item.segmentoffset,
					bytes.data() + stringEnd);
			pool.push_back(view);
		}

		uint32_t start = argc >= 3 ? static_cast<uint32_t>(std::stoul(argv[2], nullptr, 0)) : 0;
		uint32_t end = argc >= 4 ? static_cast<uint32_t>(std::stoul(argv[3], nullptr, 0)) : header.codeSize;
		start &= ~3U;
		end = std::min(header.codeSize, end & ~3U);
		for (uint32_t offset = start; offset < end; )
		{
			const uint8_t opcode = bytes[sizeof(VMGPHeader) + offset];
			printInstruction(bytes, header, pool, offset);
			offset += hasLongImmediate(opcode) ? 8 : 4;
		}
	}
	catch (const std::exception& error)
	{
		std::cerr << "Disassembly failed: " << error.what() << '\n';
		return 1;
	}
	return 0;
}
