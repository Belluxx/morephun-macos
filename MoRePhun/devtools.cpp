#include "devtools.h"
#include "mophun_vm.h"
#include "binary_io.h"
#include "syscall/input_facilities.h"

#include <cstdlib>
#include <iostream>
#include <sstream>

namespace {

uint32_t parseKeys(const std::string& letters)
{
	uint32_t keys = 0;
	for (char letter : letters)
	{
		switch (letter)
		{
			case 'U': keys |= KEY_UP; break;
			case 'D': keys |= KEY_DOWN; break;
			case 'L': keys |= KEY_LEFT; break;
			case 'R': keys |= KEY_RIGHT; break;
			case 'F': keys |= KEY_FIRE; break;
			case 'S': keys |= KEY_SELECT; break;
			case '2': keys |= KEY_FIRE2; break;
			default: break;
		}
	}
	return keys;
}

} // namespace

DevTools::DevTools()
{
	const char* inputScript = std::getenv("MOPHUN_INPUT_SCRIPT");
	if (inputScript != nullptr)
	{
		std::istringstream entries(inputScript);
		std::string entry;
		while (std::getline(entries, entry, ';'))
		{
			const size_t colon = entry.find(':');
			const size_t dash = entry.find('-');
			if (colon == std::string::npos)
				continue;
			ScriptEntry parsed{};
			parsed.firstFrame = static_cast<uint32_t>(std::strtoul(entry.c_str(), nullptr, 10));
			parsed.lastFrame = dash != std::string::npos && dash < colon
				? static_cast<uint32_t>(std::strtoul(entry.c_str() + dash + 1, nullptr, 10))
				: parsed.firstFrame;
			parsed.keys = parseKeys(entry.substr(colon + 1));
			script.push_back(parsed);
		}
		active = true;
	}

	const char* pokeScript = std::getenv("MOPHUN_POKE");
	if (pokeScript != nullptr)
	{
		std::istringstream entries(pokeScript);
		std::string entry;
		while (std::getline(entries, entry, ';'))
		{
			const size_t colon = entry.find(':');
			if (colon == std::string::npos)
				continue;
			const size_t dash = entry.find('-');
			const uint32_t first = static_cast<uint32_t>(std::strtoul(entry.c_str(), nullptr, 10));
			const uint32_t last = dash != std::string::npos && dash < colon
				? static_cast<uint32_t>(std::strtoul(entry.c_str() + dash + 1, nullptr, 10)) : first;
			std::istringstream writes(entry.substr(colon + 1));
			std::string write;
			while (std::getline(writes, write, ','))
			{
				const size_t equals = write.find('=');
				if (equals == std::string::npos)
					continue;
				PokeEntry poke{first, last,
					static_cast<uint32_t>(std::strtoul(write.c_str(), nullptr, 0)),
					static_cast<uint32_t>(std::strtol(write.c_str() + equals + 1, nullptr, 0))};
				pokes.push_back(poke);
			}
		}
		active = true;
	}

	const char* screenshotDir = std::getenv("MOPHUN_SCREENSHOT_DIR");
	if (screenshotDir != nullptr)
	{
		screenshotDirectory = screenshotDir;
		const char* every = std::getenv("MOPHUN_SCREENSHOT_EVERY");
		if (every != nullptr)
			screenshotEvery = std::max(1U, static_cast<uint32_t>(std::strtoul(every, nullptr, 10)));
		active = true;
	}

	const char* dumpPath = std::getenv("MOPHUN_RAM_DUMP");
	if (dumpPath != nullptr)
	{
		ramDump = std::fopen(dumpPath, "wb");
		if (ramDump == nullptr)
			std::cerr << "Unable to open RAM dump file: " << dumpPath << std::endl;
		active = true;
	}

	frameStats = std::getenv("MOPHUN_FRAME_STATS") != nullptr;
	active = active || frameStats;
}

DevTools::~DevTools()
{
	if (ramDump != nullptr)
		std::fclose(ramDump);
}

void DevTools::onFlip(MophunVM& vm, uint64_t executedInstructions, uint32_t virtualTicks)
{
	if (frameStats)
	{
		std::cout << "frame " << frame << " instructions=" << (executedInstructions - instructionsAtLastFlip)
			<< " tickCalls=" << tickCallsThisFrame << " ticks=" << virtualTicks << std::endl;
	}
	if (ramDump != nullptr)
	{
		const Memory& layout = vm.getMemoryLayout();
		const uint32_t start = layout.dataSegStartAddr;
		const uint32_t size = layout.resSegStartAddr - start;
		if (frame == 0)
		{
			// Header: magic, start address, size per frame.
			const uint32_t header[3] = {0x4d554452U, start, size};
			std::fwrite(header, sizeof(header), 1, ramDump);
		}
		std::fwrite(vm.getRamAddress(start), 1, size, ramDump);
	}
	instructionsAtLastFlip = executedInstructions;
	tickCallsThisFrame = 0;
	++frame;
}

uint32_t DevTools::scriptedKeys() const
{
	uint32_t keys = 0;
	for (const ScriptEntry& entry : script)
	{
		if (frame >= entry.firstFrame && frame <= entry.lastFrame)
			keys |= entry.keys;
	}
	return keys;
}

void DevTools::applyPokes(MophunVM& vm) const
{
	for (const PokeEntry& poke : pokes)
	{
		if (frame >= poke.firstFrame && frame <= poke.lastFrame)
			writeLittleU32(vm.getRamAddress(poke.address), poke.value);
	}
}

std::string DevTools::screenshotPathForFrame() const
{
	if (screenshotDirectory.empty() || frame % screenshotEvery != 0)
		return std::string();
	char name[64];
	std::snprintf(name, sizeof(name), "/frame_%06u.bmp", frame);
	return screenshotDirectory + name;
}
