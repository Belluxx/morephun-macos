#include <iostream>
#include <fstream>
#include <algorithm>
#include <cstring>
#include "mophun_vm.h"
#include "opcodes.h"
#include "vmgp_header.h"


bool MophunVM::loadRom(const std::string& romPath)
{
	std::ifstream romFile(romPath, std::ios::in | std::ios::binary | std::ios::ate);
	if (romFile.is_open())
	{
		const auto fileSize = romFile.tellg();
		if (fileSize < static_cast<std::streamoff>(sizeof(VMGPHeader)) ||
			fileSize > static_cast<std::streamoff>(memory.ram.size()))
		{
			std::cerr << "ROM size is outside the supported range" << std::endl;
			return false;
		}

		const uint64_t romSize = static_cast<uint64_t>(fileSize);
		std::fill(memory.ram.begin(), memory.ram.end(), 0);
		romFile.seekg(0, std::ios::beg);
		if (!romFile.read(reinterpret_cast<char*>(memory.ram.data()), static_cast<std::streamsize>(romSize)))
		{
			std::cerr << "Failed to read ROM" << std::endl;
			return false;
		}
		romFile.close();

		std::memcpy(&romHeader, memory.ram.data(), sizeof(romHeader));

		if (std::string(romHeader.magicNo, 4) != "VMGP")
			return false;
		if ((romHeader.flags & 0x8000) != 0)
		{
			std::cerr << "Compressed MPN sections are not supported yet" << std::endl;
			return false;
		}

		const uint64_t fileLayoutSize = sizeof(VMGPHeader) +
			static_cast<uint64_t>(romHeader.codeSize) + romHeader.dataSize + romHeader.resSize +
			static_cast<uint64_t>(romHeader.poolSize) * sizeof(PoolItem) + romHeader.stringSize;
		if (fileLayoutSize > romSize)
		{
			std::cerr << "ROM section sizes exceed the file size" << std::endl;
			return false;
		}

		const uint64_t opcodeSlots = romHeader.codeSize / sizeof(uint32_t);
		uint64_t knownOpcodeSlots = 0;
		for (uint64_t offset = sizeof(VMGPHeader);
			offset + sizeof(uint32_t) <= sizeof(VMGPHeader) + romHeader.codeSize;
			offset += sizeof(uint32_t))
		{
			knownOpcodeSlots += memory.ram[offset] <= LDHUi;
		}
		if (opcodeSlots != 0 && knownOpcodeSlots * 100 < opcodeSlots * 60)
		{
			std::cerr << "ROM code appears encrypted (only "
				<< knownOpcodeSlots * 100 / opcodeSlots
				<< "% of aligned words begin with a known opcode); decrypt a working copy first"
				<< std::endl;
			return false;
		}

		memory.codeSegStartAddr = sizeof(VMGPHeader);
		memory.dataSegStartAddr = memory.codeSegStartAddr + romHeader.codeSize;
		memory.bssSegStartAddr = memory.dataSegStartAddr + romHeader.dataSize;
		memory.resSegStartAddr = memory.bssSegStartAddr + romHeader.bssSize;
		memory.poolSegStartAddr = memory.resSegStartAddr + romHeader.resSize;
		memory.stringSegStartAddr = memory.poolSegStartAddr + (romHeader.poolSize * sizeof(PoolItem));
		memory.heapStartAddr = memory.stringSegStartAddr + romHeader.stringSize;
		memory.stackStartAddr = memory.ram.size() - romHeader.stackSize * 4;

		if (memory.heapStartAddr > memory.stackStartAddr)
		{
			std::cerr << "ROM sections overlap the requested stack" << std::endl;
			return false;
		}

		// Initialize BSS segment
		memory.ram.insert(memory.ram.begin() + memory.bssSegStartAddr, romHeader.bssSize, 0x0);
		memory.ram.resize(RAM_SIZE);

		poolParser();

		return true;
	}
	return false;
}
