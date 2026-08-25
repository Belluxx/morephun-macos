#pragma once

#include <string>
#include <unordered_map>


class Storage {

	public:
		explicit Storage(const std::string& saveDirectoryOverride = std::string());
		void mountPacksNextTo(const std::string& romPath);
		bool resolveReadPath(const std::string& guestName, std::string& hostPath,
			bool& mountedPack) const;
		bool resolveWritePath(const std::string& guestName, std::string& hostPath) const;
		const std::string& getSaveDirectory() const;

	private:
		std::string saveDirectory;
		std::unordered_map<std::string, std::string> mountedPacks;
		static std::string encodeGuestName(const std::string& guestName);
};
