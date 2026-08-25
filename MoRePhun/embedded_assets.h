#pragma once

#include <array>
#include <cstddef>
#include <cstdint>


struct EmbeddedAsset {
	const char* fileName;
	const char* logicalName;
	const uint8_t* data;
	size_t size;
};

struct EmbeddedGameAssets {
	EmbeddedAsset rom;
	std::array<EmbeddedAsset, 5> packs;
};

EmbeddedGameAssets getEmbeddedGameAssets();
