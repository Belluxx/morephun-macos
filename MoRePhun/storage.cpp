#include "storage.h"

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <dirent.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {

std::string directoryName(const std::string& path)
{
	const std::string::size_type separator = path.find_last_of("/\\");
	if (separator == std::string::npos)
		return ".";
	if (separator == 0)
		return path.substr(0, 1);
	return path.substr(0, separator);
}

std::string joinPath(const std::string& directory, const std::string& name)
{
	if (directory.empty() || directory == ".")
		return directory.empty() ? name : directory + "/" + name;
	const char last = directory[directory.size() - 1];
	return last == '/' || last == '\\' ? directory + name : directory + "/" + name;
}

std::string lowerCase(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
		return static_cast<char>(std::tolower(character));
	});
	return value;
}

bool hasMpcExtension(const std::string& name)
{
	return name.size() > 4 && lowerCase(name.substr(name.size() - 4)) == ".mpc";
}

} // namespace

Storage::Storage(const std::string& saveDirectoryOverride)
{
	const char* environmentOverride = std::getenv("MOPHUN_SAVE_DIR");
	if (!saveDirectoryOverride.empty())
		saveDirectory = saveDirectoryOverride;
	else if (environmentOverride != nullptr && environmentOverride[0] != '\0')
		saveDirectory = environmentOverride;
	else
	{
		char* preferencePath = SDL_GetPrefPath("MoRePhun", "V-Rally 2");
		if (preferencePath == nullptr)
			throw std::runtime_error(std::string("Unable to create the save directory: ") + SDL_GetError());
		saveDirectory = preferencePath;
		SDL_free(preferencePath);
	}

	if (saveDirectory.empty())
		throw std::runtime_error("The save directory is empty");
}

void Storage::mountPacksNextTo(const std::string& romPath)
{
	mountedPacks.clear();
	const std::string directory = directoryName(romPath);
	DIR* handle = opendir(directory.c_str());
	if (handle == nullptr)
	{
		std::cerr << "Unable to inspect MPC directory: " << directory << std::endl;
		return;
	}

	std::vector<std::string> fileNames;
	while (dirent* entry = readdir(handle))
	{
		const std::string fileName = entry->d_name;
		if (hasMpcExtension(fileName))
			fileNames.push_back(fileName);
	}
	closedir(handle);
	std::sort(fileNames.begin(), fileNames.end());

	for (const std::string& fileName : fileNames)
	{
		const std::string hostPath = joinPath(directory, fileName);
		const std::string stem = fileName.substr(0, fileName.size() - 4);
		const std::string::size_type separator = stem.find_last_of('_');
		const std::string logicalName = separator == std::string::npos
			? stem : stem.substr(separator + 1);

		// Installed Mophun certificates are addressed by their logical name, not
		// by the filename used to deliver the .mpc to a phone. Keep a few useful
		// aliases so both original phone dumps and plainly named packs work.
		mountedPacks.emplace(lowerCase(fileName), hostPath);
		mountedPacks.emplace(lowerCase(stem), hostPath);
		mountedPacks.emplace(lowerCase(logicalName), hostPath);
		mountedPacks.emplace(lowerCase(logicalName + ".mpc"), hostPath);
		std::cout << "Mounted MPC pack: " << logicalName << " (" << hostPath << ")" << std::endl;
	}
}

bool Storage::resolveReadPath(const std::string& guestName, std::string& hostPath,
	bool& mountedPack) const
{
	const auto pack = mountedPacks.find(lowerCase(guestName));
	if (pack != mountedPacks.end())
	{
		hostPath = pack->second;
		mountedPack = true;
		return true;
	}

	hostPath = joinPath(saveDirectory, encodeGuestName(guestName));
	mountedPack = false;
	return !guestName.empty();
}

bool Storage::resolveWritePath(const std::string& guestName, std::string& hostPath) const
{
	hostPath = joinPath(saveDirectory, encodeGuestName(guestName));
	return !guestName.empty();
}

const std::string& Storage::getSaveDirectory() const
{
	return saveDirectory;
}

std::string Storage::encodeGuestName(const std::string& guestName)
{
	std::ostringstream encoded;
	encoded << std::uppercase << std::hex;
	for (unsigned char character : guestName)
	{
		if (std::isalnum(character) || character == '-' || character == '_' || character == '.')
			encoded << static_cast<char>(character);
		else
			encoded << '%' << std::setw(2) << std::setfill('0') << static_cast<unsigned>(character);
	}
	const std::string result = encoded.str();
	if (result == ".")
		return "%2E";
	if (result == "..")
		return "%2E%2E";
	return result;
}
