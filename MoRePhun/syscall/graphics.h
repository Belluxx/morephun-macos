#pragma once
#include <cstdint>
#include "../video.h"

#define MODE_BLOCK 0
#define MODE_TRANS 1
#define MODE_FLIPX 2
#define MODE_FLIPY 4
#define MODE_ROT90 2
#define MODE_ROT270 4

inline Uint32 decodePixelFormat(uint8_t format) { 
	switch (format)
	{
	case 0x7:
		return SDL_PIXELFORMAT_RGB332;
	default:
		std::cout << "Unknown sprite pixel format: " << format << std::endl;
		return SDL_PIXELFORMAT_UNKNOWN;

	}
}

#pragma pack(push, 1)
struct VMGPFONT {
	uint32_t fontdata;
	uint32_t chartbl;
	uint8_t bpp;
	uint8_t width;
	uint8_t height;
	uint8_t palindex;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct SPRITE {
	uint8_t palindex;
	uint8_t format;
	int16_t centerx;  
	int16_t centery;
	uint16_t width;
	uint16_t height;
};
#pragma pack(pop)
static_assert(sizeof(SPRITE) == 10, "Invalid Mophun sprite header size");

#pragma pack(push, 1)
struct MAPHEADER {
	uint8_t flag;
	uint8_t format;
	uint8_t mapWidth;
	uint8_t mapHeight;
	uint8_t animationSpeed;
	uint8_t animationCount;
	uint8_t animationActive;
	uint8_t pad;
	int16_t xPan;
	int16_t yPan;
	int16_t xPos;
	int16_t yPos;
	uint32_t mapData;
	uint32_t tileSpriteData;
};
#pragma pack(pop)
static_assert(sizeof(MAPHEADER) == 24, "Invalid Mophun map header size");

struct SpriteSlot {
	SPRITE* spriteData = nullptr;
	SDL_Texture* spriteTexture = nullptr;
	int32_t x = 0;
	int32_t y = 0;
};
