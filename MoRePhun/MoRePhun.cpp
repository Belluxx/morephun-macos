#define SDL_MAIN_HANDLED
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <string>
#ifdef MOPHUN_STANDALONE
#include "embedded_assets.h"
#endif
#include "mophun_os.h"


int main(int argc, char* argv[])
{
#ifdef MOPHUN_STANDALONE
	if (argc > 2 || (argc == 2 && std::string(argv[1]) == "--help"))
	{
		std::cout << "Usage: " << argv[0] << " [max-instructions]" << std::endl;
		return argc > 2 ? 2 : 0;
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
	const int instructionArgument = argc == 2 ? 1 : 0;
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

	try
	{
		MophunOS mophunOS;
#ifdef MOPHUN_STANDALONE
		const EmbeddedGameAssets assets = getEmbeddedGameAssets();
		if (!mophunOS.loadEmbeddedRom(assets.rom.data, assets.rom.size))
			return 1;
		for (const EmbeddedAsset& pack : assets.packs)
			mophunOS.mountEmbeddedPack(pack.fileName, pack.logicalName, pack.data, pack.size);
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
