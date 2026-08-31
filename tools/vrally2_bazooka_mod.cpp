// VRally2BazookaMod — patches the RC14EU M5 executable with a complete native
// bazooka combat loop: a weapon crate on the road, a lock-on crosshair, homing
// rockets, rival AI that also arms itself and fires back, explosions, wreck
// fires, and procedurally generated sound effects. Everything below runs as
// PIP2 guest code through standard Mophun OS calls; the emulator contains no
// bazooka-specific runtime behaviour.
//
// Recovered game facts used here (see target_vrally2.cpp):
//  - Rivals live in 28-byte entries {lane 16.16, distance 16.16, fraction,
//    segment, relative segment, 0, 0} at car+0xb8, count halfword at car+0xb4.
//  - The race renderer walks that array far-to-near, so it must stay sorted by
//    descending distance; each rival is tinted by vSetPaletteEntry(197,
//    colors[index]) immediately before its vDrawObject call.
//  - The updater moves entry i by (71500000 / speedDivisor) - 5000 * i units
//    per racing frame, and the position HUD is cached during the update phase.
//
// The crate is therefore injected as a phantom extra rival for the duration of
// the render phase only: inserted (sorted) after the game's update, removed at
// vFlipScreen. The game projects, scales, and depth-sorts it for us; the
// vDrawObject hook swaps the tinted car sprite for crate artwork of the same
// footprint. Destroyed rivals stay frozen on the road as burnt, burning wrecks.

#include "binary_io.h"
#include "mophunmod/mpn_image.h"
#include "mophunmod/patch_builder.h"
#include "mophunmod/pip_assembler.h"
#include "mophunmod/target.h"
#include "rom_decrypt.h"
#include "vmgp_header.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace mophunmod;
using Assembler = PipAssembler;

constexpr double Pi = 3.14159265358979323846;
constexpr uint32_t SoundSampleRate = 11025;

// ---------------------------------------------------------------------------
// Guest state layout (single BSS allocation).
// ---------------------------------------------------------------------------
constexpr uint32_t StInRace = 0;          // 0 idle, 1 racing, 2 disabled
constexpr uint32_t StCar = 4;
constexpr uint32_t StPrevKeys = 8;
constexpr uint32_t StFrame = 12;
constexpr uint32_t StFlipFrame = 16;
constexpr uint32_t StBaseCount = 20;
constexpr uint32_t StCrateInSlots = 24;
constexpr uint32_t StPendingSlot = 28;
constexpr uint32_t StSoundBusyUntil = 32;
constexpr uint32_t StSoundPriority = 36;
constexpr uint32_t StShakeFrames = 40;
constexpr uint32_t StFlashFrames = 44;
constexpr uint32_t StSavedMode = 48;
constexpr uint32_t StMessageOff = 52;
constexpr uint32_t StMessageFrames = 56;
constexpr uint32_t StAmmo = 60;
constexpr uint32_t StAiming = 64;
constexpr uint32_t StCrossX = 68;
constexpr uint32_t StCrossY = 72;
constexpr uint32_t StLockTarget = 76;
constexpr uint32_t StLockFrames = 80;
constexpr uint32_t StAimTimeout = 84;
constexpr uint32_t StPlayerDead = 88;
constexpr uint32_t StPlayerDeadFrames = 92;
constexpr uint32_t StTargetedBy = 96;
constexpr uint32_t StCrateActive = 100;
constexpr uint32_t StCrateDist = 104;
constexpr uint32_t StCrateLane = 108;
constexpr uint32_t StCrateTimer = 112;
constexpr uint32_t StSoundReady = 116;
constexpr uint32_t StKeys = 120;
constexpr uint32_t StCopyDst = 124;       // 12-byte vCopyRect descriptor
constexpr uint32_t StCopySrc = 136;       // 12-byte vCopyRect descriptor
constexpr uint32_t StScratch28 = 148;     // rival-entry swap buffer
constexpr uint32_t StIdentity = 192;      // 8 words: entity in each slot
constexpr uint32_t StSlotOf = 224;        // 8 words: slot of each entity
constexpr uint32_t StHandles = 256;       // 8 sound handles
constexpr uint32_t StEnt = 288;           // 8 entity records
constexpr uint32_t EntStride = 48;
constexpr uint32_t EntDead = 0;
constexpr uint32_t EntLastDist = 4;
constexpr uint32_t EntAmmo = 8;
constexpr uint32_t EntAiState = 12;
constexpr uint32_t EntAiTimer = 16;
constexpr uint32_t EntAiTarget = 20;
constexpr uint32_t EntRectX = 24;
constexpr uint32_t EntRectY = 28;
constexpr uint32_t EntRectW = 32;
constexpr uint32_t EntRectH = 36;
constexpr uint32_t EntSeenFrame = 40;
constexpr uint32_t EntColor = 44;
constexpr uint32_t StRockets = StEnt + 8 * EntStride;   // 672
constexpr uint32_t RocketStride = 40;
constexpr uint32_t RkActive = 0;
constexpr uint32_t RkShooter = 4;         // -2 player, else entity
constexpr uint32_t RkTarget = 8;          // -2 player, -1 point, else entity
constexpr uint32_t RkFrames = 12;
constexpr uint32_t RkTotal = 16;
constexpr uint32_t RkFromX = 20;
constexpr uint32_t RkFromY = 24;
constexpr uint32_t RkToX = 28;
constexpr uint32_t RkToY = 32;
constexpr uint32_t RkLaunchLane = 36;
constexpr uint32_t StExplosions = StRockets + 4 * RocketStride; // 832
constexpr uint32_t ExStride = 16;
constexpr uint32_t ExFrames = 0;
constexpr uint32_t ExX = 4;
constexpr uint32_t ExY = 8;
constexpr uint32_t ExSize = 12;
constexpr uint32_t StateSize = StExplosions + 4 * ExStride;     // 896

constexpr uint32_t MaxEntities = 8;       // 7 rivals + phantom crate
constexpr int32_t ShooterPlayer = -2;
constexpr int32_t TargetPoint = -1;

constexpr uint32_t KeyUp = 0x01;
constexpr uint32_t KeyDown = 0x02;
constexpr uint32_t KeyLeft = 0x04;
constexpr uint32_t KeyRight = 0x08;
constexpr uint32_t KeyFire2 = 0x100;

// Sound effect ids (StHandles order).
constexpr int32_t SndPickup = 0;
constexpr int32_t SndBeep = 1;
constexpr int32_t SndAlarm = 2;
constexpr int32_t SndLaunch = 3;
constexpr int32_t SndExplosion = 4;
constexpr int32_t SndWrecked = 5;
constexpr uint32_t SoundCount = 6;

// Misc data blob layout.
constexpr uint32_t MiscLanes = 0;         // 3 words
constexpr uint32_t MiscDurations = 12;    // 6 words, playback length in frames
constexpr uint32_t MiscColors = 36;       // 8 words, entity tints (7 = crate)

// ---------------------------------------------------------------------------
// Small binary helpers.
// ---------------------------------------------------------------------------
void appendU16(std::vector<uint8_t>& bytes, uint16_t value)
{
	bytes.push_back(static_cast<uint8_t>(value));
	bytes.push_back(static_cast<uint8_t>(value >> 8));
}

void appendU32(std::vector<uint8_t>& bytes, uint32_t value)
{
	appendU16(bytes, static_cast<uint16_t>(value));
	appendU16(bytes, static_cast<uint16_t>(value >> 16));
}

void appendTag(std::vector<uint8_t>& bytes, const char* tag)
{
	bytes.insert(bytes.end(), tag, tag + 4);
}

std::vector<uint8_t> readFile(const std::string& path)
{
	std::ifstream input(path, std::ios::binary | std::ios::ate);
	if (!input)
		throw std::runtime_error("Unable to open input file: " + path);
	const std::streamoff length = input.tellg();
	if (length <= 0)
		throw std::runtime_error("Input file is empty: " + path);
	std::vector<uint8_t> bytes(static_cast<size_t>(length));
	input.seekg(0);
	if (!input.read(reinterpret_cast<char*>(bytes.data()), length))
		throw std::runtime_error("Unable to read input file: " + path);
	return bytes;
}

void writeFile(const std::string& path, const std::vector<uint8_t>& bytes)
{
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	if (!output || !output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()))
		throw std::runtime_error("Unable to write modded MPN: " + path);
}

// ---------------------------------------------------------------------------
// Procedural sound effects (11025 Hz mono 16-bit PCM RIFF/WAVE).
// ---------------------------------------------------------------------------
std::vector<uint8_t> makeWave(const std::vector<double>& samples)
{
	const uint32_t dataSize = static_cast<uint32_t>(samples.size() * 2);
	std::vector<uint8_t> wave;
	wave.reserve(44 + dataSize);
	appendTag(wave, "RIFF");
	appendU32(wave, 36 + dataSize);
	appendTag(wave, "WAVE");
	appendTag(wave, "fmt ");
	appendU32(wave, 16);
	appendU16(wave, 1);
	appendU16(wave, 1);
	appendU32(wave, SoundSampleRate);
	appendU32(wave, SoundSampleRate * 2);
	appendU16(wave, 2);
	appendU16(wave, 16);
	appendTag(wave, "data");
	appendU32(wave, dataSize);
	for (double sample : samples)
	{
		const double clipped = std::max(-1.0, std::min(1.0, sample));
		appendU16(wave, static_cast<uint16_t>(static_cast<int16_t>(clipped * 32000.0)));
	}
	return wave;
}

uint32_t noiseState = 0x1234abcdU;
double noise()
{
	noiseState = noiseState * 1664525U + 1013904223U;
	return (static_cast<int32_t>(noiseState >> 15) & 0xffff) / 32768.0 - 1.0;
}

std::vector<uint8_t> buildPickupSound()
{
	const uint32_t count = SoundSampleRate * 36 / 100;
	std::vector<double> samples(count);
	const double notes[3] = {587.33, 783.99, 1174.66};
	for (uint32_t i = 0; i < count; ++i)
	{
		const double t = static_cast<double>(i) / SoundSampleRate;
		const uint32_t note = std::min<uint32_t>(2, i * 3 / count);
		const double local = t - note * 0.12;
		const double env = std::exp(-local * 14.0);
		const double phase = 2.0 * Pi * notes[note] * t;
		samples[i] = (std::sin(phase) * 0.7 + std::sin(phase * 2.0) * 0.3) * 0.55 * env;
	}
	return makeWave(samples);
}

std::vector<uint8_t> buildBeepSound()
{
	const uint32_t count = SoundSampleRate * 7 / 100;
	std::vector<double> samples(count);
	for (uint32_t i = 0; i < count; ++i)
	{
		const double t = static_cast<double>(i) / SoundSampleRate;
		const double env = std::exp(-t * 40.0);
		samples[i] = std::sin(2.0 * Pi * 1318.5 * t) * 0.5 * env;
	}
	return makeWave(samples);
}

std::vector<uint8_t> buildAlarmSound()
{
	const uint32_t count = SoundSampleRate * 42 / 100;
	std::vector<double> samples(count);
	for (uint32_t i = 0; i < count; ++i)
	{
		const double t = static_cast<double>(i) / SoundSampleRate;
		const double frequency = (static_cast<int>(t / 0.07) & 1) ? 700.0 : 980.0;
		const double square = std::sin(2.0 * Pi * frequency * t) > 0.0 ? 1.0 : -1.0;
		const double env = 0.35 * (1.0 - t / 0.55);
		samples[i] = square * env;
	}
	return makeWave(samples);
}

std::vector<uint8_t> buildLaunchSound()
{
	const uint32_t count = SoundSampleRate * 65 / 100;
	std::vector<double> samples(count);
	double filtered = 0.0;
	double phase = 0.0;
	for (uint32_t i = 0; i < count; ++i)
	{
		const double a = static_cast<double>(i) / count;
		filtered = filtered * 0.55 + noise() * 0.45;
		phase += 2.0 * Pi * (320.0 - 240.0 * a) / SoundSampleRate;
		const double env = (a < 0.06 ? a / 0.06 : (1.0 - a)) * 0.9;
		samples[i] = (filtered * 0.62 + std::sin(phase) * 0.38) * env;
	}
	return makeWave(samples);
}

std::vector<double> explosionSamples(double seconds, double gain)
{
	const uint32_t count = static_cast<uint32_t>(SoundSampleRate * seconds);
	std::vector<double> samples(count);
	double low = 0.0;
	for (uint32_t i = 0; i < count; ++i)
	{
		const double t = static_cast<double>(i) / SoundSampleRate;
		const double a = static_cast<double>(i) / count;
		low = low * 0.82 + noise() * 0.18;
		const double crack = t < 0.03 ? noise() * (1.0 - t / 0.03) : 0.0;
		const double thump = std::sin(2.0 * Pi * (65.0 - 35.0 * a) * t);
		const double env = (1.0 - a) * (1.0 - a);
		samples[i] = (low * 0.72 + thump * 0.35 + crack * 0.6) * env * gain;
	}
	return samples;
}

std::vector<uint8_t> buildExplosionSound()
{
	return makeWave(explosionSamples(1.05, 1.0));
}

std::vector<uint8_t> buildWreckedSound()
{
	std::vector<double> samples = explosionSamples(1.5, 1.1);
	const std::vector<double> second = explosionSamples(1.0, 0.8);
	const uint32_t offset = SoundSampleRate * 35 / 100;
	for (uint32_t i = 0; i < second.size() && offset + i < samples.size(); ++i)
		samples[offset + i] += second[i];
	for (uint32_t i = 0; i < samples.size(); ++i)
	{
		const double t = static_cast<double>(i) / SoundSampleRate;
		samples[i] += std::sin(2.0 * Pi * (200.0 - 120.0 * std::min(1.0, t)) * t) *
			0.25 * std::exp(-t * 2.2);
	}
	return makeWave(samples);
}

// ---------------------------------------------------------------------------
// Procedural sprite artwork (Mophun format 7: 8bpp RGB332, 0 = transparent).
// ---------------------------------------------------------------------------
uint8_t solid(int red, int green, int blue)
{
	const uint8_t value = static_cast<uint8_t>(((red >> 5) << 5) |
		((green >> 5) << 2) | (blue >> 6));
	return value == 0 ? 0x01 : value;
}

struct Sprite {
	int16_t cx = 0;
	int16_t cy = 0;
	uint16_t w = 0;
	uint16_t h = 0;
	std::vector<uint8_t> pix;

	Sprite() = default;
	Sprite(int width, int height, int centerX, int centerY) :
		cx(static_cast<int16_t>(centerX)), cy(static_cast<int16_t>(centerY)),
		w(static_cast<uint16_t>(width)), h(static_cast<uint16_t>(height)),
		pix(static_cast<size_t>(width) * height, 0) {}

	void set(int x, int y, uint8_t value)
	{
		if (x >= 0 && y >= 0 && x < w && y < h)
			pix[static_cast<size_t>(y) * w + x] = value;
	}

	void rect(int x0, int y0, int x1, int y1, uint8_t value)
	{
		for (int y = y0; y <= y1; ++y)
			for (int x = x0; x <= x1; ++x)
				set(x, y, value);
	}

	void disc(double centerX, double centerY, double radius, uint8_t value)
	{
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
			{
				const double dx = x - centerX;
				const double dy = y - centerY;
				if (dx * dx + dy * dy <= radius * radius)
					set(x, y, value);
			}
	}
};

class SpriteBank {
	public:
		uint32_t add(const Sprite& sprite)
		{
			sprites.push_back(sprite);
			return static_cast<uint32_t>(sprites.size() - 1);
		}

		std::vector<uint8_t> serialize() const
		{
			std::vector<uint8_t> table;
			std::vector<uint8_t> body;
			const uint32_t tableSize = static_cast<uint32_t>(sprites.size() * 4);
			for (const Sprite& sprite : sprites)
			{
				appendU32(table, tableSize + static_cast<uint32_t>(body.size()));
				body.push_back(0);          // palette bank (unused by format 7)
				body.push_back(7);          // 8bpp direct RGB332
				appendU16(body, static_cast<uint16_t>(sprite.cx));
				appendU16(body, static_cast<uint16_t>(sprite.cy));
				appendU16(body, sprite.w);
				appendU16(body, sprite.h);
				body.insert(body.end(), sprite.pix.begin(), sprite.pix.end());
			}
			table.insert(table.end(), body.begin(), body.end());
			return table;
		}

	private:
		std::vector<Sprite> sprites;
};

double hash01(int x, int y, int salt)
{
	uint32_t value = static_cast<uint32_t>(x * 374761393 + y * 668265263 + salt * 2147483647);
	value = (value ^ (value >> 13)) * 1274126177U;
	return ((value ^ (value >> 16)) & 0xffff) / 65536.0;
}

Sprite makeCrosshair(bool locked)
{
	Sprite sprite(17, 17, 8, 8);
	const uint8_t color = locked ? solid(255, 40, 40) : solid(255, 255, 255);
	for (int angle = 0; angle < 360; ++angle)
	{
		const double radians = angle * Pi / 180.0;
		sprite.set(8 + static_cast<int>(std::lround(std::cos(radians) * 6.5)),
			8 + static_cast<int>(std::lround(std::sin(radians) * 6.5)), color);
	}
	for (int tick = 2; tick <= 5; ++tick)
	{
		sprite.set(8, tick - 1, color); sprite.set(8, 17 - tick, color);
		sprite.set(tick - 1, 8, color); sprite.set(17 - tick, 8, color);
	}
	sprite.set(8, 8, color);
	if (locked)
	{
		for (int i = 0; i < 3; ++i)
		{
			sprite.set(i, 0, color); sprite.set(0, i, color);
			sprite.set(16 - i, 0, color); sprite.set(16, i, color);
			sprite.set(i, 16, color); sprite.set(0, 16 - i, color);
			sprite.set(16 - i, 16, color); sprite.set(16, 16 - i, color);
		}
	}
	return sprite;
}

Sprite makeRocket(bool down)
{
	Sprite sprite(7, 14, 3, 7);
	const uint8_t body = solid(190, 190, 200);
	const uint8_t shade = solid(120, 120, 140);
	const uint8_t tip = solid(230, 40, 30);
	const uint8_t flame1 = solid(255, 230, 90);
	const uint8_t flame2 = solid(255, 130, 30);
	sprite.rect(2, 3, 4, 9, body);
	sprite.rect(4, 3, 4, 9, shade);
	sprite.set(3, 0, tip); sprite.rect(2, 1, 4, 2, tip);
	sprite.set(1, 8, shade); sprite.set(5, 8, shade);
	sprite.rect(1, 9, 1, 10, shade); sprite.rect(5, 9, 5, 10, shade);
	sprite.set(3, 10, flame1); sprite.rect(2, 11, 4, 11, flame2);
	sprite.set(3, 12, flame2); sprite.set(3, 13, flame1);
	if (down)
	{
		Sprite flipped = sprite;
		for (int y = 0; y < sprite.h; ++y)
			for (int x = 0; x < sprite.w; ++x)
				flipped.pix[static_cast<size_t>(sprite.h - 1 - y) * sprite.w + x] =
					sprite.pix[static_cast<size_t>(y) * sprite.w + x];
		return flipped;
	}
	return sprite;
}

// Crate footprints match the rival car sprite LODs (12x11 .. 32x29, anchored
// bottom-center) so the game's own projection places them on the road.
Sprite makeCrate(int w, int h, bool glint)
{
	Sprite sprite(w, h, w / 2, h - 1);
	const uint8_t wood = solid(150, 95, 35);
	const uint8_t woodDark = solid(105, 60, 20);
	const uint8_t edge = solid(60, 35, 10);
	const uint8_t hazard = solid(240, 200, 40);
	const uint8_t hazardDark = solid(30, 30, 30);
	const uint8_t tip = solid(230, 40, 30);
	const uint8_t metal = solid(200, 200, 210);
	const int top = h / 4;
	sprite.rect(0, top, w - 1, h - 1, wood);
	for (int x = 0; x < w; ++x)
		for (int y = top; y < h; ++y)
			if ((x + y) % 5 == 0)
				sprite.set(x, y, woodDark);
	sprite.rect(0, top, w - 1, top, edge);
	sprite.rect(0, h - 1, w - 1, h - 1, edge);
	sprite.rect(0, top, 0, h - 1, edge);
	sprite.rect(w - 1, top, w - 1, h - 1, edge);
	for (int x = 1; x < w - 1; ++x)
		sprite.set(x, top + 1, (x / 2) % 2 == 0 ? hazard : hazardDark);
	if (h >= 14)
	{
		const int mx = w / 2;
		sprite.rect(mx - 1, top + 3, mx, h - 3, metal);
		sprite.set(mx - 1, top + 2, tip); sprite.set(mx, top + 2, tip);
	}
	if (glint)
	{
		sprite.set(2, top + 2, solid(255, 255, 255));
		sprite.set(3, top + 3, solid(255, 255, 220));
	}
	return sprite;
}

Sprite makeExplosion(int size, int frame)
{
	Sprite sprite(size, size, size / 2, size / 2);
	static const double radii[7] = {0.30, 0.55, 0.78, 0.95, 1.0, 0.92, 0.80};
	const double radius = radii[frame] * size / 2.0;
	const double cx = size / 2.0 - 0.5;
	const double cy = size / 2.0 - 0.5;
	for (int y = 0; y < size; ++y)
		for (int x = 0; x < size; ++x)
		{
			const double dx = x - cx;
			const double dy = y - cy;
			const double angle = std::atan2(dy, dx);
			const double wobble = 1.0 + 0.28 *
				(hash01(static_cast<int>(angle * 4.0 + 16.0), frame, size) - 0.5);
			const double d = std::sqrt(dx * dx + dy * dy) / (radius * wobble + 0.001);
			if (d > 1.0)
				continue;
			const double n = hash01(x, y, frame * 31 + size);
			uint8_t color = 0;
			if (frame < 2)
				color = d < 0.55 ? solid(255, 255, 235) : solid(255, 220, 70);
			else if (frame < 4)
			{
				if (d < 0.28) color = solid(255, 255, 240);
				else if (d < 0.62) color = solid(255, 210, 60);
				else if (d < 0.88) color = solid(250, 120, 25);
				else color = solid(200, 45, 15);
				if (n > 0.86) color = solid(255, 245, 160);
			}
			else if (frame < 6)
			{
				if (d < 0.4 && n < 0.7) color = solid(250, 140, 30);
				else if (d < 0.75) color = n < 0.55 ? solid(190, 60, 15) : solid(95, 85, 80);
				else color = solid(70, 65, 60);
				if (n > 0.9) color = 0;
			}
			else
				color = n > 0.42 ? (n > 0.75 ? solid(120, 115, 110) : solid(80, 78, 75)) : 0;
			if (color != 0)
				sprite.set(x, y, color);
		}
	return sprite;
}

Sprite makeFlame(int w, int frame)
{
	const int h = w * 5 / 4;
	Sprite sprite(w, h, w / 2, h - 1);
	for (int x = 0; x < w; ++x)
	{
		const double edge = 1.0 - std::abs(x - (w - 1) / 2.0) / (w / 2.0);
		const double height = h * (0.30 + 0.65 * edge) *
			(0.65 + 0.5 * hash01(x, frame, w));
		for (int y = 0; y < static_cast<int>(height); ++y)
		{
			const double a = static_cast<double>(y) / height;
			uint8_t color;
			if (a < 0.35) color = solid(255, 245, 150);
			else if (a < 0.7) color = solid(255, 160, 30);
			else color = solid(210, 60, 15);
			if (hash01(x, y + frame * 7, w) > 0.88)
				continue;
			sprite.set(x, h - 1 - y, color);
		}
	}
	return sprite;
}

Sprite makeSmoke(int size)
{
	Sprite sprite(size, size, size / 2, size / 2);
	sprite.disc(size / 2.0 - 0.5, size / 2.0 - 0.5, size / 2.0 - 0.5, solid(130, 128, 125));
	sprite.disc(size / 2.0 - 1.0, size / 2.0 - 1.2, size / 4.0, solid(165, 162, 158));
	return sprite;
}

Sprite makeHudIcon()
{
	Sprite sprite(24, 10, 0, 0);
	const uint8_t tube = solid(70, 120, 60);
	const uint8_t tubeDark = solid(40, 75, 35);
	const uint8_t tip = solid(230, 40, 30);
	const uint8_t sight = solid(220, 220, 220);
	sprite.rect(1, 4, 19, 7, tube);
	sprite.rect(1, 7, 19, 7, tubeDark);
	sprite.rect(0, 3, 2, 8, tubeDark);
	sprite.rect(18, 3, 19, 8, tubeDark);
	sprite.rect(20, 4, 22, 7, tip);
	sprite.set(23, 5, tip); sprite.set(23, 6, tip);
	sprite.rect(6, 1, 7, 3, sight);
	sprite.set(6, 0, sight);
	return sprite;
}

Sprite makeAmmoIcon()
{
	Sprite sprite(5, 9, 0, 0);
	sprite.set(2, 0, solid(230, 40, 30));
	sprite.rect(1, 1, 3, 2, solid(230, 40, 30));
	sprite.rect(1, 3, 3, 6, solid(190, 190, 200));
	sprite.set(2, 7, solid(255, 160, 30));
	sprite.set(2, 8, solid(255, 230, 90));
	return sprite;
}

// 5x7 pixel font for the HUD messages.
const std::map<char, std::array<const char*, 7>>& fontGlyphs()
{
	static const std::map<char, std::array<const char*, 7>> glyphs = {
		{'A', {" ### ", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"}},
		{'B', {"#### ", "#   #", "#   #", "#### ", "#   #", "#   #", "#### "}},
		{'C', {" ####", "#    ", "#    ", "#    ", "#    ", "#    ", " ####"}},
		{'D', {"#### ", "#   #", "#   #", "#   #", "#   #", "#   #", "#### "}},
		{'E', {"#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#####"}},
		{'F', {"#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#    "}},
		{'G', {" ####", "#    ", "#    ", "# ###", "#   #", "#   #", " ### "}},
		{'H', {"#   #", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"}},
		{'I', {"#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "#####"}},
		{'J', {"    #", "    #", "    #", "    #", "#   #", "#   #", " ### "}},
		{'K', {"#   #", "#  # ", "# #  ", "##   ", "# #  ", "#  # ", "#   #"}},
		{'L', {"#    ", "#    ", "#    ", "#    ", "#    ", "#    ", "#####"}},
		{'M', {"#   #", "## ##", "# # #", "# # #", "#   #", "#   #", "#   #"}},
		{'N', {"#   #", "##  #", "# # #", "#  ##", "#   #", "#   #", "#   #"}},
		{'O', {" ### ", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "}},
		{'P', {"#### ", "#   #", "#   #", "#### ", "#    ", "#    ", "#    "}},
		{'Q', {" ### ", "#   #", "#   #", "#   #", "# # #", "#  # ", " ## #"}},
		{'R', {"#### ", "#   #", "#   #", "#### ", "# #  ", "#  # ", "#   #"}},
		{'S', {" ####", "#    ", "#    ", " ### ", "    #", "    #", "#### "}},
		{'T', {"#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  "}},
		{'U', {"#   #", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "}},
		{'V', {"#   #", "#   #", "#   #", "#   #", "#   #", " # # ", "  #  "}},
		{'W', {"#   #", "#   #", "#   #", "# # #", "# # #", "## ##", "#   #"}},
		{'X', {"#   #", "#   #", " # # ", "  #  ", " # # ", "#   #", "#   #"}},
		{'Y', {"#   #", "#   #", " # # ", "  #  ", "  #  ", "  #  ", "  #  "}},
		{'Z', {"#####", "    #", "   # ", "  #  ", " #   ", "#    ", "#####"}},
		{'0', {" ### ", "#   #", "#  ##", "# # #", "##  #", "#   #", " ### "}},
		{'1', {"  #  ", " ##  ", "  #  ", "  #  ", "  #  ", "  #  ", "#####"}},
		{'2', {" ### ", "#   #", "    #", "   # ", "  #  ", " #   ", "#####"}},
		{'3', {" ### ", "#   #", "    #", "  ## ", "    #", "#   #", " ### "}},
		{'4', {"   # ", "  ## ", " # # ", "#  # ", "#####", "   # ", "   # "}},
		{'5', {"#####", "#    ", "#### ", "    #", "    #", "#   #", " ### "}},
		{'6', {" ### ", "#    ", "#    ", "#### ", "#   #", "#   #", " ### "}},
		{'7', {"#####", "    #", "   # ", "  #  ", " #   ", " #   ", " #   "}},
		{'8', {" ### ", "#   #", "#   #", " ### ", "#   #", "#   #", " ### "}},
		{'9', {" ### ", "#   #", "#   #", " ####", "    #", "    #", " ### "}},
		{'!', {"  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "     ", "  #  "}},
		{'-', {"     ", "     ", "     ", "#####", "     ", "     ", "     "}},
		{'/', {"    #", "    #", "   # ", "  #  ", " #   ", "#    ", "#    "}}
	};
	return glyphs;
}

Sprite makeGlyph(char letter, uint8_t color)
{
	Sprite sprite(5, 7, 0, 0);
	const auto& glyphs = fontGlyphs();
	const auto found = glyphs.find(letter);
	if (found == glyphs.end())
		return sprite;
	for (int y = 0; y < 7; ++y)
		for (int x = 0; x < 5; ++x)
			if (found->second[y][x] == '#')
				sprite.set(x, y, color);
	return sprite;
}

// Sprite bank indices, filled while the bank is built.
struct SpriteIds {
	uint32_t crossWhite = 0;
	uint32_t crossRed = 0;
	uint32_t rocketUp = 0;
	uint32_t rocketDown = 0;
	uint32_t crateBase = 0;      // 4 sizes x 2 variants
	uint32_t explosionBase = 0;  // 4 sizes x 7 frames
	uint32_t flameBase = 0;      // 4 sizes x 3 frames
	uint32_t smokeSmall = 0;
	uint32_t smokeBig = 0;
	uint32_t hudIcon = 0;
	uint32_t ammoIcon = 0;
	uint32_t fontWhite = 0;      // 59 glyphs, ASCII 32..90
	uint32_t fontBlack = 0;
};

std::vector<uint8_t> buildSpriteBank(SpriteIds& ids)
{
	SpriteBank bank;
	ids.crossWhite = bank.add(makeCrosshair(false));
	ids.crossRed = bank.add(makeCrosshair(true));
	ids.rocketUp = bank.add(makeRocket(false));
	ids.rocketDown = bank.add(makeRocket(true));
	// Same footprints and anchors as the rival car LOD sprites.
	static const int crateW[4] = {12, 17, 24, 32};
	static const int crateH[4] = {11, 16, 22, 29};
	ids.crateBase = 0;
	for (int size = 0; size < 4; ++size)
		for (int variant = 0; variant < 2; ++variant)
		{
			const uint32_t id = bank.add(makeCrate(crateW[size], crateH[size], variant == 1));
			if (size == 0 && variant == 0)
				ids.crateBase = id;
		}
	static const int explosionSizes[4] = {18, 26, 36, 48};
	for (int size = 0; size < 4; ++size)
		for (int frame = 0; frame < 7; ++frame)
		{
			const uint32_t id = bank.add(makeExplosion(explosionSizes[size], frame));
			if (size == 0 && frame == 0)
				ids.explosionBase = id;
		}
	static const int flameSizes[4] = {10, 14, 18, 24};
	for (int size = 0; size < 4; ++size)
		for (int frame = 0; frame < 3; ++frame)
		{
			const uint32_t id = bank.add(makeFlame(flameSizes[size], frame));
			if (size == 0 && frame == 0)
				ids.flameBase = id;
		}
	ids.smokeSmall = bank.add(makeSmoke(6));
	ids.smokeBig = bank.add(makeSmoke(9));
	ids.hudIcon = bank.add(makeHudIcon());
	ids.ammoIcon = bank.add(makeAmmoIcon());
	for (int pass = 0; pass < 2; ++pass)
	{
		const uint8_t color = pass == 0 ? solid(255, 255, 255) : solid(24, 24, 24);
		for (char letter = 32; letter <= 90; ++letter)
		{
			const uint32_t id = bank.add(makeGlyph(letter, color));
			if (letter == 32)
				(pass == 0 ? ids.fontWhite : ids.fontBlack) = id;
		}
	}
	return bank.serialize();
}

// ---------------------------------------------------------------------------
// Guest program generation.
// ---------------------------------------------------------------------------
struct GuestPools {
	PoolId state = 0;
	PoolId car = 0;
	PoolId trackLen = 0;
	PoolId colors = 0;
	PoolId misc = 0;
	PoolId bank = 0;
	PoolId text = 0;
	PoolId waves[SoundCount] = {};
	PoolId origUpdate = 0;
	PoolId origFlip = 0;
	PoolId origButton = 0;
	PoolId origDraw = 0;
	PoolId origPalette = 0;
	PoolId fillRect = 0;
	PoolId setClip = 0;
	PoolId setFore = 0;
	PoolId setMode = 0;
	PoolId copyRect = 0;
	PoolId random = 0;
	PoolId soundInit = 0;
	PoolId soundHandle = 0;
	PoolId soundCtrlEx = 0;
};

struct GameConstants {
	uint32_t startedOff = 0;
	uint32_t targetSpeedOff = 0;
	uint32_t speedOff = 0;
	uint32_t jumpOff = 0;
	uint32_t segOff = 0;
	uint32_t laneOff = 0;
	uint32_t countOff = 0;
	uint32_t oppOff = 0;
	uint32_t stride = 0;
	uint32_t speedStep = 0;
	uint32_t paletteIndex = 0;
};

struct TextOffsets {
	uint32_t bazooka = 0;
	uint32_t rivalArmed = 0;
	uint32_t destroyed = 0;
	uint32_t wrecked = 0;
	uint32_t incoming = 0;
	uint32_t go = 0;
};

PipProgram buildGuestCode(const GuestPools& P, const GameConstants& G,
	const SpriteIds& S, const TextOffsets& T, Assembler& a)
{
	// Conventions: routines begin with STORE ra, s6 (saving ra, fp, s0..s7),
	// keep the state base in s0 and the car pointer in s1, and end with
	// RET s7, s6. Leaf helpers marked below preserve every s register and fp.
	auto ldw = [&](Register r, uint32_t field) { a.immediate(LDWd, r, s0, static_cast<int32_t>(field)); };
	auto stw = [&](Register r, uint32_t field) { a.immediate(STWd, r, s0, static_cast<int32_t>(field)); };
	auto ldk = [&](Register r, int32_t value) {
		if (value >= -32768 && value <= 32767)
			a.ldq(r, static_cast<int16_t>(value));
		else
			a.immediate(LDI, r, zero, value);
	};
	auto stwK = [&](uint32_t field, int32_t value) { ldk(r0, value); stw(r0, field); };
	auto enter = [&]() { a.op(STORE, ra, s6); a.pool(LDI, s0, zero, P.state); };
	auto leave = [&]() { a.op(RET, s7, s6); };
	auto ifZero = [&](Register r, const std::string& target) { a.branch(BEQ, r, zero, target); };
	auto ifNotZero = [&](Register r, const std::string& target) { a.branch(BNE, r, zero, target); };
	auto rectCenter = [&](Register rec, uint32_t posOff, uint32_t sizeOff, Register out) {
		a.immediate(LDWd, out, rec, static_cast<int32_t>(posOff));
		a.immediate(LDWd, r1, rec, static_cast<int32_t>(sizeOff));
		a.op(SRAi, r1, r1, 1);
		a.op(ADD, out, out, r1);
	};
	// Fresh: rec.SeenFrame + 3 >= FlipFrame, i.e. drawn this frame or the last.
	auto ifStale = [&](Register rec, const std::string& target) {
		a.immediate(LDWd, r0, rec, static_cast<int32_t>(EntSeenFrame));
		a.op(ADDQ, r0, r0, 3);
		ldw(r1, StFlipFrame);
		a.branch(BLT, r0, r1, target);
	};

	// -- BzSetColor(p0 = RGB555) ---------------------------------------------
	a.label("BzSetColor");
	a.op(STORE, zero, sp);
	a.ldq(p1, 1);
	a.op(SLLi, p1, p1, 31);
	a.op(OR, p0, p0, p1);
	a.callPool(P.setFore);
	a.op(RET, zero, sp);

	// -- BzSpriteAddr(p0 = bank index) -> r0 (leaf) --------------------------
	a.label("BzSpriteAddr");
	a.op(STORE, zero, sp);
	a.pool(LDI, r0, zero, P.bank);
	a.op(SLLi, p0, p0, 2);
	a.op(ADD, p0, r0, p0);
	a.immediate(LDWd, p0, p0, 0);
	a.op(ADD, r0, r0, p0);
	a.op(RET, zero, sp);

	// -- BzDrawSprite(p0 = x, p1 = y, p2 = bank index) -----------------------
	a.label("BzDrawSprite");
	a.op(STORE, ra, s6);
	a.op(MOV, s2, p0);
	a.op(MOV, s3, p1);
	a.op(MOV, p0, p2);
	a.call("BzSpriteAddr");
	a.op(MOV, p2, r0);
	a.op(MOV, p0, s2);
	a.op(MOV, p1, s3);
	a.callPool(P.origDraw);
	a.op(RET, s7, s6);

	// -- BzSlotPtr(p0 = slot) -> r0 (leaf) -----------------------------------
	a.label("BzSlotPtr");
	a.op(STORE, zero, sp);
	a.op(MULQ, p0, p0, static_cast<uint8_t>(G.stride));
	a.pool(LDI, r0, zero, P.car);
	a.immediate(ADDi, r0, r0, static_cast<int32_t>(G.oppOff));
	a.op(ADD, r0, r0, p0);
	a.op(RET, zero, sp);

	// -- BzEntPtr(p0 = entity) -> r0 (leaf) ----------------------------------
	a.label("BzEntPtr");
	a.op(STORE, zero, sp);
	a.op(MULQ, p0, p0, static_cast<uint8_t>(EntStride));
	a.pool(LDI, r0, zero, P.state);
	a.immediate(ADDi, r0, r0, static_cast<int32_t>(StEnt));
	a.op(ADD, r0, r0, p0);
	a.op(RET, zero, sp);

	// -- BzResync(p0 = slot entry ptr): rebuild fraction/segment/relative ----
	a.label("BzResync");
	a.op(STORE, zero, sp);
	a.immediate(LDWd, p1, p0, 4);
	a.op(SRAi, p2, p1, 17);
	a.immediate(ANDi, p3, p1, 0x1ffff);
	a.immediate(STWd, p3, p0, 8);
	a.immediate(STWd, p2, p0, 12);
	a.pool(LDI, r0, zero, P.car);
	a.immediate(LDBHd, r1, r0, static_cast<int32_t>(G.segOff));
	a.op(SUB, p2, p2, r1);
	a.pool(LDWd, r0, zero, P.trackLen);
	a.op(DIV, g0, p2, r0);
	a.op(MUL, g0, g0, r0);
	a.op(SUB, p2, p2, g0);
	a.immediate(STWd, p2, p0, 16);
	a.op(RET, zero, sp);

	// -- BzRebuildSlotOf(p0 = slot count) (leaf) -----------------------------
	a.label("BzRebuildSlotOf");
	a.op(STORE, zero, sp);
	a.op(MOV, r1, p0);
	a.pool(LDI, r0, zero, P.state);
	a.ldq(p1, 0);
	a.label("RbLoop");
	a.branch(BGE, p1, r1, "RbDone");
	a.op(SLLi, p2, p1, 2);
	a.op(ADD, p2, p2, r0);
	a.immediate(LDWd, p3, p2, static_cast<int32_t>(StIdentity));
	a.op(SLLi, g0, p3, 2);
	a.op(ADD, g0, g0, r0);
	a.immediate(STWd, p1, g0, static_cast<int32_t>(StSlotOf));
	a.op(ADDQ, p1, p1, 1);
	a.jump("RbLoop");
	a.label("RbDone");
	a.op(RET, zero, sp);

	// -- BzPlaySound(p0 = effect, p1 = priority) -----------------------------
	a.label("BzPlaySound");
	enter();
	ldw(r0, StSoundReady);
	ifZero(r0, "PsDone");
	a.op(MOV, s2, p0);
	a.op(MOV, s3, p1);
	ldw(r0, StSoundBusyUntil);
	ldw(r1, StFrame);
	a.branch(BGE, r1, r0, "PsPlay");
	ldw(r0, StSoundPriority);
	a.branch(BLT, s3, r0, "PsDone");
	a.label("PsPlay");
	a.op(SLLi, r0, s2, 2);
	a.op(ADD, r0, r0, s0);
	a.immediate(LDWd, p0, r0, static_cast<int32_t>(StHandles));
	a.ldq(p1, 1);   // SNDCTRL_PLAY
	a.ldq(p2, 0);
	a.callPool(P.soundCtrlEx);
	a.op(SLLi, r0, s2, 2);
	a.pool(LDI, r1, zero, P.misc);
	a.op(ADD, r0, r0, r1);
	a.immediate(LDWd, r0, r0, static_cast<int32_t>(MiscDurations));
	ldw(r1, StFrame);
	a.op(ADD, r0, r0, r1);
	stw(r0, StSoundBusyUntil);
	stw(s3, StSoundPriority);
	a.label("PsDone");
	leave();

	// -- BzRand(p0 = modulus > 0) -> r0 --------------------------------------
	a.label("BzRand");
	a.op(STORE, ra, s6);
	a.op(MOV, s2, p0);
	a.callPool(P.random);
	a.op(DIVU, r1, r0, s2);
	a.op(MUL, r1, r1, s2);
	a.op(SUB, r0, r0, r1);
	a.op(RET, s7, s6);

	// -- BzAllocRocket() -> r0 (slot ptr or 0) (leaf) ------------------------
	a.label("BzAllocRocket");
	a.op(STORE, zero, sp);
	a.pool(LDI, r0, zero, P.state);
	a.immediate(ADDi, r0, r0, static_cast<int32_t>(StRockets));
	a.ldq(r1, 0);
	a.label("ArLoop");
	a.immediate(LDWd, g0, r0, static_cast<int32_t>(RkActive));
	ifZero(g0, "ArFound");
	a.op(ADDQ, r0, r0, static_cast<uint8_t>(RocketStride));
	a.op(ADDQ, r1, r1, 1);
	a.branchImmediate(BLTI, r1, 4, "ArLoop");
	a.ldq(r0, 0);
	a.label("ArFound");
	a.op(RET, zero, sp);

	// -- BzSpawnExplosion(p0 = x, p1 = y, p2 = size class) (leaf) ------------
	a.label("BzSpawnExplosion");
	a.op(STORE, zero, sp);
	a.pool(LDI, r0, zero, P.state);
	a.immediate(ADDi, r0, r0, static_cast<int32_t>(StExplosions));
	a.op(MOV, g1, r0);
	a.ldq(r1, 0);
	a.label("SeLoop");
	a.immediate(LDWd, g0, r0, static_cast<int32_t>(ExFrames));
	a.branch(BGT, g0, zero, "SeNext");
	a.op(MOV, g1, r0);
	a.jump("SeWrite");
	a.label("SeNext");
	a.op(ADDQ, r0, r0, static_cast<uint8_t>(ExStride));
	a.op(ADDQ, r1, r1, 1);
	a.branchImmediate(BLTI, r1, 4, "SeLoop");
	a.label("SeWrite");
	// Keep the fireball on screen even when the victim just slid off an edge.
	a.ldq(r0, 6);
	a.branch(BGE, p0, r0, "SeClX1");
	a.op(MOV, p0, r0);
	a.label("SeClX1");
	a.ldq(r0, 121);
	a.branch(BLE, p0, r0, "SeClX2");
	a.op(MOV, p0, r0);
	a.label("SeClX2");
	a.ldq(r0, 12);
	a.branch(BGE, p1, r0, "SeClY1");
	a.op(MOV, p1, r0);
	a.label("SeClY1");
	ldk(r0, 150);
	a.branch(BLE, p1, r0, "SeClY2");
	a.op(MOV, p1, r0);
	a.label("SeClY2");
	a.ldq(r0, 14);
	a.immediate(STWd, r0, g1, static_cast<int32_t>(ExFrames));
	a.immediate(STWd, p0, g1, static_cast<int32_t>(ExX));
	a.immediate(STWd, p1, g1, static_cast<int32_t>(ExY));
	a.immediate(STWd, p2, g1, static_cast<int32_t>(ExSize));
	a.op(RET, zero, sp);

	// -- BzMessage(p0 = text offset, p1 = frames) (leaf) ---------------------
	a.label("BzMessage");
	a.op(STORE, zero, sp);
	a.pool(LDI, r0, zero, P.state);
	a.immediate(STWd, p0, r0, static_cast<int32_t>(StMessageOff));
	a.immediate(STWd, p1, r0, static_cast<int32_t>(StMessageFrames));
	a.op(RET, zero, sp);

	// -- BzKillCar(p0 = entity): freeze, tint dark, explode, shake -----------
	a.label("BzKillCar");
	enter();
	a.op(MOV, s2, p0);
	a.call("BzEntPtr");
	a.op(MOV, s3, r0);
	a.ldq(r0, 1);
	a.immediate(STWd, r0, s3, static_cast<int32_t>(EntDead));
	a.op(SLLi, r0, s2, 2);
	a.op(ADD, r0, r0, s0);
	a.immediate(LDWd, p0, r0, static_cast<int32_t>(StSlotOf));
	a.call("BzSlotPtr");
	a.immediate(LDWd, r0, r0, 4);
	a.immediate(STWd, r0, s3, static_cast<int32_t>(EntLastDist));
	ldk(r0, 0x00ff0842);
	a.op(ADD, r0, r0, s2);
	a.immediate(STWd, r0, s3, static_cast<int32_t>(EntColor));
	ifStale(s3, "KcNoVisual");
	rectCenter(s3, EntRectX, EntRectW, p0);
	rectCenter(s3, EntRectY, EntRectH, p1);
	a.immediate(LDWd, g0, s3, static_cast<int32_t>(EntRectW));
	a.ldq(p2, 3);
	a.ldq(g1, 28);
	a.branch(BGE, g0, g1, "KcSize");
	a.ldq(p2, 2);
	a.ldq(g1, 20);
	a.branch(BGE, g0, g1, "KcSize");
	a.ldq(p2, 1);
	a.ldq(g1, 14);
	a.branch(BGE, g0, g1, "KcSize");
	a.ldq(p2, 0);
	a.label("KcSize");
	a.call("BzSpawnExplosion");
	a.label("KcNoVisual");
	stwK(StShakeFrames, 6);
	stwK(StFlashFrames, 2);
	a.ldq(p0, SndExplosion);
	a.ldq(p1, 5);
	a.call("BzPlaySound");
	leave();

	// -- BzKillPlayer --------------------------------------------------------
	a.label("BzKillPlayer");
	enter();
	ldw(s1, StCar);
	stwK(StPlayerDead, 1);
	stwK(StPlayerDeadFrames, 70);
	stwK(StAiming, 0);
	stwK(StAmmo, 0);
	stwK(StLockTarget, -1);
	a.ldq(p0, 64);
	a.immediate(LDHUd, g0, s1, static_cast<int32_t>(G.jumpOff));
	ldk(p1, 134);
	a.op(SUB, p1, p1, g0);
	a.ldq(p2, 3);
	a.call("BzSpawnExplosion");
	stwK(StShakeFrames, 14);
	stwK(StFlashFrames, 3);
	ldk(p0, static_cast<int32_t>(T.wrecked));
	a.ldq(p1, 55);
	a.call("BzMessage");
	a.ldq(p0, SndWrecked);
	a.ldq(p1, 6);
	a.call("BzPlaySound");
	leave();

	// -- BzSortCars: keep rival entries sorted by descending distance --------
	a.label("BzSortCars");
	enter();
	ldw(s2, StBaseCount);
	a.op(ADDQ, s2, s2, static_cast<uint8_t>(-1));
	a.label("SortOuter");
	a.branch(BLE, s2, zero, "SortDone");
	a.ldq(s3, 0);
	a.label("SortInner");
	a.branch(BGE, s3, s2, "SortOuterNext");
	a.op(MOV, p0, s3);
	a.call("BzSlotPtr");
	a.op(MOV, s4, r0);
	a.immediate(ADDi, s5, s4, static_cast<int32_t>(G.stride));
	a.immediate(LDWd, r0, s4, 4);
	a.immediate(LDWd, r1, s5, 4);
	a.branch(BGE, r0, r1, "SortNoSwap");
	a.immediate(ADDi, p0, s0, static_cast<int32_t>(StScratch28));
	a.op(MOV, p1, s4);
	a.ldq(r0, static_cast<int16_t>(G.stride));
	a.op(SYSCPY, p0, p1, r0);
	a.op(MOV, p0, s4);
	a.op(MOV, p1, s5);
	a.op(SYSCPY, p0, p1, r0);
	a.op(MOV, p0, s5);
	a.immediate(ADDi, p1, s0, static_cast<int32_t>(StScratch28));
	a.op(SYSCPY, p0, p1, r0);
	a.op(SLLi, r0, s3, 2);
	a.op(ADD, r0, r0, s0);
	a.immediate(LDWd, g0, r0, static_cast<int32_t>(StIdentity));
	a.immediate(LDWd, g1, r0, static_cast<int32_t>(StIdentity + 4));
	a.immediate(STWd, g1, r0, static_cast<int32_t>(StIdentity));
	a.immediate(STWd, g0, r0, static_cast<int32_t>(StIdentity + 4));
	a.label("SortNoSwap");
	a.op(ADDQ, s3, s3, 1);
	a.jump("SortInner");
	a.label("SortOuterNext");
	a.op(ADDQ, s2, s2, static_cast<uint8_t>(-1));
	a.jump("SortOuter");
	a.label("SortDone");
	ldw(p0, StBaseCount);
	a.call("BzRebuildSlotOf");
	leave();

	// -- BzInsertCrate: phantom rival for the render phase only --------------
	a.label("BzInsertCrate");
	enter();
	ldw(s2, StBaseCount);
	ldw(s3, StCrateDist);
	a.ldq(s4, 0);
	a.label("InsFind");
	a.branch(BGE, s4, s2, "InsAt");
	a.op(MOV, p0, s4);
	a.call("BzSlotPtr");
	a.immediate(LDWd, r0, r0, 4);
	a.branch(BLT, r0, s3, "InsAt");
	a.op(ADDQ, s4, s4, 1);
	a.jump("InsFind");
	a.label("InsAt");
	a.op(MOV, p0, s4);
	a.call("BzSlotPtr");
	a.op(MOV, p1, r0);
	a.immediate(ADDi, p0, r0, static_cast<int32_t>(G.stride));
	a.op(SUB, r0, s2, s4);
	a.op(MULQ, r0, r0, static_cast<uint8_t>(G.stride));
	a.op(SYSCPY, p0, p1, r0);
	a.op(SLLi, r0, s4, 2);
	a.op(ADD, r0, r0, s0);
	a.immediate(ADDi, p1, r0, static_cast<int32_t>(StIdentity));
	a.immediate(ADDi, p0, p1, 4);
	a.op(SUB, r0, s2, s4);
	a.op(SLLi, r0, r0, 2);
	a.op(SYSCPY, p0, p1, r0);
	a.op(MOV, p0, s4);
	a.call("BzSlotPtr");
	a.op(MOV, s5, r0);
	ldw(r0, StCrateLane);
	a.immediate(STWd, r0, s5, 0);
	a.immediate(STWd, s3, s5, 4);
	a.ldq(r0, 0);
	a.immediate(STWd, r0, s5, 20);
	a.immediate(STWd, r0, s5, 24);
	a.op(MOV, p0, s5);
	a.call("BzResync");
	a.op(SLLi, r0, s4, 2);
	a.op(ADD, r0, r0, s0);
	a.immediate(STWd, s2, r0, static_cast<int32_t>(StIdentity));
	a.pool(LDI, r0, zero, P.car);
	a.op(ADDQ, r1, s2, 1);
	a.immediate(STHd, r1, r0, static_cast<int32_t>(G.countOff));
	stwK(StCrateInSlots, 1);
	a.op(ADDQ, p0, s2, 1);
	a.call("BzRebuildSlotOf");
	leave();

	// -- BzRemoveCrate -------------------------------------------------------
	a.label("BzRemoveCrate");
	enter();
	ldw(r0, StCrateInSlots);
	ifZero(r0, "RemDone");
	ldw(s2, StBaseCount);
	a.op(SLLi, r0, s2, 2);
	a.op(ADD, r0, r0, s0);
	a.immediate(LDWd, s3, r0, static_cast<int32_t>(StSlotOf));
	a.op(MOV, p0, s3);
	a.call("BzSlotPtr");
	a.op(MOV, p0, r0);
	a.immediate(ADDi, p1, r0, static_cast<int32_t>(G.stride));
	a.op(SUB, r0, s2, s3);
	a.op(MULQ, r0, r0, static_cast<uint8_t>(G.stride));
	a.op(SYSCPY, p0, p1, r0);
	a.op(SLLi, r0, s3, 2);
	a.op(ADD, r0, r0, s0);
	a.immediate(ADDi, p0, r0, static_cast<int32_t>(StIdentity));
	a.immediate(ADDi, p1, p0, 4);
	a.op(SUB, r0, s2, s3);
	a.op(SLLi, r0, r0, 2);
	a.op(SYSCPY, p0, p1, r0);
	a.pool(LDI, r0, zero, P.car);
	a.immediate(STHd, s2, r0, static_cast<int32_t>(G.countOff));
	stwK(StCrateInSlots, 0);
	a.op(MOV, p0, s2);
	a.call("BzRebuildSlotOf");
	a.label("RemDone");
	leave();

	// -- BzRaceInit ----------------------------------------------------------
	a.label("BzRaceInit");
	enter();
	ldw(s1, StCar);
	a.immediate(LDBHd, s2, s1, static_cast<int32_t>(G.countOff));
	a.ldq(r1, 7);
	a.branch(BGT, s2, r1, "InitDisable");
	a.branch(BLT, s2, zero, "InitDisable");
	// Zero the per-race region, keeping Car, SoundReady and the handles.
	a.immediate(ADDi, p0, s0, 8);
	a.ldq(p1, 0);
	ldk(r0, 108);
	a.op(SYSSET, p0, p1, r0);
	a.immediate(ADDi, p0, s0, static_cast<int32_t>(StKeys));
	ldk(r0, static_cast<int32_t>(StHandles - StKeys));
	a.op(SYSSET, p0, p1, r0);
	a.immediate(ADDi, p0, s0, static_cast<int32_t>(StEnt));
	ldk(r0, static_cast<int32_t>(StateSize - StEnt));
	a.op(SYSSET, p0, p1, r0);
	stw(s2, StBaseCount);
	stwK(StInRace, 1);
	stwK(StCrateTimer, 70);
	stwK(StLockTarget, -1);
	stwK(StTargetedBy, -1);
	a.ldq(s3, 0);
	a.label("InitEnt");
	a.ldq(r1, static_cast<int16_t>(MaxEntities));
	a.branch(BGE, s3, r1, "InitEntDone");
	a.op(MOV, p0, s3);
	a.call("BzEntPtr");
	a.op(MOV, s4, r0);
	ldk(r0, -100);
	a.immediate(STWd, r0, s4, static_cast<int32_t>(EntSeenFrame));
	a.op(SLLi, r0, s3, 2);
	a.pool(LDI, r1, zero, P.misc);
	a.op(ADD, r0, r0, r1);
	a.immediate(LDWd, r0, r0, static_cast<int32_t>(MiscColors));
	a.immediate(STWd, r0, s4, static_cast<int32_t>(EntColor));
	ldw(r1, StBaseCount);
	a.branch(BGE, s3, r1, "InitEntNext");
	a.op(MOV, p0, s3);
	a.call("BzSlotPtr");
	a.immediate(LDWd, r0, r0, 4);
	a.immediate(STWd, r0, s4, static_cast<int32_t>(EntLastDist));
	a.label("InitEntNext");
	a.op(SLLi, r0, s3, 2);
	a.op(ADD, r0, r0, s0);
	a.immediate(STWd, s3, r0, static_cast<int32_t>(StIdentity));
	a.immediate(STWd, s3, r0, static_cast<int32_t>(StSlotOf));
	a.op(ADDQ, s3, s3, 1);
	a.jump("InitEnt");
	a.label("InitEntDone");
	// The crate entity wears the reserved eighth tint.
	ldw(p0, StBaseCount);
	a.call("BzEntPtr");
	a.pool(LDI, r1, zero, P.misc);
	a.immediate(LDWd, r1, r1, static_cast<int32_t>(MiscColors + 28));
	a.immediate(STWd, r1, r0, static_cast<int32_t>(EntColor));
	ldw(r0, StSoundReady);
	ifNotZero(r0, "InitDone");
	a.callPool(P.soundInit);
	for (uint32_t sound = 0; sound < SoundCount; ++sound)
	{
		a.pool(LDI, p0, zero, P.waves[sound]);
		a.callPool(P.soundHandle);
		stw(r0, StHandles + sound * 4);
	}
	stwK(StSoundReady, 1);
	a.jump("InitDone");
	a.label("InitDisable");
	stwK(StInRace, 2);
	a.label("InitDone");
	leave();

	// -- BzCrateLogic --------------------------------------------------------
	a.label("BzCrateLogic");
	enter();
	ldw(s1, StCar);
	ldw(r0, StCrateActive);
	ifZero(r0, "CrInactive");
	// Player pickup: distance window plus lane match.
	ldw(r0, StPlayerDead);
	ifNotZero(r0, "CrAiPick");
	a.immediate(LDWd, r0, s1, 4);
	ldw(r1, StCrateDist);
	a.op(SUB, s2, r0, r1);
	a.branch(BLT, s2, zero, "CrAiPick");
	ldk(r0, 0x20000);
	a.branch(BGE, s2, r0, "CrPassed");
	a.immediate(LDWd, r0, s1, static_cast<int32_t>(G.laneOff));
	ldw(r1, StCrateLane);
	a.op(SUB, r0, r0, r1);
	a.branch(BGE, r0, zero, "CrAbs");
	a.op(NEG, r0, r0);
	a.label("CrAbs");
	ldk(r1, 59000);
	a.branch(BGT, r0, r1, "CrAiPick");
	ldw(r0, StAmmo);
	a.op(ADDQ, r0, r0, 3);
	a.ldq(r1, 6);
	a.branch(BLE, r0, r1, "CrAmmoOk");
	a.op(MOV, r0, r1);
	a.label("CrAmmoOk");
	stw(r0, StAmmo);
	a.ldq(p0, SndPickup);
	a.ldq(p1, 3);
	a.call("BzPlaySound");
	ldk(p0, static_cast<int32_t>(T.bazooka));
	a.ldq(p1, 25);
	a.call("BzMessage");
	stwK(StCrateActive, 0);
	stwK(StCrateTimer, 150);
	a.jump("CrDone");
	a.label("CrPassed");
	ldk(r0, 0x60000);
	a.branch(BLT, s2, r0, "CrAiPick");
	stwK(StCrateActive, 0);
	stwK(StCrateTimer, 20);
	a.jump("CrDone");
	a.label("CrAiPick");
	a.ldq(s3, 0);
	a.label("CrApLoop");
	ldw(r0, StBaseCount);
	a.branch(BGE, s3, r0, "CrDone");
	a.op(MOV, p0, s3);
	a.call("BzEntPtr");
	a.op(MOV, s4, r0);
	a.immediate(LDWd, r0, s4, static_cast<int32_t>(EntDead));
	ifNotZero(r0, "CrApNext");
	a.immediate(LDWd, r0, s4, static_cast<int32_t>(EntAmmo));
	ifNotZero(r0, "CrApNext");
	a.op(SLLi, r0, s3, 2);
	a.op(ADD, r0, r0, s0);
	a.immediate(LDWd, p0, r0, static_cast<int32_t>(StSlotOf));
	a.call("BzSlotPtr");
	a.op(MOV, s5, r0);
	a.immediate(LDWd, r0, s5, 4);
	ldw(r1, StCrateDist);
	a.op(SUB, r0, r0, r1);
	a.branch(BLT, r0, zero, "CrApNext");
	ldk(r1, 0x20000);
	a.branch(BGE, r0, r1, "CrApNext");
	a.immediate(LDWd, r0, s5, 0);
	ldw(r1, StCrateLane);
	a.op(SUB, r0, r0, r1);
	a.branch(BGE, r0, zero, "CrApAbs");
	a.op(NEG, r0, r0);
	a.label("CrApAbs");
	ldk(r1, 45000);
	a.branch(BGT, r0, r1, "CrApNext");
	a.ldq(r0, 1);
	a.immediate(STWd, r0, s4, static_cast<int32_t>(EntAmmo));
	a.ldq(r0, 0);
	a.immediate(STWd, r0, s4, static_cast<int32_t>(EntAiState));
	a.ldq(p0, 40);
	a.call("BzRand");
	a.op(ADDQ, r0, r0, 25);
	a.immediate(STWd, r0, s4, static_cast<int32_t>(EntAiTimer));
	ldk(p0, static_cast<int32_t>(T.rivalArmed));
	a.ldq(p1, 20);
	a.call("BzMessage");
	stwK(StCrateActive, 0);
	stwK(StCrateTimer, 150);
	a.jump("CrDone");
	a.label("CrApNext");
	a.op(ADDQ, s3, s3, 1);
	a.jump("CrApLoop");
	a.label("CrInactive");
	ldw(r0, StCrateTimer);
	a.op(ADDQ, r0, r0, static_cast<uint8_t>(-1));
	stw(r0, StCrateTimer);
	a.branch(BGT, r0, zero, "CrDone");
	a.ldq(p0, 7);
	a.call("BzRand");
	a.op(ADDQ, r0, r0, 7);
	a.op(SLLi, s2, r0, 17);
	a.immediate(LDWd, r0, s1, 4);
	a.immediate(ANDi, r0, r0, -131072);
	a.op(ADD, r0, r0, s2);
	a.immediate(ADDi, r0, r0, 0x10000);
	stw(r0, StCrateDist);
	a.ldq(p0, 3);
	a.call("BzRand");
	a.op(SLLi, r0, r0, 2);
	a.pool(LDI, r1, zero, P.misc);
	a.op(ADD, r0, r0, r1);
	a.immediate(LDWd, r0, r0, static_cast<int32_t>(MiscLanes));
	stw(r0, StCrateLane);
	stwK(StCrateActive, 1);
	a.label("CrDone");
	leave();

	// -- BzPlayerAim ---------------------------------------------------------
	a.label("BzPlayerAim");
	enter();
	ldw(s1, StCar);
	ldw(r0, StPlayerDead);
	ifZero(r0, "PaAlive");
	stwK(StAiming, 0);
	a.jump("PaDone");
	a.label("PaAlive");
	ldw(s2, StKeys);
	ldw(s3, StPrevKeys);
	a.op(NOT, s4, s3);
	a.op(AND, s4, s4, s2);   // fresh presses
	ldw(r0, StAiming);
	ifNotZero(r0, "PaActive");
	a.immediate(ANDi, r0, s4, static_cast<int32_t>(KeyFire2));
	ifZero(r0, "PaDone");
	ldw(r0, StAmmo);
	a.branch(BLE, r0, zero, "PaDone");
	stwK(StAiming, 1);
	stwK(StLockTarget, -1);
	stwK(StLockFrames, 0);
	stwK(StCrossX, 64);
	stwK(StCrossY, 95);
	stwK(StAimTimeout, 150);
	a.ldq(p0, SndBeep);
	a.ldq(p1, 1);
	a.call("BzPlaySound");
	a.jump("PaDone");
	a.label("PaActive");
	ldw(r0, StAimTimeout);
	a.op(ADDQ, r0, r0, static_cast<uint8_t>(-1));
	stw(r0, StAimTimeout);
	a.branch(BGT, r0, zero, "PaTimeoutOk");
	stwK(StAiming, 0);
	a.jump("PaDone");
	a.label("PaTimeoutOk");
	ldw(s5, StLockTarget);
	a.branch(BLT, s5, zero, "PaNoLock");
	a.op(MOV, p0, s5);
	a.call("BzEntPtr");
	a.op(MOV, fp, r0);
	a.immediate(LDWd, r0, fp, static_cast<int32_t>(EntDead));
	ifNotZero(r0, "PaUnlock");
	ifStale(fp, "PaUnlock");
	rectCenter(fp, EntRectX, EntRectW, r0);
	stw(r0, StCrossX);
	rectCenter(fp, EntRectY, EntRectH, r0);
	stw(r0, StCrossY);
	ldw(r0, StLockFrames);
	a.op(ADDQ, r0, r0, 1);
	stw(r0, StLockFrames);
	a.immediate(DIVi, r1, r0, 6);
	a.op(MULQ, r1, r1, 6);
	a.op(SUB, r1, r0, r1);
	ifNotZero(r1, "PaFire");
	a.ldq(p0, SndBeep);
	a.ldq(p1, 1);
	a.call("BzPlaySound");
	a.jump("PaFire");
	a.label("PaUnlock");
	stwK(StLockTarget, -1);
	a.label("PaNoLock");
	// Free crosshair movement on the arrow keys.
	ldw(r0, StCrossX);
	a.immediate(ANDi, r1, s2, static_cast<int32_t>(KeyLeft));
	ifZero(r1, "PaNotLeft");
	a.op(ADDQ, r0, r0, static_cast<uint8_t>(-3));
	a.label("PaNotLeft");
	a.immediate(ANDi, r1, s2, static_cast<int32_t>(KeyRight));
	ifZero(r1, "PaNotRight");
	a.op(ADDQ, r0, r0, 3);
	a.label("PaNotRight");
	a.ldq(r1, 6);
	a.branch(BGE, r0, r1, "PaClampX1");
	a.op(MOV, r0, r1);
	a.label("PaClampX1");
	a.ldq(r1, 121);
	a.branch(BLE, r0, r1, "PaClampX2");
	a.op(MOV, r0, r1);
	a.label("PaClampX2");
	stw(r0, StCrossX);
	ldw(r0, StCrossY);
	a.immediate(ANDi, r1, s2, static_cast<int32_t>(KeyUp));
	ifZero(r1, "PaNotUp");
	a.op(ADDQ, r0, r0, static_cast<uint8_t>(-3));
	a.label("PaNotUp");
	a.immediate(ANDi, r1, s2, static_cast<int32_t>(KeyDown));
	ifZero(r1, "PaNotDown");
	a.op(ADDQ, r0, r0, 3);
	a.label("PaNotDown");
	a.ldq(r1, 16);
	a.branch(BGE, r0, r1, "PaClampY1");
	a.op(MOV, r0, r1);
	a.label("PaClampY1");
	a.ldq(r1, 150);
	a.branch(BLE, r0, r1, "PaClampY2");
	a.op(MOV, r0, r1);
	a.label("PaClampY2");
	stw(r0, StCrossY);
	// Acquire: crosshair inside a live rival's screen rectangle.
	a.ldq(s5, 0);
	a.label("PaLkLoop");
	ldw(r0, StBaseCount);
	a.branch(BGE, s5, r0, "PaFire");
	a.op(MOV, p0, s5);
	a.call("BzEntPtr");
	a.op(MOV, fp, r0);
	a.immediate(LDWd, r0, fp, static_cast<int32_t>(EntDead));
	ifNotZero(r0, "PaLkNext");
	ifStale(fp, "PaLkNext");
	ldw(r1, StCrossX);
	a.immediate(LDWd, r0, fp, static_cast<int32_t>(EntRectX));
	a.op(ADDQ, r0, r0, static_cast<uint8_t>(-3));
	a.branch(BLT, r1, r0, "PaLkNext");
	a.immediate(LDWd, g0, fp, static_cast<int32_t>(EntRectW));
	a.op(ADD, r0, r0, g0);
	a.op(ADDQ, r0, r0, 6);
	a.branch(BGT, r1, r0, "PaLkNext");
	ldw(r1, StCrossY);
	a.immediate(LDWd, r0, fp, static_cast<int32_t>(EntRectY));
	a.op(ADDQ, r0, r0, static_cast<uint8_t>(-3));
	a.branch(BLT, r1, r0, "PaLkNext");
	a.immediate(LDWd, g0, fp, static_cast<int32_t>(EntRectH));
	a.op(ADD, r0, r0, g0);
	a.op(ADDQ, r0, r0, 6);
	a.branch(BGT, r1, r0, "PaLkNext");
	stw(s5, StLockTarget);
	stwK(StLockFrames, 0);
	a.ldq(p0, SndBeep);
	a.ldq(p1, 2);
	a.call("BzPlaySound");
	a.jump("PaFire");
	a.label("PaLkNext");
	a.op(ADDQ, s5, s5, 1);
	a.jump("PaLkLoop");
	a.label("PaFire");
	a.immediate(ANDi, r0, s4, static_cast<int32_t>(KeyFire2));
	ifZero(r0, "PaDone");
	a.call("BzAllocRocket");
	a.op(MOV, s5, r0);
	ifZero(s5, "PaDone");
	a.ldq(r0, 1);
	a.immediate(STWd, r0, s5, static_cast<int32_t>(RkActive));
	ldk(r0, ShooterPlayer);
	a.immediate(STWd, r0, s5, static_cast<int32_t>(RkShooter));
	ldw(r0, StLockTarget);
	a.immediate(STWd, r0, s5, static_cast<int32_t>(RkTarget));
	a.ldq(r0, 0);
	a.immediate(STWd, r0, s5, static_cast<int32_t>(RkFrames));
	a.immediate(STWd, r0, s5, static_cast<int32_t>(RkLaunchLane));
	a.ldq(r0, 10);
	a.immediate(STWd, r0, s5, static_cast<int32_t>(RkTotal));
	a.ldq(r0, 64);
	a.immediate(STWd, r0, s5, static_cast<int32_t>(RkFromX));
	ldk(r0, 138);
	a.immediate(STWd, r0, s5, static_cast<int32_t>(RkFromY));
	ldw(r0, StCrossX);
	a.immediate(STWd, r0, s5, static_cast<int32_t>(RkToX));
	ldw(r0, StCrossY);
	a.immediate(STWd, r0, s5, static_cast<int32_t>(RkToY));
	ldw(r0, StAmmo);
	a.op(ADDQ, r0, r0, static_cast<uint8_t>(-1));
	stw(r0, StAmmo);
	stwK(StAiming, 0);
	stwK(StShakeFrames, 2);
	a.ldq(p0, SndLaunch);
	a.ldq(p1, 4);
	a.call("BzPlaySound");
	a.label("PaDone");
	leave();

	// -- BzAiLogic: rivals arm themselves, aim, and fire ---------------------
	a.label("BzAiLogic");
	enter();
	ldw(s1, StCar);
	a.ldq(s2, 0);
	a.label("AiLoop");
	ldw(r0, StBaseCount);
	a.branch(BGE, s2, r0, "AiDone");
	a.op(MOV, p0, s2);
	a.call("BzEntPtr");
	a.op(MOV, s3, r0);
	a.immediate(LDWd, r0, s3, static_cast<int32_t>(EntDead));
	ifNotZero(r0, "AiNext");
	a.immediate(LDWd, r0, s3, static_cast<int32_t>(EntAiState));
	a.ldq(r1, 1);
	a.branch(BEQ, r0, r1, "AiAiming");
	a.ldq(r1, 2);
	a.branch(BEQ, r0, r1, "AiCool");
	// State 0: wait, then look for a victim one to ten segments ahead.
	a.immediate(LDWd, r0, s3, static_cast<int32_t>(EntAmmo));
	a.branch(BLE, r0, zero, "AiNext");
	a.immediate(LDWd, r0, s3, static_cast<int32_t>(EntAiTimer));
	a.op(ADDQ, r0, r0, static_cast<uint8_t>(-1));
	a.immediate(STWd, r0, s3, static_cast<int32_t>(EntAiTimer));
	a.branch(BGT, r0, zero, "AiNext");
	a.op(SLLi, r0, s2, 2);
	a.op(ADD, r0, r0, s0);
	a.immediate(LDWd, p0, r0, static_cast<int32_t>(StSlotOf));
	a.call("BzSlotPtr");
	a.immediate(LDWd, s4, r0, 4);
	ldk(s5, -3);
	ldk(fp, 0x3fffffff);
	ldw(r0, StPlayerDead);
	ifNotZero(r0, "AiSkipPlayer");
	a.immediate(LDWd, r0, s1, 4);
	a.op(SUB, r0, r0, s4);
	a.branch(BGE, r0, zero, "AiPlAbs");
	a.op(NEG, r0, r0);
	a.label("AiPlAbs");
	ldk(r1, 0x18000);
	a.branch(BLT, r0, r1, "AiSkipPlayer");
	ldk(r1, 0x180000);
	a.branch(BGT, r0, r1, "AiSkipPlayer");
	// Rivals hold a grudge: the player counts as a quarter closer.
	a.op(SRAi, r1, r0, 2);
	a.op(SUB, r0, r0, r1);
	ldk(s5, ShooterPlayer);
	a.op(MOV, fp, r0);
	a.label("AiSkipPlayer");
	a.ldq(s6, 0);
	a.label("AiCand");
	ldw(r0, StBaseCount);
	a.branch(BGE, s6, r0, "AiCandDone");
	a.branch(BEQ, s6, s2, "AiCandNext");
	a.op(MOV, p0, s6);
	a.call("BzEntPtr");
	a.immediate(LDWd, r0, r0, static_cast<int32_t>(EntDead));
	ifNotZero(r0, "AiCandNext");
	a.op(SLLi, r0, s6, 2);
	a.op(ADD, r0, r0, s0);
	a.immediate(LDWd, p0, r0, static_cast<int32_t>(StSlotOf));
	a.call("BzSlotPtr");
	a.immediate(LDWd, r0, r0, 4);
	a.op(SUB, r0, r0, s4);
	a.branch(BGE, r0, zero, "AiCandAbs");
	a.op(NEG, r0, r0);
	a.label("AiCandAbs");
	ldk(r1, 0x18000);
	a.branch(BLT, r0, r1, "AiCandNext");
	ldk(r1, 0x180000);
	a.branch(BGT, r0, r1, "AiCandNext");
	a.branch(BGE, r0, fp, "AiCandNext");
	a.op(MOV, s5, s6);
	a.op(MOV, fp, r0);
	a.label("AiCandNext");
	a.op(ADDQ, s6, s6, 1);
	a.jump("AiCand");
	a.label("AiCandDone");
	ldk(r0, -3);
	a.branch(BEQ, s5, r0, "AiRetry");
	a.immediate(STWd, s5, s3, static_cast<int32_t>(EntAiTarget));
	a.ldq(r0, 1);
	a.immediate(STWd, r0, s3, static_cast<int32_t>(EntAiState));
	a.ldq(r0, 25);
	a.immediate(STWd, r0, s3, static_cast<int32_t>(EntAiTimer));
	ldk(r0, ShooterPlayer);
	a.branch(BNE, s5, r0, "AiNext");
	stw(s2, StTargetedBy);
	a.ldq(p0, SndAlarm);
	a.ldq(p1, 2);
	a.call("BzPlaySound");
	ldk(p0, static_cast<int32_t>(T.incoming));
	a.ldq(p1, 20);
	a.call("BzMessage");
	a.jump("AiNext");
	a.label("AiRetry");
	a.ldq(r0, 20);
	a.immediate(STWd, r0, s3, static_cast<int32_t>(EntAiTimer));
	a.jump("AiNext");
	// State 1: locked on, counting down to launch.
	a.label("AiAiming");
	a.immediate(LDWd, s4, s3, static_cast<int32_t>(EntAiTarget));
	ldk(r0, ShooterPlayer);
	a.branch(BEQ, s4, r0, "AiAimPlayer");
	a.op(MOV, p0, s4);
	a.call("BzEntPtr");
	a.immediate(LDWd, r0, r0, static_cast<int32_t>(EntDead));
	ifZero(r0, "AiAimTick");
	a.ldq(r0, 0);
	a.immediate(STWd, r0, s3, static_cast<int32_t>(EntAiState));
	a.jump("AiNext");
	a.label("AiAimPlayer");
	ldw(r0, StPlayerDead);
	ifZero(r0, "AiAimTick");
	a.ldq(r0, 0);
	a.immediate(STWd, r0, s3, static_cast<int32_t>(EntAiState));
	stwK(StTargetedBy, -1);
	a.jump("AiNext");
	a.label("AiAimTick");
	a.immediate(LDWd, r0, s3, static_cast<int32_t>(EntAiTimer));
	a.op(ADDQ, r0, r0, static_cast<uint8_t>(-1));
	a.immediate(STWd, r0, s3, static_cast<int32_t>(EntAiTimer));
	a.op(MOV, s6, r0);
	a.branch(BLE, s6, zero, "AiFire");
	ldk(r0, ShooterPlayer);
	a.branch(BNE, s4, r0, "AiNext");
	a.immediate(DIVi, r0, s6, 8);
	a.op(MULQ, r0, r0, 8);
	a.op(SUB, r0, s6, r0);
	ifNotZero(r0, "AiNext");
	a.ldq(p0, SndAlarm);
	a.ldq(p1, 2);
	a.call("BzPlaySound");
	a.jump("AiNext");
	a.label("AiFire");
	a.call("BzAllocRocket");
	a.op(MOV, s6, r0);
	ifNotZero(s6, "AiFireGo");
	a.ldq(r0, 2);
	a.immediate(STWd, r0, s3, static_cast<int32_t>(EntAiState));
	a.ldq(r0, 20);
	a.immediate(STWd, r0, s3, static_cast<int32_t>(EntAiTimer));
	a.jump("AiNext");
	a.label("AiFireGo");
	a.ldq(r0, 1);
	a.immediate(STWd, r0, s6, static_cast<int32_t>(RkActive));
	a.immediate(STWd, s2, s6, static_cast<int32_t>(RkShooter));
	a.immediate(STWd, s4, s6, static_cast<int32_t>(RkTarget));
	a.ldq(r0, 0);
	a.immediate(STWd, r0, s6, static_cast<int32_t>(RkFrames));
	a.ldq(r0, 10);
	a.immediate(STWd, r0, s6, static_cast<int32_t>(RkTotal));
	ifStale(s3, "AiFromHidden");
	rectCenter(s3, EntRectX, EntRectW, r0);
	a.immediate(STWd, r0, s6, static_cast<int32_t>(RkFromX));
	rectCenter(s3, EntRectY, EntRectH, r0);
	a.immediate(STWd, r0, s6, static_cast<int32_t>(RkFromY));
	a.jump("AiFromDone");
	a.label("AiFromHidden");
	a.op(SLLi, r0, s2, 2);
	a.op(ADD, r0, r0, s0);
	a.immediate(LDWd, p0, r0, static_cast<int32_t>(StSlotOf));
	a.call("BzSlotPtr");
	a.immediate(LDWd, r0, r0, 4);
	a.immediate(LDWd, r1, s1, 4);
	a.branch(BGE, r0, r1, "AiFromAhead");
	a.ldq(r0, 64);
	a.immediate(STWd, r0, s6, static_cast<int32_t>(RkFromX));
	ldk(r0, 172);
	a.immediate(STWd, r0, s6, static_cast<int32_t>(RkFromY));
	a.jump("AiFromDone");
	a.label("AiFromAhead");
	a.ldq(r0, 64);
	a.immediate(STWd, r0, s6, static_cast<int32_t>(RkFromX));
	a.ldq(r0, 44);
	a.immediate(STWd, r0, s6, static_cast<int32_t>(RkFromY));
	a.label("AiFromDone");
	ldk(r0, ShooterPlayer);
	a.branch(BNE, s4, r0, "AiToCar");
	a.ldq(r0, 64);
	a.immediate(STWd, r0, s6, static_cast<int32_t>(RkToX));
	ldk(r0, 140);
	a.immediate(STWd, r0, s6, static_cast<int32_t>(RkToY));
	a.immediate(LDWd, r0, s1, static_cast<int32_t>(G.laneOff));
	a.immediate(STWd, r0, s6, static_cast<int32_t>(RkLaunchLane));
	a.jump("AiToDone");
	a.label("AiToCar");
	a.op(MOV, p0, s4);
	a.call("BzEntPtr");
	a.op(MOV, p1, r0);
	a.immediate(LDWd, r0, p1, static_cast<int32_t>(EntSeenFrame));
	a.op(ADDQ, r0, r0, 3);
	ldw(r1, StFlipFrame);
	a.branch(BLT, r0, r1, "AiToCarHidden");
	rectCenter(p1, EntRectX, EntRectW, r0);
	a.immediate(STWd, r0, s6, static_cast<int32_t>(RkToX));
	rectCenter(p1, EntRectY, EntRectH, r0);
	a.immediate(STWd, r0, s6, static_cast<int32_t>(RkToY));
	a.jump("AiToLane");
	a.label("AiToCarHidden");
	a.ldq(r0, 64);
	a.immediate(STWd, r0, s6, static_cast<int32_t>(RkToX));
	a.ldq(r0, 60);
	a.immediate(STWd, r0, s6, static_cast<int32_t>(RkToY));
	a.label("AiToLane");
	a.op(SLLi, r0, s4, 2);
	a.op(ADD, r0, r0, s0);
	a.immediate(LDWd, p0, r0, static_cast<int32_t>(StSlotOf));
	a.call("BzSlotPtr");
	a.immediate(LDWd, r0, r0, 0);
	a.immediate(STWd, r0, s6, static_cast<int32_t>(RkLaunchLane));
	a.label("AiToDone");
	a.immediate(LDWd, r0, s3, static_cast<int32_t>(EntAmmo));
	a.op(ADDQ, r0, r0, static_cast<uint8_t>(-1));
	a.immediate(STWd, r0, s3, static_cast<int32_t>(EntAmmo));
	a.ldq(r0, 2);
	a.immediate(STWd, r0, s3, static_cast<int32_t>(EntAiState));
	a.ldq(r0, 45);
	a.immediate(STWd, r0, s3, static_cast<int32_t>(EntAiTimer));
	a.ldq(p0, SndLaunch);
	a.ldq(p1, 3);
	a.call("BzPlaySound");
	a.jump("AiNext");
	a.label("AiCool");
	a.immediate(LDWd, r0, s3, static_cast<int32_t>(EntAiTimer));
	a.op(ADDQ, r0, r0, static_cast<uint8_t>(-1));
	a.immediate(STWd, r0, s3, static_cast<int32_t>(EntAiTimer));
	a.branch(BGT, r0, zero, "AiNext");
	a.ldq(r0, 0);
	a.immediate(STWd, r0, s3, static_cast<int32_t>(EntAiState));
	a.ldq(r0, 15);
	a.immediate(STWd, r0, s3, static_cast<int32_t>(EntAiTimer));
	a.label("AiNext");
	a.op(ADDQ, s2, s2, 1);
	a.jump("AiLoop");
	a.label("AiDone");
	leave();

	// -- BzRockets: advance flights, resolve impacts -------------------------
	a.label("BzRockets");
	enter();
	ldw(s1, StCar);
	a.immediate(ADDi, s2, s0, static_cast<int32_t>(StRockets));
	a.ldq(s3, 0);
	a.label("RkLoop");
	a.ldq(r1, 4);
	a.branch(BGE, s3, r1, "RkDone");
	a.immediate(LDWd, r0, s2, static_cast<int32_t>(RkActive));
	ifZero(r0, "RkNext");
	a.immediate(LDWd, r0, s2, static_cast<int32_t>(RkFrames));
	a.op(ADDQ, r0, r0, 1);
	a.immediate(STWd, r0, s2, static_cast<int32_t>(RkFrames));
	a.op(MOV, s4, r0);
	a.immediate(LDWd, s5, s2, static_cast<int32_t>(RkTarget));
	ldk(r0, ShooterPlayer);
	a.branch(BNE, s5, r0, "RkToCar");
	a.ldq(r0, 64);
	a.immediate(STWd, r0, s2, static_cast<int32_t>(RkToX));
	a.immediate(LDHUd, r0, s1, static_cast<int32_t>(G.jumpOff));
	ldk(r1, 140);
	a.op(SUB, r0, r1, r0);
	a.immediate(STWd, r0, s2, static_cast<int32_t>(RkToY));
	a.jump("RkToUpd");
	a.label("RkToCar");
	a.branch(BLT, s5, zero, "RkToUpd");
	a.op(MOV, p0, s5);
	a.call("BzEntPtr");
	a.op(MOV, p1, r0);
	a.immediate(LDWd, r0, p1, static_cast<int32_t>(EntSeenFrame));
	a.op(ADDQ, r0, r0, 3);
	ldw(r1, StFlipFrame);
	a.branch(BLT, r0, r1, "RkToUpd");
	rectCenter(p1, EntRectX, EntRectW, r0);
	a.immediate(STWd, r0, s2, static_cast<int32_t>(RkToX));
	rectCenter(p1, EntRectY, EntRectH, r0);
	a.immediate(STWd, r0, s2, static_cast<int32_t>(RkToY));
	a.label("RkToUpd");
	a.immediate(LDWd, r0, s2, static_cast<int32_t>(RkTotal));
	a.branch(BLT, s4, r0, "RkNext");
	a.ldq(r0, 0);
	a.immediate(STWd, r0, s2, static_cast<int32_t>(RkActive));
	ldk(r0, ShooterPlayer);
	a.branch(BNE, s5, r0, "RkHitCar");
	// Impact on the player: dodged if the car left the aimed lane.
	stwK(StTargetedBy, -1);
	ldw(r0, StPlayerDead);
	ifNotZero(r0, "RkPoint");
	a.immediate(LDWd, r0, s1, static_cast<int32_t>(G.laneOff));
	a.immediate(LDWd, r1, s2, static_cast<int32_t>(RkLaunchLane));
	a.op(SUB, r0, r0, r1);
	a.branch(BGE, r0, zero, "RkAbs");
	a.op(NEG, r0, r0);
	a.label("RkAbs");
	ldk(r1, 90000);
	a.branch(BGE, r0, r1, "RkDodged");
	a.call("BzKillPlayer");
	a.jump("RkNext");
	a.label("RkDodged");
	a.immediate(LDWd, p0, s2, static_cast<int32_t>(RkToX));
	a.op(ADDQ, p0, p0, 20);
	a.immediate(LDWd, p1, s2, static_cast<int32_t>(RkToY));
	a.ldq(p2, 1);
	a.call("BzSpawnExplosion");
	a.ldq(p0, SndExplosion);
	a.ldq(p1, 4);
	a.call("BzPlaySound");
	a.jump("RkNext");
	a.label("RkHitCar");
	a.branch(BLT, s5, zero, "RkPoint");
	a.op(MOV, p0, s5);
	a.call("BzEntPtr");
	a.op(MOV, s6, r0);
	a.immediate(LDWd, r0, s6, static_cast<int32_t>(EntDead));
	ifNotZero(r0, "RkPoint");
	a.immediate(LDWd, r0, s2, static_cast<int32_t>(RkShooter));
	ldk(r1, ShooterPlayer);
	a.branch(BNE, r0, r1, "RkKill");
	// A player rocket needs the mark to still be on screen ("you still have
	// the target"); otherwise it detonates where it was headed.
	ifStale(s6, "RkPoint");
	a.op(MOV, p0, s5);
	a.call("BzKillCar");
	ldk(p0, static_cast<int32_t>(T.destroyed));
	a.ldq(p1, 25);
	a.call("BzMessage");
	a.jump("RkNext");
	a.label("RkKill");
	a.op(MOV, p0, s5);
	a.call("BzKillCar");
	a.jump("RkNext");
	a.label("RkPoint");
	a.immediate(LDWd, p0, s2, static_cast<int32_t>(RkToX));
	a.immediate(LDWd, p1, s2, static_cast<int32_t>(RkToY));
	a.ldq(p2, 1);
	a.call("BzSpawnExplosion");
	a.ldq(p0, SndExplosion);
	a.ldq(p1, 4);
	a.call("BzPlaySound");
	a.label("RkNext");
	a.op(ADDQ, s2, s2, static_cast<uint8_t>(RocketStride));
	a.op(ADDQ, s3, s3, 1);
	a.jump("RkLoop");
	a.label("RkDone");
	leave();

	// -- BzColors: publish per-slot tints for the render phase ---------------
	a.label("BzColors");
	enter();
	ldw(s2, StBaseCount);
	ldw(r0, StCrateInSlots);
	a.op(ADD, s2, s2, r0);
	a.ldq(s3, 0);
	a.label("ClLoop");
	a.branch(BGE, s3, s2, "ClDone");
	a.op(SLLi, r0, s3, 2);
	a.op(ADD, r0, r0, s0);
	a.immediate(LDWd, p0, r0, static_cast<int32_t>(StIdentity));
	a.call("BzEntPtr");
	a.immediate(LDWd, r1, r0, static_cast<int32_t>(EntColor));
	a.op(SLLi, r0, s3, 2);
	a.pool(LDI, p0, zero, P.colors);
	a.op(ADD, r0, r0, p0);
	a.immediate(STWd, r1, r0, 0);
	a.op(ADDQ, s3, s3, 1);
	a.jump("ClLoop");
	a.label("ClDone");
	leave();

	// -- BzRocketPos(p0 = rocket, p1 = step) -> r0 = x, r1 = y (leaf) --------
	a.label("BzRocketPos");
	a.op(STORE, zero, sp);
	a.branch(BGE, p1, zero, "RpOk");
	a.ldq(p1, 0);
	a.label("RpOk");
	a.immediate(LDWd, g0, p0, static_cast<int32_t>(RkTotal));
	a.immediate(LDWd, g1, p0, static_cast<int32_t>(RkFromX));
	a.immediate(LDWd, g2, p0, static_cast<int32_t>(RkToX));
	a.op(SUB, g2, g2, g1);
	a.op(MUL, g2, g2, p1);
	a.op(DIV, g2, g2, g0);
	a.op(ADD, r0, g1, g2);
	a.immediate(LDWd, g1, p0, static_cast<int32_t>(RkFromY));
	a.immediate(LDWd, g2, p0, static_cast<int32_t>(RkToY));
	a.op(SUB, g2, g2, g1);
	a.op(MUL, g2, g2, p1);
	a.op(DIV, g2, g2, g0);
	a.op(ADD, r1, g1, g2);
	a.op(RET, zero, sp);

	// -- BzShake: one-pixel-ish diagonal shudder through vCopyRect -----------
	a.label("BzShake");
	enter();
	a.ldq(r0, 0);
	stw(r0, StCopyDst);
	stw(r0, StCopySrc);
	a.ldq(r0, 256);
	a.immediate(STHd, r0, s0, static_cast<int32_t>(StCopyDst + 8));
	a.immediate(STHd, r0, s0, static_cast<int32_t>(StCopySrc + 8));
	ldw(r0, StFrame);
	a.immediate(ANDi, r0, r0, 1);
	ifZero(r0, "ShEven");
	a.ldq(r0, 4);
	a.immediate(STHd, r0, s0, static_cast<int32_t>(StCopyDst + 4));
	a.ldq(r0, 1);
	a.immediate(STHd, r0, s0, static_cast<int32_t>(StCopyDst + 6));
	a.ldq(r0, 0);
	a.immediate(STHd, r0, s0, static_cast<int32_t>(StCopySrc + 4));
	a.immediate(STHd, r0, s0, static_cast<int32_t>(StCopySrc + 6));
	a.jump("ShGo");
	a.label("ShEven");
	a.ldq(r0, 0);
	a.immediate(STHd, r0, s0, static_cast<int32_t>(StCopyDst + 4));
	a.immediate(STHd, r0, s0, static_cast<int32_t>(StCopyDst + 6));
	a.ldq(r0, 4);
	a.immediate(STHd, r0, s0, static_cast<int32_t>(StCopySrc + 4));
	a.ldq(r0, 1);
	a.immediate(STHd, r0, s0, static_cast<int32_t>(StCopySrc + 6));
	a.label("ShGo");
	a.immediate(ADDi, p0, s0, static_cast<int32_t>(StCopyDst));
	a.immediate(ADDi, p1, s0, static_cast<int32_t>(StCopySrc));
	a.ldq(p2, 125);
	a.ldq(p3, 158);
	a.callPool(P.copyRect);
	leave();

	// -- BzDrawText(p0 = address, p1 = x, p2 = y, p3 = glyph base) -----------
	a.label("BzDrawText");
	enter();
	a.op(MOV, s2, p0);
	a.op(MOV, s3, p1);
	a.op(MOV, s4, p2);
	a.op(MOV, s5, p3);
	a.label("DtLoop");
	a.immediate(LDBUd, r0, s2, 0);
	ifZero(r0, "DtDone");
	a.immediate(ADDi, r0, r0, -32);
	a.op(ADD, p2, s5, r0);
	a.op(MOV, p0, s3);
	a.op(MOV, p1, s4);
	a.call("BzDrawSprite");
	a.op(ADDQ, s3, s3, 6);
	a.op(ADDQ, s2, s2, 1);
	a.jump("DtLoop");
	a.label("DtDone");
	leave();

	// -- BzDrawTextCentered(p0 = text offset, p1 = y): shadowed --------------
	a.label("BzDrawTextCentered");
	enter();
	a.pool(LDI, r0, zero, P.text);
	a.op(ADD, s2, r0, p0);
	a.op(MOV, s4, p1);
	a.op(MOV, r0, s2);
	a.ldq(r1, 0);
	a.label("TcLen");
	a.immediate(LDBUd, g0, r0, 0);
	ifZero(g0, "TcLenDone");
	a.op(ADDQ, r0, r0, 1);
	a.op(ADDQ, r1, r1, 1);
	a.jump("TcLen");
	a.label("TcLenDone");
	a.op(MULQ, r1, r1, 3);
	a.ldq(r0, 64);
	a.op(SUB, s3, r0, r1);
	a.op(MOV, p0, s2);
	a.op(ADDQ, p1, s3, 1);
	a.op(ADDQ, p2, s4, 1);
	ldk(p3, static_cast<int32_t>(S.fontBlack));
	a.call("BzDrawText");
	a.op(MOV, p0, s2);
	a.op(MOV, p1, s3);
	a.op(MOV, p2, s4);
	ldk(p3, static_cast<int32_t>(S.fontWhite));
	a.call("BzDrawText");
	leave();

	// -- BzFlipOverlay: everything drawn on top of the finished frame --------
	a.label("BzFlipOverlay");
	enter();
	ldw(s1, StCar);
	a.ldq(p0, 0);
	a.ldq(p1, 0);
	a.ldq(p2, 127);
	a.ldq(p3, 159);
	a.callPool(P.setClip);
	a.ldq(p0, 1);   // MODE_TRANS for the overlay sprites
	a.callPool(P.setMode);
	stw(r0, StSavedMode);
	ldw(r0, StShakeFrames);
	a.branch(BLE, r0, zero, "FoNoShake");
	a.op(ADDQ, r0, r0, static_cast<uint8_t>(-1));
	stw(r0, StShakeFrames);
	a.call("BzShake");
	a.label("FoNoShake");
	ldw(r0, StFlashFrames);
	a.branch(BLE, r0, zero, "FoNoFlash");
	a.op(ADDQ, r0, r0, static_cast<uint8_t>(-1));
	stw(r0, StFlashFrames);
	ldk(p0, 0x7fff);
	a.call("BzSetColor");
	a.ldq(p0, 0);
	a.ldq(p1, 0);
	a.ldq(p2, 127);
	a.ldq(p3, 159);
	a.callPool(P.fillRect);
	a.label("FoNoFlash");
	ldw(r0, StPlayerDead);
	ifZero(r0, "FoNoWreck");
	ldw(r0, StFrame);
	a.op(SRAi, r0, r0, 1);
	a.immediate(DIVi, r1, r0, 3);
	a.op(MULQ, r1, r1, 3);
	a.op(SUB, r0, r0, r1);
	ldk(p2, static_cast<int32_t>(S.flameBase + 9));
	a.op(ADD, p2, p2, r0);
	a.ldq(p0, 64);
	a.immediate(LDHUd, g0, s1, static_cast<int32_t>(G.jumpOff));
	ldk(p1, 140);
	a.op(SUB, p1, p1, g0);
	a.call("BzDrawSprite");
	ldw(r0, StFrame);
	a.immediate(ANDi, r0, r0, 15);
	ldk(p1, 120);
	a.op(SUB, p1, p1, r0);
	ldw(r1, StFrame);
	a.op(SRAi, r1, r1, 3);
	a.immediate(ANDi, r1, r1, 3);
	a.ldq(p0, 58);
	a.op(ADD, p0, p0, r1);
	ldk(p2, static_cast<int32_t>(S.smokeBig));
	a.call("BzDrawSprite");
	a.label("FoNoWreck");
	// Rockets in flight (with a short smoke trail).
	a.immediate(ADDi, s2, s0, static_cast<int32_t>(StRockets));
	a.ldq(s3, 0);
	a.label("FoRkLoop");
	a.ldq(r1, 4);
	a.branch(BGE, s3, r1, "FoRkDone");
	a.immediate(LDWd, r0, s2, static_cast<int32_t>(RkActive));
	ifZero(r0, "FoRkNext");
	a.immediate(LDWd, r0, s2, static_cast<int32_t>(RkFrames));
	a.op(ADDQ, r0, r0, static_cast<uint8_t>(-3));
	a.branch(BLT, r0, zero, "FoRkBody");
	a.op(MOV, p0, s2);
	a.op(MOV, p1, r0);
	a.call("BzRocketPos");
	a.op(MOV, p0, r0);
	a.op(MOV, p1, r1);
	ldk(p2, static_cast<int32_t>(S.smokeSmall));
	a.call("BzDrawSprite");
	a.label("FoRkBody");
	a.op(MOV, p0, s2);
	a.immediate(LDWd, p1, s2, static_cast<int32_t>(RkFrames));
	a.call("BzRocketPos");
	a.op(MOV, s4, r0);
	a.op(MOV, s5, r1);
	a.immediate(LDWd, r0, s2, static_cast<int32_t>(RkToY));
	a.immediate(LDWd, r1, s2, static_cast<int32_t>(RkFromY));
	ldk(p2, static_cast<int32_t>(S.rocketUp));
	a.branch(BLE, r0, r1, "FoRkDir");
	ldk(p2, static_cast<int32_t>(S.rocketDown));
	a.label("FoRkDir");
	a.op(MOV, p0, s4);
	a.op(MOV, p1, s5);
	a.call("BzDrawSprite");
	a.label("FoRkNext");
	a.op(ADDQ, s2, s2, static_cast<uint8_t>(RocketStride));
	a.op(ADDQ, s3, s3, 1);
	a.jump("FoRkLoop");
	a.label("FoRkDone");
	// Explosion animations.
	a.immediate(ADDi, s2, s0, static_cast<int32_t>(StExplosions));
	a.ldq(s3, 0);
	a.label("FoExLoop");
	a.ldq(r1, 4);
	a.branch(BGE, s3, r1, "FoExDone");
	a.immediate(LDWd, s4, s2, static_cast<int32_t>(ExFrames));
	a.branch(BLE, s4, zero, "FoExNext");
	a.op(ADDQ, r0, s4, static_cast<uint8_t>(-1));
	a.immediate(STWd, r0, s2, static_cast<int32_t>(ExFrames));
	a.ldq(r0, 14);
	a.op(SUB, r0, r0, s4);
	a.op(SRAi, r0, r0, 1);
	a.ldq(r1, 6);
	a.branch(BLE, r0, r1, "FoExClamp");
	a.op(MOV, r0, r1);
	a.label("FoExClamp");
	a.immediate(LDWd, r1, s2, static_cast<int32_t>(ExSize));
	a.op(MULQ, r1, r1, 7);
	a.op(ADD, r0, r0, r1);
	ldk(p2, static_cast<int32_t>(S.explosionBase));
	a.op(ADD, p2, p2, r0);
	a.immediate(LDWd, p0, s2, static_cast<int32_t>(ExX));
	a.immediate(LDWd, p1, s2, static_cast<int32_t>(ExY));
	a.call("BzDrawSprite");
	a.label("FoExNext");
	a.op(ADDQ, s2, s2, static_cast<uint8_t>(ExStride));
	a.op(ADDQ, s3, s3, 1);
	a.jump("FoExLoop");
	a.label("FoExDone");
	// Crosshair, flashing while locked.
	ldw(r0, StAiming);
	ifZero(r0, "FoNoCross");
	ldw(r1, StLockTarget);
	ldk(p2, static_cast<int32_t>(S.crossWhite));
	a.branch(BLT, r1, zero, "FoCrossDraw");
	ldk(p2, static_cast<int32_t>(S.crossRed));
	ldw(r0, StFrame);
	a.immediate(DIVi, r1, r0, 3);
	a.immediate(ANDi, r1, r1, 1);
	ifZero(r1, "FoCrossDraw");
	ldk(p2, static_cast<int32_t>(S.crossWhite));
	a.label("FoCrossDraw");
	ldw(p0, StCrossX);
	ldw(p1, StCrossY);
	a.call("BzDrawSprite");
	a.label("FoNoCross");
	// Incoming-rocket warning.
	ldw(r0, StTargetedBy);
	a.branch(BLT, r0, zero, "FoNoWarn");
	ldw(r0, StFrame);
	a.immediate(DIVi, r1, r0, 3);
	a.immediate(ANDi, r1, r1, 1);
	ifZero(r1, "FoNoWarn");
	ldk(p0, 0x7c00);
	a.call("BzSetColor");
	a.ldq(p0, 0);
	a.ldq(p1, 0);
	a.ldq(p2, 127);
	a.ldq(p3, 1);
	a.callPool(P.fillRect);
	a.ldq(p0, 0);
	ldk(p1, 158);
	a.ldq(p2, 127);
	ldk(p3, 159);
	a.callPool(P.fillRect);
	a.ldq(p0, 0);
	a.ldq(p1, 0);
	a.ldq(p2, 1);
	ldk(p3, 159);
	a.callPool(P.fillRect);
	a.ldq(p0, 126);
	a.ldq(p1, 0);
	a.ldq(p2, 127);
	ldk(p3, 159);
	a.callPool(P.fillRect);
	ldk(p0, static_cast<int32_t>(T.incoming));
	a.ldq(p1, 34);
	a.call("BzDrawTextCentered");
	a.label("FoNoWarn");
	// Launcher HUD in the free top-left corner.
	ldw(s2, StAmmo);
	a.branch(BLE, s2, zero, "FoNoHud");
	a.ldq(p0, 2);
	a.ldq(p1, 2);
	ldk(p2, static_cast<int32_t>(S.hudIcon));
	a.call("BzDrawSprite");
	a.ldq(s3, 0);
	a.label("FoHudLoop");
	a.branch(BGE, s3, s2, "FoNoHud");
	a.op(MULQ, r0, s3, 6);
	a.ldq(p0, 29);
	a.op(ADD, p0, p0, r0);
	a.ldq(p1, 3);
	ldk(p2, static_cast<int32_t>(S.ammoIcon));
	a.call("BzDrawSprite");
	a.op(ADDQ, s3, s3, 1);
	a.jump("FoHudLoop");
	a.label("FoNoHud");
	ldw(r0, StMessageFrames);
	a.branch(BLE, r0, zero, "FoNoMsg");
	a.op(ADDQ, r0, r0, static_cast<uint8_t>(-1));
	stw(r0, StMessageFrames);
	ldw(p0, StMessageOff);
	a.ldq(p1, 56);
	a.call("BzDrawTextCentered");
	a.label("FoNoMsg");
	ldw(p0, StSavedMode);
	a.callPool(P.setMode);
	leave();

	// -- BzUpdate(p0 = car): everything simulated after the game's update ----
	a.label("BzUpdate");
	enter();
	a.pool(LDI, r0, zero, P.car);
	a.branch(BNE, p0, r0, "BzUpdRet");   // ignore secondary car copies
	a.op(MOV, s1, p0);
	stw(s1, StCar);
	a.immediate(LDBUd, r0, s1, static_cast<int32_t>(G.startedOff));
	a.ldq(r1, 1);
	a.branch(BNE, r0, r1, "BzUpdNotRacing");
	ldw(r0, StInRace);
	a.ldq(r1, 2);
	a.branch(BEQ, r0, r1, "BzUpdRet");
	ifNotZero(r0, "BzUpdReady");
	a.call("BzRaceInit");
	ldw(r0, StInRace);
	a.ldq(r1, 1);
	a.branch(BNE, r0, r1, "BzUpdRet");
	a.label("BzUpdReady");
	a.call("BzRemoveCrate");   // safety: catch-up updates can skip the flip
	ldw(r0, StFrame);
	a.op(ADDQ, r0, r0, 1);
	stw(r0, StFrame);
	a.callPool(P.origButton);   // raw keys, before any bazooka masking
	stw(r0, StKeys);
	// Give every mover its own speed back (the game keys speed to the slot
	// index), keep wrecks frozen, and refresh the derived segment fields.
	a.ldq(s3, 0);
	a.label("UpMove");
	ldw(r0, StBaseCount);
	a.branch(BGE, s3, r0, "UpMoveDone");
	a.op(MOV, p0, s3);
	a.call("BzSlotPtr");
	a.op(MOV, s4, r0);
	a.op(SLLi, r0, s3, 2);
	a.op(ADD, r0, r0, s0);
	a.immediate(LDWd, s5, r0, static_cast<int32_t>(StIdentity));
	a.op(MOV, p0, s5);
	a.call("BzEntPtr");
	a.op(MOV, s7, r0);
	a.immediate(LDWd, r0, s4, 4);
	a.immediate(LDWd, r1, s7, static_cast<int32_t>(EntLastDist));
	a.branch(BEQ, r0, r1, "UpMoveNext");
	a.immediate(LDWd, g0, s7, static_cast<int32_t>(EntDead));
	ifNotZero(g0, "UpFrozen");
	a.op(SUB, g1, s3, s5);
	a.immediate(MULi, g1, g1, static_cast<int32_t>(G.speedStep));
	a.op(ADD, r0, r0, g1);
	a.jump("UpStore");
	a.label("UpFrozen");
	a.op(MOV, r0, r1);
	a.label("UpStore");
	a.immediate(STWd, r0, s4, 4);
	a.immediate(STWd, r0, s7, static_cast<int32_t>(EntLastDist));
	a.op(MOV, p0, s4);
	a.call("BzResync");
	a.label("UpMoveNext");
	a.op(ADDQ, s3, s3, 1);
	a.jump("UpMove");
	a.label("UpMoveDone");
	a.call("BzSortCars");
	ldw(r0, StPlayerDead);
	ifZero(r0, "UpNoDeath");
	a.ldq(r0, 0);
	a.immediate(STWd, r0, s1, static_cast<int32_t>(G.targetSpeedOff));
	a.immediate(STWd, r0, s1, static_cast<int32_t>(G.speedOff));
	ldw(r0, StPlayerDeadFrames);
	a.op(ADDQ, r0, r0, static_cast<uint8_t>(-1));
	stw(r0, StPlayerDeadFrames);
	a.branch(BGT, r0, zero, "UpNoDeath");
	stwK(StPlayerDead, 0);
	ldk(p0, static_cast<int32_t>(T.go));
	a.ldq(p1, 15);
	a.call("BzMessage");
	a.label("UpNoDeath");
	a.call("BzCrateLogic");
	a.call("BzPlayerAim");
	a.call("BzAiLogic");
	a.call("BzRockets");
	ldw(r0, StCrateActive);
	ifZero(r0, "UpNoIns");
	a.call("BzInsertCrate");
	a.label("UpNoIns");
	a.call("BzColors");
	ldw(r0, StKeys);
	stw(r0, StPrevKeys);
	a.jump("BzUpdRet");
	a.label("BzUpdNotRacing");
	ldw(r0, StInRace);
	ifZero(r0, "BzUpdRet");
	a.call("BzRemoveCrate");
	stwK(StInRace, 0);
	stwK(StAiming, 0);
	stwK(StMessageFrames, 0);
	stwK(StTargetedBy, -1);
	stwK(StPlayerDead, 0);
	stwK(StAmmo, 0);
	stwK(StPendingSlot, 0);
	stwK(StShakeFrames, 0);
	stwK(StFlashFrames, 0);
	stwK(StCrateActive, 0);
	a.immediate(ADDi, p0, s0, static_cast<int32_t>(StRockets));
	a.ldq(p1, 0);
	ldk(r0, static_cast<int32_t>(StateSize - StRockets));
	a.op(SYSSET, p0, p1, r0);
	a.label("BzUpdRet");
	leave();

	// -- Hook wrappers -------------------------------------------------------
	a.label("BzUpdateWrapper");
	a.op(STORE, ra, s2);
	a.op(MOV, s1, p0);
	a.callPool(P.origUpdate);
	a.op(MOV, s2, r0);
	a.op(MOV, p0, s1);
	a.call("BzUpdate");
	a.op(MOV, r0, s2);
	a.op(RET, s3, s2);

	a.label("BzFlipWrapper");
	a.op(STORE, ra, s2);
	a.pool(LDI, s0, zero, P.state);
	ldw(r0, StInRace);
	a.ldq(r1, 1);
	a.branch(BNE, r0, r1, "FwSkip");
	ldw(r0, StFlipFrame);
	a.op(ADDQ, r0, r0, 1);
	stw(r0, StFlipFrame);
	a.call("BzFlipOverlay");
	a.call("BzRemoveCrate");   // the phantom rival exists only while drawing
	a.label("FwSkip");
	a.callPool(P.origFlip);
	a.op(RET, s3, s2);

	a.label("BzButtonWrapper");
	a.op(STORE, ra, s2);
	a.pool(LDI, s0, zero, P.state);
	a.callPool(P.origButton);
	a.op(MOV, s2, r0);
	ldw(r0, StInRace);
	a.ldq(r1, 1);
	a.branch(BNE, r0, r1, "BwDone");
	ldw(r0, StPlayerDead);
	ifZero(r0, "BwAlive");
	a.ldq(s2, 0);   // a wreck takes no input
	a.jump("BwDone");
	a.label("BwAlive");
	ldw(r0, StAiming);
	ifZero(r0, "BwNoAim");
	// Aiming: the arrows steer the crosshair; the car cruises straight on.
	a.immediate(ANDi, s2, s2, ~static_cast<int32_t>(KeyUp | KeyDown | KeyLeft |
		KeyRight | KeyFire2));
	a.immediate(ORi, s2, s2, static_cast<int32_t>(KeyUp));
	a.jump("BwDone");
	a.label("BwNoAim");
	ldw(r0, StAmmo);
	a.branch(BLE, r0, zero, "BwDone");
	a.immediate(ANDi, s2, s2, ~static_cast<int32_t>(KeyFire2));
	a.label("BwDone");
	a.op(MOV, r0, s2);
	a.op(RET, s3, s2);

	a.label("BzPaletteWrapper");
	a.op(STORE, ra, s2);
	a.pool(LDI, s0, zero, P.state);
	a.op(MOV, s2, p0);
	a.op(MOV, s3, p1);
	ldk(r0, static_cast<int32_t>(G.paletteIndex));
	a.branch(BNE, s2, r0, "PwPass");
	ldw(r0, StInRace);
	a.ldq(r1, 1);
	a.branch(BNE, r0, r1, "PwPass");
	ldw(r1, StBaseCount);
	ldw(r0, StCrateInSlots);
	a.op(ADD, r1, r1, r0);
	a.ldq(g0, 0);
	a.label("PwLoop");
	a.branch(BGE, g0, r1, "PwPass");
	a.op(SLLi, g1, g0, 2);
	a.pool(LDI, g2, zero, P.colors);
	a.op(ADD, g1, g1, g2);
	a.immediate(LDWd, g1, g1, 0);
	a.branch(BNE, g1, s3, "PwNext");
	a.op(ADDQ, g0, g0, 1);
	stw(g0, StPendingSlot);
	a.jump("PwPass");
	a.label("PwNext");
	a.op(ADDQ, g0, g0, 1);
	a.jump("PwLoop");
	a.label("PwPass");
	a.op(MOV, p0, s2);
	a.op(MOV, p1, s3);
	a.callPool(P.origPalette);
	a.op(RET, s3, s2);

	a.label("BzDrawObjectWrapper");
	a.op(STORE, ra, s6);
	a.pool(LDI, s0, zero, P.state);
	a.op(MOV, s2, p0);
	a.op(MOV, s3, p1);
	a.op(MOV, s4, p2);
	// The renderer leaves junk in the high halves of the coordinates; only the
	// low 16 bits are meaningful to vDrawObject.
	a.op(EXSH, s2, s2);
	a.op(EXSH, s3, s3);
	ldw(s5, StPendingSlot);
	stwK(StPendingSlot, 0);
	ifZero(s5, "DwPass");
	ldw(r0, StInRace);
	a.ldq(r1, 1);
	a.branch(BNE, r0, r1, "DwPass");
	a.op(ADDQ, s5, s5, static_cast<uint8_t>(-1));
	a.op(SLLi, r0, s5, 2);
	a.op(ADD, r0, r0, s0);
	a.immediate(LDWd, s5, r0, static_cast<int32_t>(StIdentity));   // entity
	a.op(MOV, p0, s5);
	a.call("BzEntPtr");
	a.op(MOV, s6, r0);
	a.immediate(LDBHd, r0, s4, 2);
	a.op(SUB, r0, s2, r0);
	a.immediate(STWd, r0, s6, static_cast<int32_t>(EntRectX));
	a.immediate(LDBHd, r0, s4, 4);
	a.op(SUB, r0, s3, r0);
	a.immediate(STWd, r0, s6, static_cast<int32_t>(EntRectY));
	a.immediate(LDHUd, fp, s4, 6);
	a.immediate(STWd, fp, s6, static_cast<int32_t>(EntRectW));
	a.immediate(LDHUd, r0, s4, 8);
	a.immediate(STWd, r0, s6, static_cast<int32_t>(EntRectH));
	ldw(r0, StFlipFrame);
	a.immediate(STWd, r0, s6, static_cast<int32_t>(EntSeenFrame));
	ldw(r0, StBaseCount);
	a.branch(BNE, s5, r0, "DwCar");
	// The phantom rival becomes the weapon crate, at the same footprint.
	a.ldq(r0, 0);
	a.ldq(r1, 12);
	a.branch(BLE, fp, r1, "DwCrateSize");
	a.ldq(r0, 1);
	a.ldq(r1, 17);
	a.branch(BLE, fp, r1, "DwCrateSize");
	a.ldq(r0, 2);
	a.ldq(r1, 24);
	a.branch(BLE, fp, r1, "DwCrateSize");
	a.ldq(r0, 3);
	a.label("DwCrateSize");
	a.op(SLLi, r0, r0, 1);
	ldw(r1, StFrame);
	a.op(SRAi, r1, r1, 3);
	a.immediate(ANDi, r1, r1, 1);
	a.op(ADD, r0, r0, r1);
	ldk(p2, static_cast<int32_t>(S.crateBase));
	a.op(ADD, p2, p2, r0);
	a.op(MOV, p0, s2);
	a.op(MOV, p1, s3);
	a.call("BzDrawSprite");
	a.jump("DwDone");
	a.label("DwCar");
	a.op(MOV, p0, s2);
	a.op(MOV, p1, s3);
	a.op(MOV, p2, s4);
	a.callPool(P.origDraw);
	a.immediate(LDWd, r0, s6, static_cast<int32_t>(EntDead));
	ifZero(r0, "DwDone");
	// Burning wreck: flames sized to the sprite, drifting smoke above.
	a.ldq(r0, 0);
	a.ldq(r1, 12);
	a.branch(BLE, fp, r1, "DwFlameSize");
	a.ldq(r0, 1);
	a.ldq(r1, 17);
	a.branch(BLE, fp, r1, "DwFlameSize");
	a.ldq(r0, 2);
	a.ldq(r1, 24);
	a.branch(BLE, fp, r1, "DwFlameSize");
	a.ldq(r0, 3);
	a.label("DwFlameSize");
	a.op(MULQ, r0, r0, 3);
	ldw(r1, StFrame);
	a.op(SRAi, r1, r1, 1);
	a.immediate(DIVi, g0, r1, 3);
	a.op(MULQ, g0, g0, 3);
	a.op(SUB, r1, r1, g0);
	a.op(ADD, r0, r0, r1);
	ldk(p2, static_cast<int32_t>(S.flameBase));
	a.op(ADD, p2, p2, r0);
	a.op(MOV, p0, s2);
	a.immediate(LDHUd, r1, s4, 8);
	a.op(SRAi, r1, r1, 1);
	a.op(SUB, p1, s3, r1);
	a.call("BzDrawSprite");
	a.immediate(LDHUd, r1, s4, 8);
	a.op(SUB, p1, s3, r1);
	a.op(ADDQ, p1, p1, static_cast<uint8_t>(-3));
	ldw(r0, StFrame);
	a.immediate(ANDi, r0, r0, 7);
	a.op(SUB, p1, p1, r0);
	ldw(r0, StFrame);
	a.op(SRAi, r0, r0, 2);
	a.immediate(ANDi, r0, r0, 3);
	a.op(ADDQ, r0, r0, static_cast<uint8_t>(-1));
	a.op(ADD, p0, s2, r0);
	ldk(p2, static_cast<int32_t>(S.smokeSmall));
	a.call("BzDrawSprite");
	a.jump("DwDone");
	a.label("DwPass");
	a.op(MOV, p0, s2);
	a.op(MOV, p1, s3);
	a.op(MOV, p2, s4);
	a.callPool(P.origDraw);
	a.label("DwDone");
	a.op(RET, s7, s6);

	return a.finish();
}

// ---------------------------------------------------------------------------
// Patch assembly.
// ---------------------------------------------------------------------------
std::vector<uint8_t> buildModdedMpn(std::vector<uint8_t> input)
{
	const MpnImage sourceImage = MpnImage::parse(input);
	const TargetDetection detection = builtInTargets().detect(sourceImage);
	if (detection.compatibility != TargetCompatibility::Compatible ||
		detection.profile == nullptr)
		throw std::runtime_error(detection.message);
	if (detection.profile->id() != "vrally2-rc14eu-m5")
		throw std::runtime_error("Bazooka mod does not support detected target '" +
			detection.profile->id() + "'");

	const VMGPHeader encryptedHeader = decodeVMGPHeader(input.data());
	std::string decryptError;
	if (!decryptCommercialCode(input, encryptedHeader, decryptError))
		throw std::runtime_error("Unable to decrypt the commercial code: " + decryptError);
	MpnImage decryptedImage = MpnImage::parse(input);
	const ResolvedTarget target = detection.profile->resolve(decryptedImage);
	const MpnHeader header = decryptedImage.header();
	PatchBuilder patch(std::move(decryptedImage));

	GameConstants game;
	game.startedOff = target.constant("game.car.started_offset");
	game.targetSpeedOff = target.constant("game.car.target_speed_offset");
	game.speedOff = target.constant("game.car.speed_offset");
	game.jumpOff = target.constant("game.car.jump_height_offset");
	game.segOff = target.constant("game.car.segment_offset");
	game.laneOff = target.constant("game.car.road_lane_offset");
	game.countOff = target.constant("game.car.opponent_count_offset");
	game.oppOff = target.constant("game.car.opponents_offset");
	game.stride = target.constant("game.opponent.stride");
	game.speedStep = target.constant("game.opponent.speed_step");
	game.paletteIndex = target.constant("game.opponent.palette_index");

	// Hooks. Reservations keep callable originals; wrappers are bound after
	// the program has been assembled.
	const CodeHook updateHook = patch.reserveCodeHook(target.pool("game.car.update"));
	const CodeHook flipHook = patch.reserveCodeHook(target.pool("os.graphics.flip_screen"));
	const CodeHook buttonHook = patch.reserveCodeHook(target.pool("os.input.get_button_data"));
	const CodeHook drawHook = patch.reserveCodeHook(target.pool("os.graphics.draw_object"));
	const CodeHook paletteHook =
		patch.reserveCodeHook(target.pool("os.graphics.set_palette_entry"));

	GuestPools pools;
	pools.origUpdate = updateHook.originalPoolId();
	pools.origFlip = flipHook.originalPoolId();
	pools.origButton = buttonHook.originalPoolId();
	pools.origDraw = drawHook.originalPoolId();
	pools.origPalette = paletteHook.originalPoolId();
	pools.fillRect = target.pool("os.graphics.fill_rect");
	pools.setClip = target.pool("os.graphics.set_clip_window");
	pools.setFore = target.pool("os.graphics.set_fore_color");
	pools.random = target.pool("os.system.get_random");
	pools.setMode = patch.importSyscall("vSetTransferMode");
	pools.copyRect = patch.importSyscall("vCopyRect");
	pools.soundInit = patch.importSyscall("vSoundInit");
	pools.soundHandle = patch.importSyscall("vSoundGetHandle");
	pools.soundCtrlEx = patch.importSyscall("vSoundCtrlEx");

	const SectionAllocation state = patch.allocateBss(StateSize, 4);
	pools.state = patch.addReference(state);
	pools.car = patch.addPoolEntry(PoolEntry::bss(target.bssOffset("game.car")));
	pools.trackLen = patch.addPoolEntry(PoolEntry::bss(target.bssOffset("game.track.length")));

	// The rival tint table moves into fresh data so a fourth (crate) and dead
	// (dark) tints exist; the original three colours are carried over.
	const PoolId gameColorsPool = target.pool("game.opponent.colors");
	const PoolEntry originalColorsEntry = patch.poolEntry(gameColorsPool);
	const uint32_t originalColorsOffset = originalColorsEntry.value;
	uint32_t originalColors[3] = {0, 0, 0};
	for (int index = 0; index < 3; ++index)
	{
		const size_t fileOffset = MpnHeaderSize + header.codeSize +
			originalColorsOffset + index * 4;
		if (fileOffset + 4 > input.size())
			throw std::runtime_error("Rival colour table lies outside the executable");
		originalColors[index] = readLittleU32(input.data() + fileOffset);
	}

	patch.alignData(4);
	std::vector<uint8_t> colorTable;
	for (int index = 0; index < 8; ++index)
		appendU32(colorTable, index < 3 ? originalColors[index] : 0);
	const SectionAllocation colorAlloc = patch.allocateData(colorTable);
	PoolEntry movedColors = originalColorsEntry;
	movedColors.value = colorAlloc.offset;
	patch.replacePoolEntry(gameColorsPool, movedColors);
	pools.colors = gameColorsPool;

	// Misc tables: crate lanes, sound durations (frames), entity tints.
	std::vector<uint8_t> misc;
	appendU32(misc, static_cast<uint32_t>(-81920));
	appendU32(misc, 0);
	appendU32(misc, 81920);
	const uint32_t durations[SoundCount] = {6, 2, 7, 10, 16, 23};
	for (uint32_t sound = 0; sound < SoundCount; ++sound)
		appendU32(misc, durations[sound]);
	for (int entity = 0; entity < 7; ++entity)
		appendU32(misc, entity < 3 ? originalColors[entity]
			: 0x00ff0000U | (0x7d1fU + static_cast<uint32_t>(entity) * 0x0020U));
	appendU32(misc, 0x00ff7c1f);   // crate tint, never drawn as a car
	patch.alignData(4);
	const SectionAllocation miscAlloc = patch.allocateData(misc);
	pools.misc = patch.addReference(miscAlloc);

	// HUD strings.
	TextOffsets text;
	std::vector<uint8_t> textBlob;
	auto addText = [&](const char* value) {
		const uint32_t offset = static_cast<uint32_t>(textBlob.size());
		textBlob.insert(textBlob.end(), value, value + std::strlen(value) + 1);
		return offset;
	};
	text.bazooka = addText("BAZOOKA!");
	text.rivalArmed = addText("RIVAL ARMED!");
	text.destroyed = addText("DESTROYED!");
	text.wrecked = addText("WRECKED!");
	text.incoming = addText("INCOMING!");
	text.go = addText("GO!");
	patch.alignData(4);
	const SectionAllocation textAlloc = patch.allocateData(textBlob);
	pools.text = patch.addReference(textAlloc);

	SpriteIds spriteIds;
	const std::vector<uint8_t> bank = buildSpriteBank(spriteIds);
	patch.alignData(4);
	const SectionAllocation bankAlloc = patch.allocateData(bank);
	pools.bank = patch.addReference(bankAlloc);

	const std::vector<uint8_t> waves[SoundCount] = {
		buildPickupSound(), buildBeepSound(), buildAlarmSound(),
		buildLaunchSound(), buildExplosionSound(), buildWreckedSound()
	};
	size_t waveBytes = 0;
	for (uint32_t sound = 0; sound < SoundCount; ++sound)
	{
		patch.alignData(4);
		const SectionAllocation waveAlloc = patch.allocateData(waves[sound]);
		pools.waves[sound] = patch.addReference(waveAlloc);
		waveBytes += waves[sound].size();
	}

	Assembler assembler(patch.nextCodeOffset(4));
	const PipProgram program = buildGuestCode(pools, game, spriteIds, text, assembler);
	const SectionAllocation guestCode = patch.allocateCode(program.bytes(), 4);
	if (guestCode.offset != header.codeSize)
		throw std::runtime_error("Unexpected padding before the bazooka guest code");
	patch.bindCodeHook(updateHook, program.symbolOffset("BzUpdateWrapper"));
	patch.bindCodeHook(flipHook, program.symbolOffset("BzFlipWrapper"));
	patch.bindCodeHook(buttonHook, program.symbolOffset("BzButtonWrapper"));
	patch.bindCodeHook(drawHook, program.symbolOffset("BzDrawObjectWrapper"));
	patch.bindCodeHook(paletteHook, program.symbolOffset("BzPaletteWrapper"));
	patch.markModified(target.profile().id(), "vrally2-bazooka");

	const std::vector<uint8_t> output = patch.serialize();
	std::cout << "Selected target: " << target.profile().displayName() << " ("
		<< target.profile().id() << ")\n";
	std::cout << "Guest program: " << program.bytes().size() << " bytes of PIP2, sprite bank "
		<< bank.size() << " bytes, " << SoundCount << " sound effects ("
		<< waveBytes << " bytes of PCM WAVE)\n";
	return output;
}

} // namespace

int main(int argc, char* argv[])
{
	if (argc != 3)
	{
		std::cerr << "Usage: " << argv[0]
			<< " <original-vrally2.mpn> <modded-vrally2.mpn>\n";
		return 2;
	}
	try
	{
		const std::vector<uint8_t> output = buildModdedMpn(readFile(argv[1]));
		writeFile(argv[2], output);
		std::cout << "Native bazooka mod written to " << argv[2] << " (" << output.size()
			<< " bytes)\n";
	}
	catch (const std::exception& error)
	{
		std::cerr << "Bazooka mod build failed: " << error.what() << '\n';
		return 1;
	}
	return 0;
}
