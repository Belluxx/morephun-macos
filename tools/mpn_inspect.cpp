#include "mophunmod/mpn_image.h"
#include "mophunmod/pool_table.h"
#include "mophunmod/target.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace mophunmod;

constexpr uint8_t HighestKnownOpcode = 0x73;

uint32_t readU32(const uint8_t* bytes)
{
	return static_cast<uint32_t>(bytes[0]) |
		static_cast<uint32_t>(bytes[1]) << 8 |
		static_cast<uint32_t>(bytes[2]) << 16 |
		static_cast<uint32_t>(bytes[3]) << 24;
}

std::string imageString(const MpnImage& image, uint32_t offset)
{
	if (offset >= image.strings().size())
		throw std::runtime_error("String offset is outside the name table");
	const auto begin = image.strings().begin() + offset;
	const auto end = std::find(begin, image.strings().end(), 0);
	if (end == image.strings().end())
		throw std::runtime_error("Unterminated name-table string");
	return std::string(begin, end);
}

std::vector<uint8_t> readFile(const std::string& path)
{
	std::ifstream input(path, std::ios::binary | std::ios::ate);
	if (!input)
		throw std::runtime_error("Unable to open " + path);

	const std::streamoff size = input.tellg();
	if (size < 0)
		throw std::runtime_error("Unable to determine file size");
	std::vector<uint8_t> bytes(static_cast<std::size_t>(size));
	input.seekg(0);
	if (!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()), size))
		throw std::runtime_error("Unable to read " + path);
	return bytes;
}

void printTarget(const MpnImage& image, bool likelyEncrypted)
{
	const TargetDetection detection = builtInTargets().detect(image, !likelyEncrypted);
	if (detection.profile != nullptr)
		std::cout << "Target: " << detection.profile->displayName() << " ("
			<< detection.profile->id() << ")\n";
	else if (detection.compatibility == TargetCompatibility::PreviouslyModified)
		std::cout << "Target marker: " << detection.modification.targetId << '\n';
	else
		std::cout << "Target: unknown\n";
	std::cout << "Compatibility: " << targetCompatibilityName(detection.compatibility)
		<< " — " << detection.message << '\n';

	if (detection.compatibility != TargetCompatibility::Compatible ||
		detection.profile == nullptr)
		return;

	const ResolvedTarget target = detection.profile->resolve(image, !likelyEncrypted);
	std::cout << "Target symbols (code signatures "
		<< (likelyEncrypted ? "deferred for encrypted input" : "validated") << "):\n";
	for (const ResolvedSymbol& symbol : target.symbols())
	{
		std::cout << "  " << symbol.name << " [" << targetSymbolKindName(symbol.kind)
			<< "] = " << symbol.value;
		if (symbol.source == TargetSymbolSource::ImportedSyscall)
			std::cout << " (import " << symbol.sourceName << ')';
		std::cout << '\n';
	}
}

} // namespace

int main(int argc, char* argv[])
{
	if (argc != 2)
	{
		std::cerr << "Usage: " << argv[0] << " <rom.mpn>\n";
		return 2;
	}

	try
	{
		const std::vector<uint8_t> bytes = readFile(argv[1]);
		const MpnImage image = MpnImage::parse(bytes);
		const MpnHeader& header = image.header();
		const PoolTable pool(image.poolBytes());

		std::cout << "File: " << argv[1] << '\n'
			<< "Size: " << bytes.size() << " bytes\n"
			<< "Heap: " << header.heapSize << " units (" << header.heapSize * 4ULL
			<< " bytes)\n"
			<< "Stack: " << header.stackSize << " units (" << header.stackSize * 4ULL
			<< " bytes)\n"
			<< "Flags: 0x" << std::hex << std::setw(4) << std::setfill('0')
			<< header.flags << std::dec << '\n'
			<< "Sections: code=" << header.codeSize << ", data=" << header.dataSize
			<< ", bss=" << header.bssSize << ", resources=" << header.resourceSize
			<< ", directory=" << header.directorySize << '\n'
			<< "Addresses: " << pool.size() << '\n'
			<< "Name table: " << image.strings().size() << " bytes\n"
			<< "Trailing data: " << image.trailingData().size() << " bytes\n";

		if (image.resources().size() >= 4)
		{
			const uint32_t tableSize = readU32(image.resources().data());
			if (tableSize >= 8 && tableSize % 4 == 0 && tableSize <= image.resources().size())
				std::cout << "Resources: " << tableSize / 4 - 1 << '\n';
		}

		const std::size_t opcodeSlots = image.code().size() / 4;
		std::size_t knownOpcodeSlots = 0;
		for (std::size_t offset = 0; offset + 4 <= image.code().size(); offset += 4)
			knownOpcodeSlots += image.code()[offset] <= HighestKnownOpcode;
		const double opcodeScore = opcodeSlots == 0 ? 0.0 :
			100.0 * static_cast<double>(knownOpcodeSlots) /
				static_cast<double>(opcodeSlots);
		const bool likelyEncrypted = opcodeSlots != 0 && opcodeScore < 60.0;
		std::cout << "Opcode-slot heuristic: " << std::fixed << std::setprecision(1)
			<< opcodeScore << "% in known range; "
			<< (likelyEncrypted ? "likely encrypted" : "not obviously encrypted") << '\n';

		printTarget(image, likelyEncrypted);

		std::map<uint8_t, uint32_t> addressTypes;
		std::vector<std::string> imports;
		for (std::size_t index = 0; index < pool.size(); ++index)
		{
			const PoolEntry& entry = pool.at(static_cast<PoolId>(index + 1));
			++addressTypes[entry.type];
			if (entry.type == 0x02)
				imports.push_back(imageString(image, entry.argument));
		}

		std::cout << "Address types:";
		for (const auto& entry : addressTypes)
			std::cout << " 0x" << std::hex << std::setw(2) << std::setfill('0')
				<< static_cast<unsigned>(entry.first) << std::dec << ':' << entry.second;
		std::cout << "\nImports (" << imports.size() << "):\n";
		for (const std::string& import : imports)
			std::cout << "  " << import << '\n';
	}
	catch (const std::exception& error)
	{
		std::cerr << "Inspection failed: " << error.what() << '\n';
		return 1;
	}

	return 0;
}
