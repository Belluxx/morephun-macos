#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include "graphics.h"
#include "stream_io.h"

struct OSData {
	uint8_t currentFgColor[3] = {255, 255, 255};
	uint8_t currentBgColor[3] = {0, 0, 0};
	std::array<uint16_t, 256> palette{};
	uint32_t currentTransferMode = 1;
	uint32_t systemFontWidth = 6;
	uint32_t systemFontHeight = 8;
	MAPHEADER mapHeader{};
	bool mapInitialized = false;
	uint32_t mapFrame = 0;
	int64_t timer;
	std::vector<SpriteSlot> spriteSlots;
	VMGPFONT* currentFont = nullptr;
	VMGPFONT* previousFont = nullptr;
	std::unordered_map<uint32_t, StreamSlot> streamSlots;
	uint32_t streamCounter;
};
