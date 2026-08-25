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

constexpr uint64_t HeaderSize = 40;
constexpr uint8_t HighestKnownOpcode = 0x73;

uint16_t readU16(const std::vector<uint8_t>& bytes, uint64_t offset)
{
    if (offset + 2 > bytes.size())
        throw std::runtime_error("Unexpected end of file");
    return static_cast<uint16_t>(bytes[offset]) |
        static_cast<uint16_t>(bytes[offset + 1]) << 8;
}

uint32_t readU24(const std::vector<uint8_t>& bytes, uint64_t offset)
{
    if (offset + 3 > bytes.size())
        throw std::runtime_error("Unexpected end of file");
    return static_cast<uint32_t>(bytes[offset]) |
        static_cast<uint32_t>(bytes[offset + 1]) << 8 |
        static_cast<uint32_t>(bytes[offset + 2]) << 16;
}

uint32_t readU32(const std::vector<uint8_t>& bytes, uint64_t offset)
{
    if (offset + 4 > bytes.size())
        throw std::runtime_error("Unexpected end of file");
    return static_cast<uint32_t>(bytes[offset]) |
        static_cast<uint32_t>(bytes[offset + 1]) << 8 |
        static_cast<uint32_t>(bytes[offset + 2]) << 16 |
        static_cast<uint32_t>(bytes[offset + 3]) << 24;
}

std::string readString(const std::vector<uint8_t>& bytes, uint64_t offset, uint64_t limit)
{
    if (offset >= bytes.size() || offset >= limit)
        throw std::runtime_error("String offset is outside the name table");

    const auto begin = bytes.begin() + static_cast<std::ptrdiff_t>(offset);
    const auto end = bytes.begin() + static_cast<std::ptrdiff_t>(std::min<uint64_t>(limit, bytes.size()));
    const auto terminator = std::find(begin, end, 0);
    if (terminator == end)
        throw std::runtime_error("Unterminated name-table string");
    return std::string(begin, terminator);
}

std::vector<uint8_t> readFile(const std::string& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        throw std::runtime_error("Unable to open " + path);

    const auto size = input.tellg();
    if (size < 0)
        throw std::runtime_error("Unable to determine file size");

    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    input.seekg(0);
    if (!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()), size))
        throw std::runtime_error("Unable to read " + path);
    return bytes;
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " <rom.mpn>" << std::endl;
        return 2;
    }

    try
    {
        const auto bytes = readFile(argv[1]);
        if (bytes.size() < HeaderSize || std::string(bytes.begin(), bytes.begin() + 4) != "VMGP")
            throw std::runtime_error("Not a VMGP executable");

        const uint32_t heapUnits = readU32(bytes, 4);
        const uint16_t stackUnits = readU16(bytes, 8);
        const uint16_t flags = readU16(bytes, 10);
        const uint32_t codeSize = readU32(bytes, 12);
        const uint32_t dataSize = readU32(bytes, 16);
        const uint32_t bssSize = readU32(bytes, 20);
        const uint32_t resourceSize = readU32(bytes, 24);
        const uint32_t directorySize = readU32(bytes, 28);
        const uint32_t addressCount = readU32(bytes, 32);
        const uint32_t nameTableSize = readU32(bytes, 36);

        const uint64_t codeOffset = HeaderSize;
        const uint64_t dataOffset = codeOffset + codeSize;
        const uint64_t resourceOffset = dataOffset + dataSize;
        const uint64_t addressOffset = resourceOffset + resourceSize;
        const uint64_t nameTableOffset = addressOffset + static_cast<uint64_t>(addressCount) * 8;
        const uint64_t expectedSize = nameTableOffset + nameTableSize;

        if ((flags & 0x8000) != 0)
            throw std::runtime_error("Compressed section layout is not supported by this inspector yet");
        if (expectedSize > bytes.size())
            throw std::runtime_error("Section sizes extend beyond the end of the file");

        std::cout << "File: " << argv[1] << '\n'
            << "Size: " << bytes.size() << " bytes\n"
            << "Heap: " << heapUnits << " units (" << heapUnits * 4ULL << " bytes)\n"
            << "Stack: " << stackUnits << " units (" << stackUnits * 4ULL << " bytes)\n"
            << "Flags: 0x" << std::hex << std::setw(4) << std::setfill('0') << flags << std::dec << '\n'
            << "Sections: code=" << codeSize << ", data=" << dataSize << ", bss=" << bssSize
            << ", resources=" << resourceSize << ", directory=" << directorySize << '\n'
            << "Addresses: " << addressCount << '\n'
            << "Name table: " << nameTableSize << " bytes\n";

        if (resourceSize >= 4)
        {
            const uint32_t resourceTableSize = readU32(bytes, resourceOffset);
            if (resourceTableSize >= 8 && resourceTableSize % 4 == 0 && resourceTableSize <= resourceSize)
                std::cout << "Resources: " << resourceTableSize / 4 - 1 << '\n';
        }

        uint64_t opcodeSlots = codeSize / 4;
        uint64_t knownOpcodeSlots = 0;
        for (uint64_t offset = codeOffset; offset + 4 <= dataOffset; offset += 4)
            knownOpcodeSlots += bytes[offset] <= HighestKnownOpcode;

        const double opcodeScore = opcodeSlots == 0 ? 0.0 :
            100.0 * static_cast<double>(knownOpcodeSlots) / static_cast<double>(opcodeSlots);
        const bool likelyEncrypted = opcodeSlots != 0 && opcodeScore < 60.0;
        std::cout << "Opcode-slot heuristic: " << std::fixed << std::setprecision(1) << opcodeScore
            << "% in known range; " << (likelyEncrypted ? "likely encrypted" : "not obviously encrypted") << '\n';

        std::map<uint8_t, uint32_t> addressTypes;
        std::vector<std::string> imports;
        const uint64_t nameTableEnd = nameTableOffset + nameTableSize;
        for (uint32_t index = 0; index < addressCount; index++)
        {
            const uint64_t itemOffset = addressOffset + static_cast<uint64_t>(index) * 8;
            const uint8_t type = bytes[itemOffset];
            addressTypes[type]++;
            if (type == 0x02)
            {
                const uint32_t nameOffset = readU24(bytes, itemOffset + 1);
                imports.push_back(readString(bytes, nameTableOffset + nameOffset, nameTableEnd));
            }
        }

        std::cout << "Address types:";
        for (const auto& entry : addressTypes)
            std::cout << " 0x" << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<unsigned>(entry.first) << std::dec << ':' << entry.second;
        std::cout << "\nImports (" << imports.size() << "):\n";
        for (const auto& import : imports)
            std::cout << "  " << import << '\n';
    }
    catch (const std::exception& error)
    {
        std::cerr << "Inspection failed: " << error.what() << std::endl;
        return 1;
    }

    return 0;
}
