#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>


struct StorageReadSource {
	std::string path;
	const uint8_t* data = nullptr;
	size_t size = 0;
	bool mountedPack = false;
};


class Storage {

	public:
		explicit Storage(const std::string& saveDirectoryOverride = std::string());
		void mountPacksNextTo(const std::string& romPath);
		void mountEmbeddedPack(const std::string& fileName, const std::string& logicalName,
			const uint8_t* data, size_t size);
		bool resolveReadSource(const std::string& guestName, StorageReadSource& source) const;
		bool resolveReadPath(const std::string& guestName, std::string& hostPath,
			bool& mountedPack) const;
		bool resolveWritePath(const std::string& guestName, std::string& hostPath) const;
		const std::string& getSaveDirectory() const;

	private:
		struct MountedPack {
			std::string path;
			const uint8_t* data = nullptr;
			size_t size = 0;
		};

		std::string saveDirectory;
		std::unordered_map<std::string, MountedPack> mountedPacks;
		void addPackAliases(const std::string& fileName, const std::string& logicalName,
			const MountedPack& pack);
		static std::string encodeGuestName(const std::string& guestName);
};
