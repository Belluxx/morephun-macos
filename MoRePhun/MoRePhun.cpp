#define SDL_MAIN_HANDLED
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <string>
#ifdef MOPHUN_EMBEDDED_ASSETS
#include "embedded_assets.h"
#endif
#ifdef MOPHUN_STANDALONE
#include <sys/stat.h>
#endif
#include "mophun_os.h"

#ifdef MOPHUN_STANDALONE
namespace {

const char* DefaultVRally2File = "VRally2_[RC14EU]_[multiscreen]_M5.mpn";

bool isDirectory(const std::string& path)
{
	struct stat status;
	return stat(path.c_str(), &status) == 0 && S_ISDIR(status.st_mode);
}

std::string gameFileFromArgument(const std::string& path)
{
	if (!isDirectory(path))
		return path;
	const char lastCharacter = path.empty() ? '\0' : path[path.size() - 1];
	return path + (lastCharacter == '/' || lastCharacter == '\\' ? "" : "/")
		+ DefaultVRally2File;
}

bool isUnsignedInteger(const std::string& value)
{
	if (value.empty())
		return false;
	for (char character : value)
	{
		if (character < '0' || character > '9')
			return false;
	}
	return true;
}

void printStandaloneUsage(const char* executable, std::ostream& stream)
{
	stream << "Usage: " << executable
		<< " [game-directory-or-file] [max-instructions]" << std::endl;
}

} // namespace
#endif

int main(int argc, char* argv[])
{
#ifdef MOPHUN_STANDALONE
	if (argc > 3 || (argc >= 2 && std::string(argv[1]) == "--help"))
	{
		printStandaloneUsage(argv[0], argc > 3 ? std::cerr : std::cout);
		return argc > 3 ? 2 : 0;
	}
#else
	if (argc < 2 || argc > 3)
	{
		std::cerr << "Usage: " << argv[0] << " <rom.mpn> [max-instructions]" << std::endl;
		return 2;
	}
#endif

	uint64_t maxInstructions = 0;
#ifdef MOPHUN_STANDALONE
	const bool oneArgumentIsInstructionLimit = argc == 2 && isUnsignedInteger(argv[1]);
	const int gameArgument = argc >= 2 && !oneArgumentIsInstructionLimit ? 1 : 0;
	const int instructionArgument = argc == 3 ? 2 : (oneArgumentIsInstructionLimit ? 1 : 0);
#else
	const int instructionArgument = argc == 3 ? 2 : 0;
#endif
	if (instructionArgument != 0)
	{
		try
		{
			size_t parsedCharacters = 0;
			maxInstructions = std::stoull(argv[instructionArgument], &parsedCharacters);
			if (parsedCharacters != std::string(argv[instructionArgument]).size())
				throw std::invalid_argument("trailing characters");
		}
		catch (const std::exception&)
		{
			std::cerr << "Invalid instruction limit: " << argv[instructionArgument] << std::endl;
			return 2;
		}
	}

#if defined(MOPHUN_STANDALONE) && !defined(MOPHUN_EMBEDDED_ASSETS)
	if (gameArgument == 0)
	{
		std::cerr << "No game data is embedded; provide the extracted game directory or MPN file."
			<< std::endl;
		printStandaloneUsage(argv[0], std::cerr);
		return 2;
	}
#endif

	try
	{
		MophunOS mophunOS;
#ifdef MOPHUN_STANDALONE
		if (gameArgument != 0)
		{
			if (!mophunOS.loadRom(gameFileFromArgument(argv[gameArgument])))
				return 1;
		}
#ifdef MOPHUN_EMBEDDED_ASSETS
		else
		{
			const EmbeddedGameAssets assets = getEmbeddedGameAssets();
			if (!mophunOS.loadEmbeddedRom(assets.rom.data, assets.rom.size))
				return 1;
			for (const EmbeddedAsset& pack : assets.packs)
				mophunOS.mountEmbeddedPack(pack.fileName, pack.logicalName, pack.data, pack.size);
		}
#endif
#else
		if (!mophunOS.loadRom(argv[1]))
			return 1;
#endif

		mophunOS.emulate(maxInstructions);
	}
	catch (const std::exception& error)
	{
		std::cerr << "Fatal error: " << error.what() << std::endl;
		return 1;
	}

	return 0;
}
