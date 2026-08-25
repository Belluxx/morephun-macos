#define SDL_MAIN_HANDLED
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <string>
#include "mophun_os.h"


int main(int argc, char* argv[])
{
	if (argc < 2 || argc > 3)
	{
		std::cerr << "Usage: " << argv[0] << " <rom.mpn> [max-instructions]" << std::endl;
		return 2;
	}

	uint64_t maxInstructions = 0;
	if (argc == 3)
	{
		try
		{
			size_t parsedCharacters = 0;
			maxInstructions = std::stoull(argv[2], &parsedCharacters);
			if (parsedCharacters != std::string(argv[2]).size())
				throw std::invalid_argument("trailing characters");
		}
		catch (const std::exception&)
		{
			std::cerr << "Invalid instruction limit: " << argv[2] << std::endl;
			return 2;
		}
	}

	try
	{
		MophunOS mophunOS;
		if (!mophunOS.loadRom(argv[1]))
			return 1;

		mophunOS.emulate(maxInstructions);
	}
	catch (const std::exception& error)
	{
		std::cerr << "Fatal error: " << error.what() << std::endl;
		return 1;
	}

    return 0;
}
