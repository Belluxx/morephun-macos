#include "../mophun_os.h"
#include "../registers.h"
#include <bitset>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

void rgb555(uint16_t color, uint8_t& red, uint8_t& green, uint8_t& blue)
{
	red = static_cast<uint8_t>(((color >> 10) & 0x1f) * 255 / 31);
	green = static_cast<uint8_t>(((color >> 5) & 0x1f) * 255 / 31);
	blue = static_cast<uint8_t>((color & 0x1f) * 255 / 31);
}

uint8_t spriteBitsPerPixel(uint8_t format)
{
	switch (format)
	{
		case 0:
		case 3: return 1;
		case 1:
		case 4: return 2;
		case 2:
		case 5: return 4;
		case 6:
		case 7: return 8;
		default: return 0;
	}
}

SDL_Texture* createSpriteTexture(Video* video, const OSData& osdata, const SPRITE* sprite,
	bool transparent)
{
	const uint8_t bits = spriteBitsPerPixel(sprite->format);
	if (bits == 0 || sprite->width == 0 || sprite->height == 0)
		return nullptr;

	SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, sprite->width, sprite->height,
		32, SDL_PIXELFORMAT_RGBA32);
	if (surface == nullptr)
		return nullptr;

	const uint8_t* const source = reinterpret_cast<const uint8_t*>(sprite) + sizeof(SPRITE);
	auto* pixels = static_cast<uint32_t*>(surface->pixels);
	const uint32_t pixelCount = static_cast<uint32_t>(sprite->width) * sprite->height;
	const uint32_t mask = (1U << bits) - 1U;
	for (uint32_t pixel = 0; pixel < pixelCount; ++pixel)
	{
		const uint8_t raw = source[(pixel * bits) >> 3];
		const uint32_t value = (raw >> ((pixel & ((8U / bits) - 1U)) * bits)) & mask;
		uint8_t red = 0;
		uint8_t green = 0;
		uint8_t blue = 0;
		if (sprite->format == 7)
		{
			red = static_cast<uint8_t>(((value >> 5) & 7U) * 255 / 7);
			green = static_cast<uint8_t>(((value >> 2) & 7U) * 255 / 7);
			blue = static_cast<uint8_t>((value & 3U) * 255 / 3);
		}
		else if (sprite->format <= 2)
		{
			red = green = blue = static_cast<uint8_t>(value * 255 / mask);
		}
		else
		{
			const uint32_t paletteIndex = sprite->format == 6 ? value : sprite->palindex + value;
			rgb555(osdata.palette[paletteIndex & 0xffU], red, green, blue);
		}
		const uint8_t alpha = transparent && value == 0 ? 0 : 255;
		pixels[pixel] = SDL_MapRGBA(surface->format, red, green, blue, alpha);
	}

	SDL_Texture* const texture = SDL_CreateTextureFromSurface(video->app.renderer, surface);
	SDL_FreeSurface(surface);
	if (texture != nullptr)
		SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
	return texture;
}

void setForeground(SDL_Renderer* renderer, const OSData& osdata)
{
	SDL_SetRenderDrawColor(renderer, osdata.currentFgColor[0], osdata.currentFgColor[1],
		osdata.currentFgColor[2], 255);
}

const uint8_t* glyphRows(char character)
{
	static const uint8_t blank[7] = {0, 0, 0, 0, 0, 0, 0};
	static const uint8_t unknown[7] = {14, 17, 1, 2, 4, 0, 4};
	static const uint8_t letters[26][7] = {
		{14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14},
		{30,17,17,17,17,17,30}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
		{14,17,16,23,17,17,14}, {17,17,17,31,17,17,17}, {14,4,4,4,4,4,14},
		{7,2,2,2,18,18,12}, {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
		{17,27,21,21,17,17,17}, {17,25,21,19,17,17,17}, {14,17,17,17,17,17,14},
		{30,17,17,30,16,16,16}, {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
		{15,16,16,14,1,1,30}, {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14},
		{17,17,17,17,17,10,4}, {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
		{17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}
	};
	static const uint8_t numbers[10][7] = {
		{14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31},
		{30,1,1,14,1,1,30}, {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
		{14,16,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14},
		{14,17,17,15,1,1,14}
	};
	static const uint8_t period[7] = {0,0,0,0,0,12,12};
	static const uint8_t comma[7] = {0,0,0,0,0,12,8};
	static const uint8_t colon[7] = {0,12,12,0,12,12,0};
	static const uint8_t dash[7] = {0,0,0,31,0,0,0};
	static const uint8_t slash[7] = {1,2,2,4,8,8,16};
	static const uint8_t exclamation[7] = {4,4,4,4,4,0,4};
	if (character >= 'a' && character <= 'z')
		character = static_cast<char>(character - 'a' + 'A');
	if (character >= 'A' && character <= 'Z') return letters[character - 'A'];
	if (character >= '0' && character <= '9') return numbers[character - '0'];
	switch (character)
	{
		case ' ': return blank;
		case '.': return period;
		case ',': return comma;
		case ':': return colon;
		case '-': return dash;
		case '/': return slash;
		case '!': return exclamation;
		default: return unknown;
	}
}

void drawSystemCharacter(SDL_Renderer* renderer, int x, int y, char character)
{
	const uint8_t* rows = glyphRows(character);
	for (int row = 0; row < 7; ++row)
		for (int column = 0; column < 5; ++column)
			if ((rows[row] & (1U << (4 - column))) != 0)
				SDL_RenderDrawPoint(renderer, x + column, y + row);
}

} // namespace

void MophunOS::vClearScreen()
{
	int32_t color = static_cast<int32_t>(mophunVM->readReg(p0));
	uint8_t rgb[3];
	if (color & (1 << 31))
	{
		rgb[0] = (color & 0x7C00) >> 7;
		rgb[1] = (color & 0x3E0) >> 2;
		rgb[2] = (color & 0x1f) << 3;
	}
	else
	{
		rgb[0] = ((color >> 5) & 0x7) << 5;
		rgb[1] = ((color >> 2) & 0x7) << 5;
		rgb[2] = ((color >> 0) & 0x3) << 6;
	}
	SDL_SetRenderDrawColor(video->app.renderer, rgb[0], rgb[1], rgb[2], 255);
	SDL_RenderClear(video->app.renderer);
}


void MophunOS::vFlipScreen()
{
	const char* screenshotPath = std::getenv("MOPHUN_SCREENSHOT");
	if (screenshotPath != nullptr)
	{
		int outputWidth = 0;
		int outputHeight = 0;
		SDL_GetRendererOutputSize(video->app.renderer, &outputWidth, &outputHeight);
		SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, outputWidth, outputHeight,
			32, SDL_PIXELFORMAT_RGBA32);
		if (surface != nullptr)
		{
			SDL_Rect frame = {0, 0, outputWidth, outputHeight};
			if (SDL_RenderReadPixels(video->app.renderer, &frame, surface->format->format,
				surface->pixels, surface->pitch) == 0)
				SDL_SaveBMP(surface, screenshotPath);
			SDL_FreeSurface(surface);
		}
	}
	SDL_RenderPresent(video->app.renderer);
}

void MophunOS::vSetPaletteEntry()
{
	osdata.palette[mophunVM->readReg(p0) & 0xffU] = static_cast<uint16_t>(mophunVM->readReg(p1));
}

void MophunOS::vGetPaletteEntry()
{
	mophunVM->writeReg(r0, osdata.palette[mophunVM->readReg(p0) & 0xffU]);
}

void MophunOS::vSetClipWindow()
{
	const int x0 = static_cast<int16_t>(mophunVM->readReg(p0));
	const int y0 = static_cast<int16_t>(mophunVM->readReg(p1));
	const int x1 = static_cast<int16_t>(mophunVM->readReg(p2));
	const int y1 = static_cast<int16_t>(mophunVM->readReg(p3));
	SDL_Rect clip = {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
	SDL_RenderSetClipRect(video->app.renderer, &clip);
}

void MophunOS::vSetTransferMode()
{
	const uint32_t previous = osdata.currentTransferMode;
	osdata.currentTransferMode = mophunVM->readReg(p0);
	mophunVM->writeReg(r0, previous);
}

void MophunOS::vFillRect()
{
	const int x0 = static_cast<int16_t>(mophunVM->readReg(p0));
	const int y0 = static_cast<int16_t>(mophunVM->readReg(p1));
	const int x1 = static_cast<int16_t>(mophunVM->readReg(p2));
	const int y1 = static_cast<int16_t>(mophunVM->readReg(p3));
	if (x1 <= x0 || y1 <= y0)
		return;
	SDL_Rect rectangle = {x0, y0, x1 - x0, y1 - y0};
	setForeground(video->app.renderer, osdata);
	SDL_RenderFillRect(video->app.renderer, &rectangle);
}

void MophunOS::vDrawLine()
{
	setForeground(video->app.renderer, osdata);
	SDL_RenderDrawLine(video->app.renderer,
		static_cast<int16_t>(mophunVM->readReg(p0)), static_cast<int16_t>(mophunVM->readReg(p1)),
		static_cast<int16_t>(mophunVM->readReg(p2)), static_cast<int16_t>(mophunVM->readReg(p3)));
}

void MophunOS::vDrawFlatPolygon()
{
	const uint8_t* points = mophunVM->getRamAddress(mophunVM->readReg(p0));
	int16_t coordinates[6];
	std::memcpy(coordinates, points, sizeof(coordinates));
	setForeground(video->app.renderer, osdata);
#if SDL_VERSION_ATLEAST(2, 0, 18)
	uint8_t red, green, blue, alpha;
	SDL_GetRenderDrawColor(video->app.renderer, &red, &green, &blue, &alpha);
	const SDL_Color color = {red, green, blue, alpha};
	SDL_Vertex vertices[3] = {
		{{static_cast<float>(coordinates[0]), static_cast<float>(coordinates[1])}, color, {0, 0}},
		{{static_cast<float>(coordinates[2]), static_cast<float>(coordinates[3])}, color, {0, 0}},
		{{static_cast<float>(coordinates[4]), static_cast<float>(coordinates[5])}, color, {0, 0}}
	};
	SDL_RenderGeometry(video->app.renderer, nullptr, vertices, 3, nullptr, 0);
#else
	SDL_RenderDrawLine(video->app.renderer, coordinates[0], coordinates[1], coordinates[2], coordinates[3]);
	SDL_RenderDrawLine(video->app.renderer, coordinates[2], coordinates[3], coordinates[4], coordinates[5]);
	SDL_RenderDrawLine(video->app.renderer, coordinates[4], coordinates[5], coordinates[0], coordinates[1]);
#endif
}

void MophunOS::vDrawObject()
{
	const int x = static_cast<int16_t>(mophunVM->readReg(p0));
	const int y = static_cast<int16_t>(mophunVM->readReg(p1));
	const auto* sprite = reinterpret_cast<const SPRITE*>(mophunVM->getRamAddress(mophunVM->readReg(p2)));
	SDL_Texture* const texture = createSpriteTexture(video, osdata, sprite,
		(osdata.currentTransferMode & MODE_TRANS) != 0);
	if (texture == nullptr)
		return;
	SDL_Rect destination = {x - sprite->centerx, y - sprite->centery, sprite->width, sprite->height};
	const SDL_RendererFlip flip = static_cast<SDL_RendererFlip>(
		((osdata.currentTransferMode & MODE_FLIPX) ? SDL_FLIP_HORIZONTAL : 0) |
		((osdata.currentTransferMode & MODE_FLIPY) ? SDL_FLIP_VERTICAL : 0));
	SDL_RenderCopyEx(video->app.renderer, texture, nullptr, &destination, 0, nullptr, flip);
	SDL_DestroyTexture(texture);
}

void MophunOS::vSetForeColor()
{	
	int32_t color = static_cast<int32_t>(mophunVM->readReg(p0));
	if (color & (1 << 31))
	{
		osdata.currentFgColor[0] = (color & 0x7C00) >> 7;
		osdata.currentFgColor[1] = (color & 0x3E0) >> 2;
		osdata.currentFgColor[2] = (color & 0x1f) << 3;
	}
	else
	{
		osdata.currentFgColor[0] = ((color >> 5) & 0x7) << 5;
		osdata.currentFgColor[1] = ((color >> 2) & 0x7) << 5;
		osdata.currentFgColor[2] = ((color >> 0) & 0x3) << 6;
	}
}

void MophunOS::vSpriteInit()
{
	uint8_t count = mophunVM->readReg(p0);
	osdata.spriteSlots.resize(count);
	mophunVM->writeReg(r0, 1);
}

void MophunOS::vSpriteClear()
{
	for (size_t i = 0; i < osdata.spriteSlots.size(); i++)
	{
		SDL_DestroyTexture(osdata.spriteSlots[i].spriteTexture);
	}
	int32_t size = osdata.spriteSlots.size();
	osdata.spriteSlots.clear();
	osdata.spriteSlots.resize(size);
}

void MophunOS::vSpriteSet()
{
	uint8_t slot = mophunVM->readReg(p0);
	SPRITE* sprite = reinterpret_cast<SPRITE*>(mophunVM->getRamAddress(mophunVM->readReg(p1)));
	int16_t x = mophunVM->readReg(p2);
	int16_t y = mophunVM->readReg(p3);

	if (osdata.spriteSlots[slot].spriteTexture != nullptr)
	{
		SDL_DestroyTexture(osdata.spriteSlots[slot].spriteTexture);
	}
	osdata.spriteSlots[slot].spriteData = sprite;
	osdata.spriteSlots[slot].x = x;
	osdata.spriteSlots[slot].y = y;
	
	osdata.spriteSlots[slot].spriteTexture = createSpriteTexture(video, osdata, sprite,
		(osdata.currentTransferMode & MODE_TRANS) != 0);
	// FIXME set centerpoint??
}

void MophunOS::vUpdateSprite()
{
	for (const SpriteSlot spriteSlot : osdata.spriteSlots)
	{
		if (spriteSlot.spriteData == nullptr)
			continue;

		SDL_Rect dstrect = { spriteSlot.x, spriteSlot.y,  spriteSlot.spriteData->width, spriteSlot.spriteData->height };
		SDL_RenderCopy(video->app.renderer, spriteSlot.spriteTexture, NULL, &dstrect);
	}
}

void MophunOS::vSetActiveFont()
{
	VMGPFONT* pFont = reinterpret_cast<VMGPFONT*>(mophunVM->getRamAddress(mophunVM->readReg(p0)));
	osdata.previousFont = osdata.currentFont;
	osdata.currentFont = pFont;
	// FIXME return osdata.previousFont in r0
}

void MophunOS::vSelectFont()
{
	const uint32_t requestedSize = mophunVM->readReg(p0);
	osdata.systemFontWidth = requestedSize == 2 ? 7 : 6;
	osdata.systemFontHeight = requestedSize == 2 ? 10 : 8;
	// The legacy API returns the subset of requested style flags it accepted.
	mophunVM->writeReg(r0, mophunVM->readReg(p1));
}

void MophunOS::vTextExtent()
{
	const char* text = reinterpret_cast<const char*>(mophunVM->getRamAddress(mophunVM->readReg(p0)));
	const uint32_t width = static_cast<uint32_t>(std::strlen(text)) * osdata.systemFontWidth;
	mophunVM->writeReg(r0, width | (osdata.systemFontHeight << 16));
}

void MophunOS::vTextExtentU()
{
	const uint8_t* text = mophunVM->getRamAddress(mophunVM->readReg(p0));
	uint32_t length = 0;
	while (text[length * 2] != 0 || text[length * 2 + 1] != 0)
		++length;
	mophunVM->writeReg(r0, length * osdata.systemFontWidth | (osdata.systemFontHeight << 16));
}

void MophunOS::vTextOut()
{
	int x = static_cast<int16_t>(mophunVM->readReg(p0));
	const int y = static_cast<int16_t>(mophunVM->readReg(p1));
	const char* text = reinterpret_cast<const char*>(mophunVM->getRamAddress(mophunVM->readReg(p2)));
	setForeground(video->app.renderer, osdata);
	while (*text != '\0')
	{
		drawSystemCharacter(video->app.renderer, x, y, *text++);
		x += osdata.systemFontWidth;
	}
}

void MophunOS::vTextOutU()
{
	int x = static_cast<int16_t>(mophunVM->readReg(p0));
	const int y = static_cast<int16_t>(mophunVM->readReg(p1));
	const uint8_t* text = mophunVM->getRamAddress(mophunVM->readReg(p2));
	setForeground(video->app.renderer, osdata);
	while (text[0] != 0 || text[1] != 0)
	{
		const uint16_t character = static_cast<uint16_t>(text[0] | (text[1] << 8));
		drawSystemCharacter(video->app.renderer, x, y,
			character < 128 ? static_cast<char>(character) : '?');
		x += osdata.systemFontWidth;
		text += 2;
	}
}


void MophunOS::vPrint()
{
	int32_t mode = static_cast<int32_t>(mophunVM->readReg(p0));
	int32_t x = static_cast<int32_t>(mophunVM->readReg(p1));
	int32_t y = static_cast<int32_t>(mophunVM->readReg(p2));
	const char* str = reinterpret_cast<char*>(mophunVM->getRamAddress(mophunVM->readReg(p3)));
	if (mode != MODE_TRANS || osdata.currentFont->bpp != 1)
	{
		std::cout << "unsupported vPrint mode: " << mode << "or bpp: " << osdata.currentFont->bpp << std::endl;
		return;
	}

	uint8_t* fnt = mophunVM->getRamAddress(osdata.currentFont->fontdata);
	uint8_t* charTbl = mophunVM->getRamAddress(osdata.currentFont->chartbl);

	int bitsPerChar = osdata.currentFont->width * osdata.currentFont->height * osdata.currentFont->bpp;
	int bytesPerChar = bitsPerChar / CHAR_BIT;
	std::vector<char> pixels;
	pixels.resize(bitsPerChar);


	uint8_t origRGBA[4];
	SDL_GetRenderDrawColor(video->app.renderer,
		&origRGBA[0], &origRGBA[1], &origRGBA[2],
		&origRGBA[3]);
	
	SDL_SetRenderDrawColor(video->app.renderer, osdata.currentFgColor[0], osdata.currentFgColor[1], osdata.currentFgColor[2], 255);
	
	while (*str != '\0')
	{
		uint8_t currChar = charTbl[static_cast<uint8_t>(*str)];
		int pixelIndex = 0;

		for (int i = 0; i < bytesPerChar; ++i)
		{
			uint8_t cur = fnt[currChar * bytesPerChar + i];
			int offset = i * CHAR_BIT;

			for (int bit = 0; bit < CHAR_BIT; ++bit)
			{
				pixels[offset] = cur & 1;
				++offset;
				cur >>= 1;
			}
		}
		
		for (int yy = y; yy < y+osdata.currentFont->height; yy++)
		{
			for (int xx = x; xx < x+osdata.currentFont->width; xx++)
			{
				if (pixels[pixelIndex])
					SDL_RenderDrawPoint(video->app.renderer, xx, yy);
				pixelIndex += 1;
			}
		}

		x += osdata.currentFont->width;
		str += 1;
	}

	SDL_SetRenderDrawColor(video->app.renderer, origRGBA[0], origRGBA[1], origRGBA[2], origRGBA[3]);
}
