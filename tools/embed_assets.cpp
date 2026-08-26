#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>


namespace {

struct AssetDescription {
	const char* symbol;
	const char* fileName;
	const char* logicalName;
};

const AssetDescription Assets[] = {
	{"vrMpn", "VRally2_[RC14EU]_[multiscreen]_M5.mpn", "V-Rally 2"},
	{"vrMulti", "VRally2_multipack.mpc", "multipack"},
	{"vrExtra1", "VRally2_extrapack1.mpc", "extrapack1"},
	{"vrExtra2", "VRally2_extrapack2.mpc", "extrapack2"},
	{"vrExtra3", "VRally2_extrapack3.mpc", "extrapack3"},
	{"vrExtra4", "VRally2_extrapack4.mpc", "extrapack4"}
};

std::vector<unsigned char> readFile(const char* path)
{
	std::ifstream input(path, std::ios::binary | std::ios::ate);
	if (!input)
		throw std::runtime_error(std::string("Unable to open asset: ") + path);
	const std::streamoff length = input.tellg();
	if (length <= 0)
		throw std::runtime_error(std::string("Asset is empty: ") + path);
	std::vector<unsigned char> bytes(static_cast<size_t>(length));
	input.seekg(0);
	if (!input.read(reinterpret_cast<char*>(bytes.data()), length))
		throw std::runtime_error(std::string("Unable to read asset: ") + path);
	return bytes;
}

void writeByteArray(std::ostream& output, const AssetDescription& asset,
	const std::vector<unsigned char>& bytes)
{
	output << "alignas(16) const uint8_t " << asset.symbol << "[] = {\n";
	output << std::hex << std::setfill('0');
	for (size_t index = 0; index < bytes.size(); ++index)
	{
		if (index % 12 == 0)
			output << "\t";
		output << "0x" << std::setw(2) << static_cast<unsigned>(bytes[index]);
		if (index + 1 != bytes.size())
			output << ',';
		output << (index % 12 == 11 || index + 1 == bytes.size() ? "\n" : " ");
	}
	output << std::dec << "};\n\n";
}

} // namespace

int main(int argc, char* argv[])
{
	constexpr int AssetCount = sizeof(Assets) / sizeof(Assets[0]);
	if (argc != AssetCount + 2)
	{
		std::cerr << "Usage: " << argv[0]
			<< " <output.cpp> <game.mpn> <multipack.mpc> <extrapack1.mpc> ... <extrapack4.mpc>"
			<< std::endl;
		return 2;
	}

	const std::string outputPath = argv[1];
	const std::string temporaryPath = outputPath + ".tmp";
	try
	{
		std::vector<std::vector<unsigned char>> contents;
		for (int index = 0; index < AssetCount; ++index)
			contents.push_back(readFile(argv[index + 2]));

		std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
		if (!output)
			throw std::runtime_error("Unable to create generated asset source: " + temporaryPath);
		output << "// Generated from user-supplied game data. Do not distribute without permission.\n"
			<< "#include \"embedded_assets.h\"\n\n"
			<< "namespace {\n\n";
		for (int index = 0; index < AssetCount; ++index)
			writeByteArray(output, Assets[index], contents[index]);
		output << "} // namespace\n\n"
			<< "EmbeddedGameAssets getEmbeddedGameAssets()\n"
			<< "{\n"
			<< "\treturn {\n"
			<< "\t\t{\"" << Assets[0].fileName << "\", \"" << Assets[0].logicalName
			<< "\", " << Assets[0].symbol << ", sizeof(" << Assets[0].symbol << ")},\n"
			<< "\t\t{{\n";
		for (int index = 1; index < AssetCount; ++index)
		{
			output << "\t\t\t{\"" << Assets[index].fileName << "\", \""
				<< Assets[index].logicalName << "\", " << Assets[index].symbol
				<< ", sizeof(" << Assets[index].symbol << ")}";
			output << (index + 1 == AssetCount ? "\n" : ",\n");
		}
		output << "\t\t}}\n\t};\n}\n";
		output.close();
		if (!output)
			throw std::runtime_error("Unable to finish generated asset source: " + temporaryPath);

		std::remove(outputPath.c_str());
		if (std::rename(temporaryPath.c_str(), outputPath.c_str()) != 0)
			throw std::runtime_error("Unable to install generated asset source: " + outputPath);
	}
	catch (const std::exception& error)
	{
		std::remove(temporaryPath.c_str());
		std::cerr << "Asset embedding failed: " << error.what() << std::endl;
		return 1;
	}

	return 0;
}
