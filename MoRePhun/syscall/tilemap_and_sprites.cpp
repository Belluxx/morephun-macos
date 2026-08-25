#include "../mophun_os.h"
#include "../registers.h"
#include "graphics.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

uint8_t bitsPerPixel(uint8_t format)
{
	switch (format)
	{
		case 3: return 1;
		case 4: return 2;
		case 5: return 4;
		case 6:
		case 7: return 8;
		default: return 0;
	}
}

void colorFrom555(uint16_t color, uint8_t& red, uint8_t& green, uint8_t& blue)
{
	red = static_cast<uint8_t>(((color >> 10) & 0x1f) * 255 / 31);
	green = static_cast<uint8_t>(((color >> 5) & 0x1f) * 255 / 31);
	blue = static_cast<uint8_t>((color & 0x1f) * 255 / 31);
}

SDL_Texture* createTileTexture(Video* video, const OSData& osdata, const uint8_t* source,
	uint8_t format, bool transparent)
{
	const uint8_t bits = bitsPerPixel(format);
	if (bits == 0)
		return nullptr;
	SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, 8, 8, 32, SDL_PIXELFORMAT_RGBA32);
	if (surface == nullptr)
		return nullptr;
	auto* pixels = static_cast<uint32_t*>(surface->pixels);
	const uint32_t mask = (1U << bits) - 1U;
	for (uint32_t pixel = 0; pixel < 64; ++pixel)
	{
		const uint32_t value = (source[(pixel * bits) >> 3] >>
			((pixel & ((8U / bits) - 1U)) * bits)) & mask;
		uint8_t red = 0;
		uint8_t green = 0;
		uint8_t blue = 0;
		if (format == 7)
		{
			red = static_cast<uint8_t>(((value >> 5) & 7U) * 255 / 7);
			green = static_cast<uint8_t>(((value >> 2) & 7U) * 255 / 7);
			blue = static_cast<uint8_t>((value & 3U) * 255 / 3);
		}
		else
		{
			colorFrom555(osdata.palette[value], red, green, blue);
		}
		pixels[pixel] = SDL_MapRGBA(surface->format, red, green, blue,
			transparent && value == 0 ? 0 : 255);
	}
	SDL_Texture* texture = SDL_CreateTextureFromSurface(video->app.renderer, surface);
	SDL_FreeSurface(surface);
	if (texture != nullptr)
		SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
	return texture;
}

} // namespace

void MophunOS::vMapInit()
{
	std::memcpy(&osdata.mapHeader, mophunVM->getRamAddress(mophunVM->readReg(p0)),
		sizeof(osdata.mapHeader));
	osdata.mapInitialized = bitsPerPixel(osdata.mapHeader.format) != 0;
	osdata.mapFrame = 0;
	mophunVM->writeReg(r0, osdata.mapInitialized ? 1 : 0);
}

void MophunOS::vUpdateMap()
{
	if (!osdata.mapInitialized || osdata.mapHeader.mapWidth == 0 || osdata.mapHeader.mapHeight == 0)
		return;

	const MAPHEADER& map = osdata.mapHeader;
	const bool hasAttributes = map.flag != 0;
	const uint32_t mapStride = hasAttributes ? 2 : 1;
	const uint8_t bits = bitsPerPixel(map.format);
	const uint32_t tileBytes = (64U * bits + 7U) / 8U;
	const uint8_t* mapData = mophunVM->getRamAddress(map.mapData);
	const uint8_t* tiles = mophunVM->getRamAddress(map.tileSpriteData);

	for (uint32_t tileY = 0; tileY < map.mapHeight; ++tileY)
	{
		for (uint32_t tileX = 0; tileX < map.mapWidth; ++tileX)
		{
			const int screenX = map.xPan + static_cast<int>(tileX) * 8 - map.xPos;
			const int screenY = map.yPan + static_cast<int>(tileY) * 8 - map.yPos;
			if (screenX <= -8 || screenY <= -8 || screenX >= SCREEN_WIDTH || screenY >= SCREEN_HEIGHT)
				continue;
			const uint32_t mapIndex = (tileY * map.mapWidth + tileX) * mapStride;
			uint8_t tileNumber = mapData[mapIndex];
			if (tileNumber == 0)
				continue;
			const uint8_t attributes = hasAttributes ? mapData[mapIndex + 1] : 0;
			if ((attributes & 0x40) != 0)
				tileNumber = static_cast<uint8_t>(tileNumber + ((osdata.mapFrame / std::max<uint8_t>(1, map.animationSpeed)) & 1));
			else if ((attributes & 0x80) != 0)
				tileNumber = static_cast<uint8_t>(tileNumber + ((osdata.mapFrame / std::max<uint8_t>(1, map.animationSpeed)) & 3));
			SDL_Texture* texture = createTileTexture(video, osdata,
				tiles + static_cast<uint32_t>(tileNumber - 1) * tileBytes, map.format,
				(map.flag & 1) != 0 && (attributes & 1) != 0);
			if (texture != nullptr)
			{
				SDL_Rect destination = {screenX, screenY, 8, 8};
				SDL_RenderCopy(video->app.renderer, texture, nullptr, &destination);
				SDL_DestroyTexture(texture);
			}
		}
	}
	++osdata.mapFrame;
}


void MophunOS::vSpriteCollision()
{
	uint8_t slot = mophunVM->readReg(p0);
	uint8_t slotFrom = mophunVM->readReg(p1);
	uint8_t slotTo = mophunVM->readReg(p2);
	for (uint8_t i = slotFrom; i <= slotTo; i++)
	{
		
		if (!(osdata.spriteSlots[i].x > (osdata.spriteSlots[slot].x + osdata.spriteSlots[slot].spriteData->width) ||
			(osdata.spriteSlots[i].x + osdata.spriteSlots[i].spriteData->width) < osdata.spriteSlots[slot].x ||
			osdata.spriteSlots[i].y > (osdata.spriteSlots[slot].y + osdata.spriteSlots[slot].spriteData->height) ||
			(osdata.spriteSlots[i].y + osdata.spriteSlots[i].spriteData->height) < osdata.spriteSlots[slot].y))
		{
			mophunVM->writeReg(r0, i);
			return;
		}
	}
	mophunVM->writeReg(r0, -1);
}
