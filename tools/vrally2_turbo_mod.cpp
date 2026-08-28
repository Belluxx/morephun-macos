#include "binary_io.h"
#include "turbo/cinematic.h"
#include "opcodes.h"
#include "pool.h"
#include "registers.h"
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

constexpr uint32_t ExpectedCodeSize = 0x82f0;
constexpr uint32_t ExpectedDataSize = 0x0a44;
constexpr uint32_t ExpectedBssSize = 0xcdd0;
constexpr uint32_t ExpectedResourceSize = 0xd1e8;
constexpr uint32_t ExpectedPoolSize = 0x1d2;
constexpr uint32_t ExpectedStringSize = 0x25b;
constexpr uint32_t CarUpdatePoolId = 185;
constexpr uint32_t FlipScreenPoolId = 9;
constexpr uint32_t CarUpdateCodeOffset = 0x3dc4;
constexpr uint32_t TurboStateSize = 80;
constexpr uint32_t CinematicFrameRate = 15;
constexpr uint32_t CinematicDurationMs = 19280;
constexpr uint32_t BoostFrameCount = 120;
constexpr uint32_t BoostDurationMs = 8000;
constexpr uint32_t TurboMusicDurationMs = CinematicDurationMs + BoostDurationMs;
constexpr uint32_t TurboMusicSampleRate = 11025;
constexpr uint32_t TurboMusicFadeMs = 1000;
constexpr uint32_t CinematicFrameCount =
	(CinematicDurationMs * CinematicFrameRate + 999) / 1000;

constexpr uint32_t StateCharge = 0;
constexpr uint32_t StatePhase = 4;
constexpr uint32_t StateActiveFrames = 8;
constexpr uint32_t StatePreviousSpeed = 12;
constexpr uint32_t StateCollisionCooldown = 16;
constexpr uint32_t StatePreviousKeys = 20;
constexpr uint32_t StateInRace = 24;
constexpr uint32_t StateCinematicStartTick = 28;
constexpr uint32_t StateLastUpdateResult = 32;
constexpr uint32_t StateSoundHandle = 36;
constexpr uint32_t StatePolygon = 40;
constexpr uint32_t StateCopyDestination = 52;
constexpr uint32_t StateCopySource = 64;
constexpr uint32_t StateCar = 76;

constexpr uint32_t CarStarted = 0;
constexpr uint32_t CarTargetSpeed = 0x24;
constexpr uint32_t CarSpeed = 0x28;
constexpr uint32_t CarJumpHeight = 0xac;

constexpr uint32_t KeyDown = 0x02;
constexpr uint32_t KeyFire2 = 0x100;

uint32_t encodeImmediate(int32_t value)
{
	return static_cast<uint32_t>(value) | 0x80000000U;
}

void appendU32(std::vector<uint8_t>& bytes, uint32_t value)
{
	bytes.push_back(static_cast<uint8_t>(value));
	bytes.push_back(static_cast<uint8_t>(value >> 8));
	bytes.push_back(static_cast<uint8_t>(value >> 16));
	bytes.push_back(static_cast<uint8_t>(value >> 24));
}

void appendU16(std::vector<uint8_t>& bytes, uint16_t value)
{
	bytes.push_back(static_cast<uint8_t>(value));
	bytes.push_back(static_cast<uint8_t>(value >> 8));
}

void appendPoolItem(std::vector<uint8_t>& bytes, uint8_t type, uint32_t argument1,
	uint32_t argument2)
{
	appendU32(bytes, static_cast<uint32_t>(type) | (argument1 << 8));
	appendU32(bytes, argument2);
}

class Assembler {
	public:
		explicit Assembler(uint32_t baseOffset) : baseOffset(baseOffset) {}

		void label(const std::string& name)
		{
			if (!labels.emplace(name, static_cast<uint32_t>(code.size())).second)
				throw std::runtime_error("Duplicate assembly label: " + name);
		}

		uint32_t labelOffset(const std::string& name) const
		{
			auto found = labels.find(name);
			if (found == labels.end())
				throw std::runtime_error("Unknown assembly label: " + name);
			return baseOffset + found->second;
		}

		void op(uint8_t opcode, uint8_t destination = zero, uint8_t source = zero,
			uint8_t extra = zero)
		{
			code.push_back(opcode);
			code.push_back(destination);
			code.push_back(source);
			code.push_back(extra);
		}

		void ldq(uint8_t destination, int16_t value)
		{
			op(LDQ, destination, static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8));
		}

		void immediate(uint8_t opcode, uint8_t destination, uint8_t source, int32_t value)
		{
			op(opcode, destination, source);
			appendU32(code, encodeImmediate(value));
		}

		void pool(uint8_t opcode, uint8_t destination, uint8_t source, uint32_t id)
		{
			op(opcode, destination, source);
			appendU32(code, id);
		}

		void callPool(uint32_t id) { pool(CALLl, zero, zero, id); }

		void call(const std::string& target)
		{
			const uint32_t instruction = static_cast<uint32_t>(code.size());
			op(CALLl);
			appendU32(code, 0);
			fixups.push_back({FixupKind::Long, instruction, instruction + 4, target});
		}

		void jump(const std::string& target)
		{
			const uint32_t instruction = static_cast<uint32_t>(code.size());
			op(JPl);
			appendU32(code, 0);
			fixups.push_back({FixupKind::Long, instruction, instruction + 4, target});
		}

		void branch(uint8_t opcode, uint8_t left, uint8_t right, const std::string& target)
		{
			const uint32_t instruction = static_cast<uint32_t>(code.size());
			op(opcode, left, right);
			appendU32(code, 0);
			fixups.push_back({FixupKind::Long, instruction, instruction + 4, target});
		}

		void branchImmediate(uint8_t opcode, uint8_t value, int8_t comparison,
			const std::string& target)
		{
			const uint32_t instruction = static_cast<uint32_t>(code.size());
			op(opcode, value, static_cast<uint8_t>(comparison), 0);
			fixups.push_back({FixupKind::Short, instruction, instruction + 3, target});
		}

		std::vector<uint8_t> finish()
		{
			for (const Fixup& fixup : fixups)
			{
				auto found = labels.find(fixup.target);
				if (found == labels.end())
					throw std::runtime_error("Undefined assembly label: " + fixup.target);
				const int32_t displacement = static_cast<int32_t>(found->second) -
					static_cast<int32_t>(fixup.instruction);
				if (fixup.kind == FixupKind::Short)
				{
					if (displacement % 4 != 0 || displacement / 4 < -128 || displacement / 4 > 127)
						throw std::runtime_error("Short branch is out of range: " + fixup.target);
					code[fixup.patchOffset] = static_cast<uint8_t>(displacement / 4);
				}
				else
					writeLittleU32(code.data() + fixup.patchOffset, encodeImmediate(displacement));
			}
			return code;
		}

	private:
		enum class FixupKind { Short, Long };
		struct Fixup {
			FixupKind kind;
			uint32_t instruction;
			uint32_t patchOffset;
			std::string target;
		};

		uint32_t baseOffset;
		std::vector<uint8_t> code;
		std::map<std::string, uint32_t> labels;
		std::vector<Fixup> fixups;
};

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

uint16_t rgb555(uint8_t red, uint8_t green, uint8_t blue)
{
	return static_cast<uint16_t>(((red >> 3) << 10) | ((green >> 3) << 5) | (blue >> 3));
}

class CinematicFrameBuilder {
	public:
		void color(uint8_t red, uint8_t green, uint8_t blue)
		{
			const uint16_t value = rgb555(red, green, blue);
			if (hasColor && value == currentColor)
				return;
			bytes.push_back(1);
			appendU16(bytes, value);
			currentColor = value;
			hasColor = true;
		}

		void rectangle(int x0, int y0, int x1, int y1)
		{
			if (x1 < x0)
				std::swap(x0, x1);
			if (y1 < y0)
				std::swap(y0, y1);
			x0 = clamp(x0, 0, 127);
			x1 = clamp(x1, 0, 127);
			y0 = clamp(y0, 0, 159);
			y1 = clamp(y1, 0, 159);
			bytes.push_back(2);
			bytes.push_back(static_cast<uint8_t>(x0));
			bytes.push_back(static_cast<uint8_t>(y0));
			bytes.push_back(static_cast<uint8_t>(x1));
			bytes.push_back(static_cast<uint8_t>(y1));
		}

		void triangle(double x0, double y0, double x1, double y1, double x2, double y2)
		{
			std::vector<Point> polygon = {{x0, y0}, {x1, y1}, {x2, y2}};
			polygon = clip(polygon, 0, 0.0, true);
			polygon = clip(polygon, 0, 127.0, false);
			polygon = clip(polygon, 1, 0.0, true);
			polygon = clip(polygon, 1, 159.0, false);
			for (size_t index = 1; index + 1 < polygon.size(); ++index)
				appendTriangle(polygon[0], polygon[index], polygon[index + 1]);
		}

		void quadrilateral(int x0, int y0, int x1, int y1, int x2, int y2,
			int x3, int y3)
		{
			triangle(x0, y0, x1, y1, x2, y2);
			triangle(x0, y0, x2, y2, x3, y3);
		}

		void disc(int centerX, int centerY, int radius, int segments)
		{
			constexpr double Pi = 3.14159265358979323846;
			for (int index = 0; index < segments; ++index)
			{
				const double angle0 = index * 2.0 * Pi / segments;
				const double angle1 = (index + 1) * 2.0 * Pi / segments;
				triangle(centerX, centerY,
					centerX + static_cast<int>(std::lround(std::cos(angle0) * radius)),
					centerY + static_cast<int>(std::lround(std::sin(angle0) * radius)),
					centerX + static_cast<int>(std::lround(std::cos(angle1) * radius)),
					centerY + static_cast<int>(std::lround(std::sin(angle1) * radius)));
			}
		}

		std::vector<uint8_t> finish()
		{
			bytes.push_back(0);
			return bytes;
		}

	private:
		struct Point {
			double x;
			double y;
		};

		static std::vector<Point> clip(const std::vector<Point>& input, int axis,
			double boundary, bool keepGreater)
		{
			std::vector<Point> output;
			if (input.empty())
				return output;
			auto coordinate = [axis](const Point& point) {
				return axis == 0 ? point.x : point.y;
			};
			auto inside = [&](const Point& point) {
				return keepGreater ? coordinate(point) >= boundary : coordinate(point) <= boundary;
			};
			Point previous = input.back();
			bool previousInside = inside(previous);
			for (const Point& current : input)
			{
				const bool currentInside = inside(current);
				if (previousInside != currentInside)
				{
					const double from = coordinate(previous);
					const double to = coordinate(current);
					const double amount = std::abs(to - from) < 1e-9 ? 0.0 :
						(boundary - from) / (to - from);
					Point intersection = {previous.x + (current.x - previous.x) * amount,
						previous.y + (current.y - previous.y) * amount};
					if (axis == 0)
						intersection.x = boundary;
					else
						intersection.y = boundary;
					output.push_back(intersection);
				}
				if (currentInside)
					output.push_back(current);
				previous = current;
				previousInside = currentInside;
			}
			return output;
		}

		void appendTriangle(const Point& a, const Point& b, const Point& c)
		{
			const int x0 = clamp(static_cast<int>(std::lround(a.x)), 0, 127);
			const int y0 = clamp(static_cast<int>(std::lround(a.y)), 0, 159);
			const int x1 = clamp(static_cast<int>(std::lround(b.x)), 0, 127);
			const int y1 = clamp(static_cast<int>(std::lround(b.y)), 0, 159);
			const int x2 = clamp(static_cast<int>(std::lround(c.x)), 0, 127);
			const int y2 = clamp(static_cast<int>(std::lround(c.y)), 0, 159);
			if ((x1 - x0) * (y2 - y0) == (x2 - x0) * (y1 - y0))
				return;
			bytes.push_back(3);
			bytes.push_back(static_cast<uint8_t>(x0));
			bytes.push_back(static_cast<uint8_t>(y0));
			bytes.push_back(static_cast<uint8_t>(x1));
			bytes.push_back(static_cast<uint8_t>(y1));
			bytes.push_back(static_cast<uint8_t>(x2));
			bytes.push_back(static_cast<uint8_t>(y2));
		}

		static int clamp(int value, int minimum, int maximum)
		{
			return std::max(minimum, std::min(maximum, value));
		}

		std::vector<uint8_t> bytes;
		uint16_t currentColor = 0;
		bool hasColor = false;
};

std::vector<uint8_t> buildTurboCinematic()
{
	std::vector<std::vector<uint8_t>> frames;
	frames.reserve(CinematicFrameCount);

	SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, 128, 160, 32,
		SDL_PIXELFORMAT_RGBA32);
	if (surface == nullptr)
		throw std::runtime_error(std::string("Unable to create cinematic surface: ") +
			SDL_GetError());
	SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
	if (renderer == nullptr)
	{
		const std::string error = SDL_GetError();
		SDL_FreeSurface(surface);
		throw std::runtime_error("Unable to create cinematic renderer: " + error);
	}

	uint64_t triangleCount = 0;
	{
		Cinematic cinematic(static_cast<double>(CinematicDurationMs) / 1000.0,
			renderer, 128, 160);
		CinematicPalette palette;
		cinematic.begin(palette);

		for (uint32_t index = 0; index < CinematicFrameCount; ++index)
		{
			CinematicFrameBuilder frame;
			frame.color(palette.sky.r, palette.sky.g, palette.sky.b);
			frame.rectangle(0, 0, 127, 159);
			cinematic.setTriangleSink([&](const RetroScreenTriangle& triangle) {
				frame.color(triangle.color.r, triangle.color.g, triangle.color.b);
				frame.triangle(triangle.x[0], triangle.y[0], triangle.x[1], triangle.y[1],
					triangle.x[2], triangle.y[2]);
				++triangleCount;
			});

			const double time = static_cast<double>(index) / CinematicFrameRate;
			cinematic.render(time);
			const double beat = 4.0 + time * 28.0 /
				(static_cast<double>(CinematicDurationMs) / 1000.0);
			if (beat >= 26.5 && beat < 26.72)
			{
				frame.color(255, 240, 200);
				frame.rectangle(0, 0, 127, 159);
			}
			// A narrow matte makes the camera cuts read as a cinematic while retaining
			// almost all of the T610's already-small image area.
			frame.color(0, 0, 0);
			frame.rectangle(0, 0, 127, 5);
			frame.rectangle(0, 154, 127, 159);
			frames.push_back(frame.finish());
		}
	}
	SDL_DestroyRenderer(renderer);
	SDL_FreeSurface(surface);

	std::vector<uint8_t> output(CinematicFrameCount * sizeof(uint32_t));
	for (uint32_t index = 0; index < CinematicFrameCount; ++index)
	{
		writeLittleU32(output.data() + index * sizeof(uint32_t), static_cast<uint32_t>(output.size()));
		output.insert(output.end(), frames[index].begin(), frames[index].end());
	}
	const uint32_t tableSize = CinematicFrameCount * sizeof(uint32_t);
	for (uint32_t index = 0; index < CinematicFrameCount; ++index)
	{
		const uint32_t frameStart = readLittleU32(output.data() + index * sizeof(uint32_t));
		const uint32_t frameEnd = index + 1 < CinematicFrameCount ?
			readLittleU32(output.data() + (index + 1) * sizeof(uint32_t)) :
			static_cast<uint32_t>(output.size());
		if (frameStart < tableSize || frameStart >= frameEnd || frameEnd > output.size())
			throw std::runtime_error("Invalid native cinematic frame table");
		uint32_t cursor = frameStart;
		while (cursor < frameEnd)
		{
			const uint8_t command = output[cursor++];
			if (command == 0)
			{
				if (cursor != frameEnd)
					throw std::runtime_error("Native cinematic frame has trailing data");
				break;
			}
			const uint32_t argumentSize = command == 1 ? 2 : command == 2 ? 4 :
				command == 3 ? 6 : 0;
			if (argumentSize == 0 || cursor + argumentSize > frameEnd)
				throw std::runtime_error("Invalid native cinematic drawing command");
			if (command == 2 && (output[cursor + 1] > 159 || output[cursor + 3] > 159))
				throw std::runtime_error("Native cinematic rectangle exceeds the screen");
			if (command == 3 && (output[cursor + 1] > 159 || output[cursor + 3] > 159 ||
				output[cursor + 5] > 159))
				throw std::runtime_error("Native cinematic triangle exceeds the screen");
			cursor += argumentSize;
		}
		if (output[frameEnd - 1] != 0)
			throw std::runtime_error("Native cinematic frame is not terminated");
	}
	std::cout << "Serialized " << triangleCount << " depth-sorted 3D triangles\n";
	return output;
}

double validateTurboWave(const std::vector<uint8_t>& wave)
{
	if (wave.size() < 44 || std::string(reinterpret_cast<const char*>(wave.data()), 4) != "RIFF" ||
		std::string(reinterpret_cast<const char*>(wave.data() + 8), 4) != "WAVE" ||
		readLittleU32(wave.data() + 4) + 8U != wave.size())
		throw std::runtime_error("Turbo music must be a complete RIFF/WAVE file");
	uint16_t format = 0;
	uint16_t channels = 0;
	uint16_t bits = 0;
	uint32_t sampleRate = 0;
	uint32_t dataSize = 0;
	for (size_t offset = 12; offset + 8 <= wave.size();)
	{
		const uint32_t chunkSize = readLittleU32(wave.data() + offset + 4);
		const size_t next = offset + 8U + chunkSize + (chunkSize & 1U);
		if (next > wave.size())
			throw std::runtime_error("Turbo music WAVE contains a truncated chunk");
		const std::string id(reinterpret_cast<const char*>(wave.data() + offset), 4);
		if (id == "fmt " && chunkSize >= 16)
		{
			format = readLittleU16(wave.data() + offset + 8);
			channels = readLittleU16(wave.data() + offset + 10);
			sampleRate = readLittleU32(wave.data() + offset + 12);
			bits = readLittleU16(wave.data() + offset + 22);
		}
		else if (id == "data")
			dataSize = chunkSize;
		offset = next;
	}
	if (format != 1 || channels != 1 || sampleRate != TurboMusicSampleRate ||
		bits != 16 || dataSize == 0)
		throw std::runtime_error("Turbo music must be 11025 Hz, mono, 16-bit PCM WAVE");
	const double duration = static_cast<double>(dataSize) /
		(static_cast<double>(sampleRate) * channels * (bits / 8));
	const double expectedDuration = static_cast<double>(TurboMusicDurationMs) / 1000.0;
	if (duration < expectedDuration - 0.08 || duration > expectedDuration + 0.08)
		throw std::runtime_error(
			"Turbo music WAVE must span the complete cinematic and boost");
	return duration;
}

void applyTurboMusicFade(std::vector<uint8_t>& wave)
{
	uint32_t dataOffset = 0;
	uint32_t dataSize = 0;
	for (size_t offset = 12; offset + 8 <= wave.size();)
	{
		const uint32_t chunkSize = readLittleU32(wave.data() + offset + 4);
		if (std::string(reinterpret_cast<const char*>(wave.data() + offset), 4) == "data")
		{
			dataOffset = static_cast<uint32_t>(offset + 8);
			dataSize = chunkSize;
			break;
		}
		offset += 8U + chunkSize + (chunkSize & 1U);
	}
	if (dataSize < 2 || dataOffset > wave.size() || dataSize > wave.size() - dataOffset)
		throw std::runtime_error("Turbo music WAVE has no usable PCM data chunk");

	const uint32_t sampleCount = dataSize / sizeof(int16_t);
	const uint32_t fadeSamples = std::min(sampleCount,
		TurboMusicSampleRate * TurboMusicFadeMs / 1000U);
	const uint32_t firstFadeSample = sampleCount - fadeSamples;
	for (uint32_t index = 0; index < fadeSamples; ++index)
	{
		const uint32_t sampleOffset = dataOffset + (firstFadeSample + index) * 2U;
		const int16_t sample = static_cast<int16_t>(readLittleU16(wave.data() + sampleOffset));
		const int32_t remaining = static_cast<int32_t>(fadeSamples - index - 1U);
		const int16_t faded = static_cast<int16_t>(
			static_cast<int32_t>(sample) * remaining / static_cast<int32_t>(fadeSamples));
		writeLittleU16(wave.data() + sampleOffset, static_cast<uint16_t>(faded));
	}
}

void requireTargetHeader(const VMGPHeader& header)
{
	if (std::string(header.magicNo, 4) != "VMGP" || header.flags != 0 ||
		header.codeSize != ExpectedCodeSize || header.dataSize != ExpectedDataSize ||
		header.bssSize != ExpectedBssSize || header.resSize != ExpectedResourceSize ||
		header.poolSize != ExpectedPoolSize || header.stringSize != ExpectedStringSize)
		throw std::runtime_error("Input is not the supported V-Rally 2 RC14EU M5 executable");
}

std::vector<uint8_t> buildGuestCode(uint32_t originalUpdatePoolId,
	uint32_t originalFlipPoolId, uint32_t turboStatePoolId, uint32_t turboWavePoolId,
	uint32_t cinematicPoolId, uint32_t soundInitPoolId, uint32_t soundGetHandlePoolId,
	uint32_t soundCtrlExPoolId, uint32_t soundCtrlPoolId, uint32_t copyRectPoolId,
	Assembler& assembler)
{
	auto drawTriangle = [&](int x0, int y0, int x1, int y1, int x2, int y2) {
		const int coordinates[6] = {x0, y0, x1, y1, x2, y2};
		for (uint32_t coordinate = 0; coordinate < 6; ++coordinate)
		{
			assembler.ldq(r0, static_cast<int16_t>(coordinates[coordinate]));
			if ((coordinate & 1U) != 0)
				assembler.op(SUB, r0, r0, s2);
			assembler.immediate(STHd, r0, s0, StatePolygon + coordinate * 2);
		}
		assembler.immediate(ADDi, p0, s0, StatePolygon);
		assembler.callPool(5); // vDrawFlatPolygon
	};

	// All gameplay state is guest BSS. During the modal cinematic every invocation of
	// the original car update is skipped, freezing race physics until the native handoff.
	assembler.label("TurboCarUpdateWrapper");
	assembler.op(STORE, ra, s2);
	assembler.op(MOV, s1, p0);
	assembler.pool(LDI, s0, zero, turboStatePoolId);
	assembler.immediate(LDWd, r0, s0, StatePhase);
	assembler.branchImmediate(BEQI, r0, 1, "TurboCinematicUpdateSkip");
	assembler.op(MOV, p0, s1);
	assembler.callPool(originalUpdatePoolId);
	assembler.op(MOV, s2, r0);
	assembler.immediate(STWd, s2, s0, StateLastUpdateResult);
	assembler.op(MOV, p0, s1);
	assembler.call("TurboUpdate");
	assembler.op(MOV, r0, s2);
	assembler.op(RET, s3, s2);
	assembler.label("TurboCinematicUpdateSkip");
	assembler.immediate(LDWd, r0, s0, StateLastUpdateResult);
	assembler.op(RET, s3, s2);

	assembler.label("TurboUpdate");
	assembler.op(STORE, ra, s6);
	assembler.op(MOV, s1, p0);                         // s1 = car
	assembler.pool(LDI, s0, zero, turboStatePoolId);  // s0 = TurboState
	assembler.immediate(STWd, s1, s0, StateCar);
	assembler.immediate(LDBUd, r0, s1, CarStarted);
	assembler.ldq(p0, 1);
	assembler.branch(BNE, r0, p0, "TurboReset");
	assembler.ldq(r0, 1);
	assembler.immediate(STWd, r0, s0, StateInRace);
	assembler.callPool(10); // vGetButtonData
	assembler.op(MOV, s2, r0);
	assembler.immediate(LDWd, s3, s0, StatePhase);
	assembler.branchImmediate(BEQI, s3, 2, "TurboActive");

	// Meter update. Charge is stored in tenths of a percent (0..1000).
	assembler.immediate(LDWd, s4, s0, StateCollisionCooldown);
	assembler.branchImmediate(BLEI, s4, 0, "CooldownDone");
	assembler.op(ADDQ, s4, s4, static_cast<uint8_t>(-1));
	assembler.immediate(STWd, s4, s0, StateCollisionCooldown);
	assembler.label("CooldownDone");
	assembler.immediate(LDWd, s5, s0, StateCharge);
	assembler.immediate(LDWd, p0, s1, CarSpeed);
	assembler.immediate(LDWd, p1, s1, CarTargetSpeed);
	assembler.branchImmediate(BLTI, p0, 0, "RateReverse");
	assembler.immediate(ANDi, r0, s2, KeyDown);
	assembler.branchImmediate(BNEI, r0, 0, "RateBrake");
	assembler.branchImmediate(BLEI, p1, 0, "RateSpeedBands");
	assembler.immediate(LDI, p2, zero, 40000);
	assembler.branch(BLT, p1, p2, "RateOffroad");
	assembler.label("RateSpeedBands");
	assembler.immediate(LDI, p2, zero, 43200);
	assembler.branch(BGE, p0, p2, "RateMaximum");
	assembler.immediate(LDI, p2, zero, 33600);
	assembler.branch(BGE, p0, p2, "RateHigh");
	assembler.immediate(LDI, p2, zero, 19200);
	assembler.branch(BLT, p0, p2, "RateLow");
	assembler.jump("RateDone");
	assembler.label("RateReverse");
	assembler.op(ADDQ, s5, s5, static_cast<uint8_t>(-16));
	assembler.jump("RateDone");
	assembler.label("RateBrake");
	assembler.op(ADDQ, s5, s5, static_cast<uint8_t>(-3));
	assembler.jump("RateDone");
	assembler.label("RateOffroad");
	assembler.op(ADDQ, s5, s5, static_cast<uint8_t>(-9));
	assembler.jump("RateDone");
	assembler.label("RateMaximum");
	assembler.op(ADDQ, s5, s5, 6);
	assembler.jump("RateDone");
	assembler.label("RateHigh");
	assembler.op(ADDQ, s5, s5, 3);
	assembler.jump("RateDone");
	assembler.label("RateLow");
	assembler.op(ADDQ, s5, s5, static_cast<uint8_t>(-5));
	assembler.label("RateDone");

	// Detect a sudden non-braking speed loss as a collision.
	assembler.branchImmediate(BGTI, s4, 0, "CollisionDone");
	assembler.immediate(ANDi, r0, s2, KeyDown);
	assembler.branchImmediate(BNEI, r0, 0, "CollisionDone");
	assembler.immediate(LDWd, p2, s0, StatePreviousSpeed);
	assembler.branchImmediate(BLEI, p2, 0, "CollisionDone");
	assembler.op(SUB, r0, p2, p0);
	assembler.immediate(LDI, p1, zero, 9600);
	assembler.branch(BLE, r0, p1, "CollisionDone");
	assembler.immediate(ADDi, s5, s5, -300);
	assembler.ldq(s4, 15);
	assembler.immediate(STWd, s4, s0, StateCollisionCooldown);
	assembler.label("CollisionDone");

	// Clamp and store charge.
	assembler.branchImmediate(BLTI, s5, 0, "ChargeZero");
	assembler.immediate(LDI, p1, zero, 1000);
	assembler.branch(BLE, s5, p1, "ChargeClamped");
	assembler.op(MOV, s5, p1);
	assembler.jump("ChargeClamped");
	assembler.label("ChargeZero");
	assembler.ldq(s5, 0);
	assembler.label("ChargeClamped");
	assembler.immediate(STWd, s5, s0, StateCharge);

	// Fire 2 (Space in MoRePhun) activates only on a fresh press at full charge.
	assembler.immediate(LDI, p1, zero, 1000);
	assembler.branch(BLT, s5, p1, "TurboFinish");
	assembler.immediate(ANDi, r0, s2, KeyFire2);
	assembler.branchImmediate(BEQI, r0, 0, "TurboFinish");
	assembler.immediate(LDWd, p0, s0, StatePreviousKeys);
	assembler.immediate(ANDi, p0, p0, KeyFire2);
	assembler.branchImmediate(BNEI, p0, 0, "TurboFinish");
	assembler.ldq(r0, 1);
	assembler.immediate(STWd, r0, s0, StatePhase);
	assembler.ldq(r0, 0);
	assembler.immediate(STWd, r0, s0, StateCharge);
	assembler.callPool(14); // vGetTickCount: native A/V synchronization clock
	assembler.immediate(STWd, r0, s0, StateCinematicStartTick);
	assembler.callPool(soundInitPoolId); // official Mophun PCM/ADPCM sound API
	assembler.pool(LDI, p0, zero, turboWavePoolId);
	assembler.callPool(soundGetHandlePoolId); // vSoundGetHandle(in-memory RIFF/WAVE)
	assembler.immediate(STWd, r0, s0, StateSoundHandle);
	assembler.op(MOV, p0, r0);
	assembler.ldq(p1, 1); // SNDCTRL_PLAY
	assembler.ldq(p2, 0); // play once, no override
	assembler.callPool(soundCtrlExPoolId); // vSoundCtrlEx
	assembler.jump("TurboFinish");

	assembler.label("TurboActive");
	assembler.label("TurboApplyBoost");
	assembler.immediate(LDWd, p0, s1, CarTargetSpeed);
	assembler.immediate(LDI, p1, zero, 40000);
	assembler.branch(BLT, p0, p1, "BoostTimer");
	assembler.op(MULQ, p0, p0, 7);
	assembler.immediate(DIVi, p0, p0, 4);
	assembler.immediate(LDI, p1, zero, 84000);
	assembler.branch(BLE, p0, p1, "StoreBoost");
	assembler.op(MOV, p0, p1);
	assembler.label("StoreBoost");
	assembler.immediate(STWd, p0, s1, CarTargetSpeed);
	assembler.label("BoostTimer");
	assembler.immediate(LDWd, s4, s0, StateActiveFrames);
	assembler.op(ADDQ, s4, s4, static_cast<uint8_t>(-1));
	assembler.immediate(STWd, s4, s0, StateActiveFrames);
	assembler.branchImmediate(BGTI, s4, 0, "TurboFinish");
	assembler.ldq(r0, 0);
	assembler.immediate(STWd, r0, s0, StatePhase);
	assembler.immediate(LDWd, p0, s0, StateSoundHandle);
	assembler.ldq(p1, 2); // SNDCTRL_STOP: music ends with the boost, not the cinematic
	assembler.callPool(soundCtrlPoolId); // vSoundCtrl

	assembler.label("TurboFinish");
	assembler.immediate(STWd, s2, s0, StatePreviousKeys);
	assembler.immediate(LDWd, r0, s1, CarSpeed);
	assembler.immediate(STWd, r0, s0, StatePreviousSpeed);
	assembler.op(RET, s7, s6);

	assembler.label("TurboReset");
	assembler.immediate(LDWd, r0, s0, StatePhase);
	assembler.branchImmediate(BEQI, r0, 0, "TurboResetClear");
	assembler.immediate(LDWd, p0, s0, StateSoundHandle);
	assembler.ldq(p1, 2); // stop a cinematic/boost interrupted by leaving the race
	assembler.callPool(soundCtrlPoolId); // vSoundCtrl
	assembler.label("TurboResetClear");
	assembler.ldq(p0, TurboStateSize);
	assembler.op(SYSSET, s0, zero, p0);
	assembler.op(RET, s7, s6);

	// The existing vFlipScreen pool entry points here. The HUD is therefore guest-rendered
	// immediately before the real OS flip without any emulator graphics hook.
	assembler.label("TurboFlipWrapper");
	assembler.op(STORE, zero, sp);
	assembler.pool(LDI, r0, zero, turboStatePoolId);
	assembler.immediate(LDWd, r0, r0, StatePhase);
	assembler.branchImmediate(BNEI, r0, 1, "TurboFlipHud");
	assembler.call("TurboDrawCinematic");
	assembler.jump("TurboFlipPresent");
	assembler.label("TurboFlipHud");
	assembler.call("TurboDrawHud");
	assembler.label("TurboFlipPresent");
	assembler.callPool(originalFlipPoolId);
	assembler.op(RET, zero, sp);

	// The cinematic is a compact display-list interpreter. Every frame is the
	// depth-sorted output of the offline 3D camera and is rendered by guest calls.
	assembler.label("TurboDrawCinematic");
	assembler.op(STORE, ra, s6);
	assembler.pool(LDI, s0, zero, turboStatePoolId);
	assembler.callPool(14); // vGetTickCount
	assembler.immediate(LDWd, s1, s0, StateCinematicStartTick);
	assembler.op(SUB, s2, r0, s1); // elapsed milliseconds
	assembler.op(MULQ, s3, s2, CinematicFrameRate);
	assembler.immediate(DIVi, s3, s3, 1000);
	assembler.immediate(LDI, p0, zero, CinematicFrameCount);
	assembler.branch(BLT, s3, p0, "CinematicFrameReady");
	assembler.ldq(s3, CinematicFrameCount - 1);
	assembler.label("CinematicFrameReady");

	assembler.ldq(p0, 0);
	assembler.ldq(p1, 0);
	assembler.ldq(p2, 127);
	assembler.ldq(p3, 159);
	assembler.callPool(23); // vSetClipWindow

	assembler.pool(LDI, s4, zero, cinematicPoolId);
	assembler.op(MULQ, s5, s3, 4);
	assembler.op(ADD, s5, s4, s5);
	assembler.immediate(LDWd, s5, s5, 0);
	assembler.op(ADD, s5, s4, s5);
	assembler.label("CinematicCommand");
	assembler.immediate(LDBUd, r0, s5, 0);
	assembler.op(ADDQ, s5, s5, 1);
	assembler.branchImmediate(BEQI, r0, 0, "CinematicFrameDone");
	assembler.branchImmediate(BEQI, r0, 1, "CinematicColor");
	assembler.branchImmediate(BEQI, r0, 2, "CinematicRect");

	// Triangle coordinates are expanded from bytes into the guest BSS scratch area.
	for (uint32_t coordinate = 0; coordinate < 6; ++coordinate)
	{
		assembler.immediate(LDBUd, r0, s5, static_cast<int32_t>(coordinate));
		assembler.immediate(STHd, r0, s0, StatePolygon + coordinate * 2);
	}
	assembler.immediate(ADDi, p0, s0, StatePolygon);
	assembler.callPool(5); // vDrawFlatPolygon
	assembler.op(ADDQ, s5, s5, 6);
	assembler.jump("CinematicCommand");

	assembler.label("CinematicColor");
	assembler.immediate(LDHUd, p0, s5, 0);
	assembler.op(ADDQ, s5, s5, 2);
	assembler.call("TurboSetColor");
	assembler.jump("CinematicCommand");

	assembler.label("CinematicRect");
	assembler.immediate(LDBUd, p0, s5, 0);
	assembler.immediate(LDBUd, p1, s5, 1);
	assembler.immediate(LDBUd, p2, s5, 2);
	assembler.immediate(LDBUd, p3, s5, 3);
	assembler.op(ADDQ, s5, s5, 4);
	assembler.callPool(8); // vFillRect
	assembler.jump("CinematicCommand");

	assembler.label("CinematicFrameDone");
	assembler.immediate(LDI, p0, zero, CinematicDurationMs);
	assembler.branch(BLT, s2, p0, "CinematicReturn");
	assembler.ldq(r0, 2);
	assembler.immediate(STWd, r0, s0, StatePhase);
	assembler.ldq(r0, BoostFrameCount); // eight seconds at V-Rally's 15 Hz race update
	assembler.immediate(STWd, r0, s0, StateActiveFrames);
	assembler.label("CinematicReturn");
	assembler.op(RET, s7, s6);

	assembler.label("TurboDrawHud");
	assembler.op(STORE, ra, s5);
	assembler.pool(LDI, s0, zero, turboStatePoolId);
	assembler.immediate(LDWd, r0, s0, StateInRace);
	assembler.branchImmediate(BEQI, r0, 0, "HudReturnJump");
	assembler.jump("HudBegin");
	assembler.label("HudReturnJump");
	assembler.jump("HudReturn");
	assembler.label("HudBegin");

	assembler.ldq(p0, 0);
	assembler.ldq(p1, 0);
	assembler.ldq(p2, 127);
	assembler.ldq(p3, 159);
	assembler.callPool(23); // vSetClipWindow

	// The active effect pass is entirely guest-driven. vCopyRect is the standard
	// Mophun screen-copy API: alternating source/destination offsets produce the
	// original one-pixel shudder before the exhaust geometry is drawn.
	assembler.immediate(LDWd, s1, s0, StatePhase);
	assembler.branchImmediate(BEQI, s1, 2, "HudBoostEffects");
	assembler.jump("HudMeter");
	assembler.label("HudBoostEffects");
	assembler.ldq(r0, 0);
	assembler.immediate(STWd, r0, s0, StateCopyDestination);
	assembler.immediate(STWd, r0, s0, StateCopySource);
	assembler.ldq(r0, 256); // RGB555 scan-line stride in bytes
	assembler.immediate(STHd, r0, s0, StateCopyDestination + 8);
	assembler.immediate(STHd, r0, s0, StateCopySource + 8);
	assembler.immediate(LDWd, s1, s0, StateActiveFrames);
	assembler.immediate(ANDi, r0, s1, 1);
	assembler.branchImmediate(BEQI, r0, 0, "ShakeXEven");
	assembler.ldq(r0, 2); // one RGB555 pixel
	assembler.immediate(STHd, r0, s0, StateCopyDestination + 4);
	assembler.ldq(r0, 0);
	assembler.immediate(STHd, r0, s0, StateCopySource + 4);
	assembler.jump("ShakeXDone");
	assembler.label("ShakeXEven");
	assembler.ldq(r0, 0);
	assembler.immediate(STHd, r0, s0, StateCopyDestination + 4);
	assembler.ldq(r0, 2);
	assembler.immediate(STHd, r0, s0, StateCopySource + 4);
	assembler.label("ShakeXDone");
	assembler.immediate(ANDi, r0, s1, 2);
	assembler.branchImmediate(BEQI, r0, 0, "ShakeYEven");
	assembler.ldq(r0, 1);
	assembler.immediate(STHd, r0, s0, StateCopyDestination + 6);
	assembler.ldq(r0, 0);
	assembler.immediate(STHd, r0, s0, StateCopySource + 6);
	assembler.jump("ShakeYDone");
	assembler.label("ShakeYEven");
	assembler.ldq(r0, 0);
	assembler.immediate(STHd, r0, s0, StateCopyDestination + 6);
	assembler.ldq(r0, 1);
	assembler.immediate(STHd, r0, s0, StateCopySource + 6);
	assembler.label("ShakeYDone");
	assembler.immediate(ADDi, p0, s0, StateCopyDestination);
	assembler.immediate(ADDi, p1, s0, StateCopySource);
	assembler.ldq(p2, 127);
	assembler.ldq(p3, 159);
	assembler.callPool(copyRectPoolId); // vCopyRect(screen -> screen)

	// Two alternating, layered exhaust flames. V-Rally renders the player car at
	// y=147-CarJumpHeight, so apply the same displacement to every flame vertex.
	assembler.immediate(LDWd, r0, s0, StateCar);
	assembler.immediate(LDHUd, s2, r0, CarJumpHeight);
	assembler.immediate(ANDi, r0, s1, 1);
	assembler.branchImmediate(BEQI, r0, 0, "BoostFlameEvenJump");
	assembler.jump("BoostFlameOdd");
	assembler.label("BoostFlameEvenJump");
	assembler.jump("BoostFlameEven");
	assembler.label("BoostFlameOdd");
	assembler.ldq(p0, rgb555(220, 40, 20));
	assembler.call("TurboSetColor");
	drawTriangle(54, 147, 62, 147, 60, 159);
	drawTriangle(55, 148, 59, 148, 52, 156);
	drawTriangle(66, 147, 74, 147, 69, 157);
	drawTriangle(69, 148, 73, 148, 77, 159);
	assembler.ldq(p0, rgb555(255, 140, 20));
	assembler.call("TurboSetColor");
	drawTriangle(55, 147, 61, 147, 56, 158);
	drawTriangle(67, 147, 73, 147, 72, 156);
	assembler.ldq(p0, rgb555(255, 240, 130));
	assembler.call("TurboSetColor");
	drawTriangle(57, 147, 59, 147, 58, 154);
	drawTriangle(69, 147, 71, 147, 70, 153);
	assembler.jump("BoostFlameDone");
	assembler.label("BoostFlameEven");
	assembler.ldq(p0, rgb555(220, 40, 20));
	assembler.call("TurboSetColor");
	drawTriangle(54, 147, 62, 147, 56, 157);
	drawTriangle(57, 148, 61, 148, 64, 159);
	drawTriangle(66, 147, 74, 147, 72, 159);
	drawTriangle(67, 148, 71, 148, 64, 156);
	assembler.ldq(p0, rgb555(255, 140, 20));
	assembler.call("TurboSetColor");
	drawTriangle(55, 147, 61, 147, 60, 156);
	drawTriangle(67, 147, 73, 147, 68, 158);
	assembler.ldq(p0, rgb555(255, 240, 130));
	assembler.call("TurboSetColor");
	drawTriangle(57, 147, 59, 147, 59, 153);
	drawTriangle(69, 147, 71, 147, 69, 155);
	assembler.label("BoostFlameDone");

	assembler.label("HudMeter");
	assembler.ldq(p0, 0x03ff); // cyan RGB555
	assembler.call("TurboSetColor");
	assembler.ldq(p0, 94);
	assembler.ldq(p1, 136);
	assembler.ldq(p2, 127);
	assembler.ldq(p3, 145);
	assembler.callPool(8); // vFillRect

	assembler.ldq(p0, 0); // black
	assembler.call("TurboSetColor");
	assembler.ldq(p0, 95);
	assembler.ldq(p1, 137);
	assembler.ldq(p2, 126);
	assembler.ldq(p3, 144);
	assembler.callPool(8);

	assembler.immediate(LDWd, s1, s0, StatePhase);
	assembler.branchImmediate(BEQI, s1, 2, "HudActive");
	assembler.immediate(LDWd, s2, s0, StateCharge);
	assembler.op(MULQ, s2, s2, 30);
	assembler.immediate(DIVi, s2, s2, 1000);
	assembler.immediate(LDWd, r0, s0, StateCharge);
	assembler.immediate(LDI, p0, zero, 1000);
	assembler.branch(BGE, r0, p0, "HudReadyColor");
	assembler.ldq(p0, 0x001f); // blue RGB555
	assembler.jump("HudColorReady");
	assembler.label("HudReadyColor");
	assembler.ldq(p0, 0x7fff); // white RGB555
	assembler.jump("HudColorReady");
	assembler.label("HudActive");
	assembler.immediate(LDWd, s2, s0, StateActiveFrames);
	assembler.op(MULQ, s2, s2, 30);
	assembler.immediate(DIVi, s2, s2, 120);
	assembler.ldq(p0, 0x7fe0); // yellow RGB555
	assembler.label("HudColorReady");
	assembler.branchImmediate(BLEI, s2, 0, "HudReturn");
	assembler.call("TurboSetColor");
	assembler.op(ADDQ, p2, s2, 94);
	assembler.ldq(p0, 95);
	assembler.ldq(p1, 138);
	assembler.ldq(p3, 143);
	assembler.callPool(8);
	assembler.label("HudReturn");
	assembler.op(RET, s6, s5);

	assembler.label("TurboSetColor");
	assembler.op(STORE, zero, sp);
	assembler.ldq(p1, 1);
	assembler.op(SLLi, p1, p1, 31);
	assembler.op(OR, p0, p0, p1);
	assembler.callPool(24); // vSetForeColor
	assembler.op(RET, zero, sp);

	return assembler.finish();
}

std::vector<uint8_t> buildModdedMpn(std::vector<uint8_t> input,
	const std::vector<uint8_t>& turboWave)
{
	const double waveDuration = validateTurboWave(turboWave);
	std::vector<uint8_t> fadedTurboWave = turboWave;
	applyTurboMusicFade(fadedTurboWave);
	const VMGPHeader header = decodeVMGPHeader(input.data());
	requireTargetHeader(header);
	std::string decryptError;
	if (!decryptCommercialCode(input, header, decryptError))
		throw std::runtime_error("Unable to decrypt the commercial code: " + decryptError);

	const uint32_t oldDataOffset = sizeof(VMGPHeader) + header.codeSize;
	const uint32_t oldResourceOffset = oldDataOffset + header.dataSize;
	const uint32_t oldPoolOffset = oldResourceOffset + header.resSize;
	const uint32_t oldStringOffset = oldPoolOffset + header.poolSize * PoolItemSize;
	if (oldStringOffset + header.stringSize > input.size())
		throw std::runtime_error("Input MPN sections exceed the file size");

	const uint8_t expectedUpdateStart[] = {STORE, ra, s2, 0};
	if (!std::equal(expectedUpdateStart, expectedUpdateStart + sizeof(expectedUpdateStart),
		input.begin() + sizeof(VMGPHeader) + CarUpdateCodeOffset))
		throw std::runtime_error("Car update signature does not match the supported executable");

	uint8_t* const oldPool = input.data() + oldPoolOffset;
	const PoolItem flipItem = decodePoolItemBytes(oldPool + (FlipScreenPoolId - 1) * PoolItemSize);
	const PoolItem updateItem = decodePoolItemBytes(oldPool + (CarUpdatePoolId - 1) * PoolItemSize);
	if (flipItem.segment_0 != 0 || flipItem.segment_1 != 2 ||
		updateItem.segment_0 != 1 || updateItem.extra != CarUpdateCodeOffset)
		throw std::runtime_error("V-Rally hook address table entries do not match");

	const uint32_t originalUpdatePoolId = header.poolSize + 1;
	const uint32_t originalFlipPoolId = header.poolSize + 2;
	const uint32_t turboStatePoolId = header.poolSize + 3;
	const uint32_t turboWavePoolId = header.poolSize + 4;
	const uint32_t cinematicPoolId = header.poolSize + 5;
	const uint32_t soundInitPoolId = header.poolSize + 6;
	const uint32_t soundGetHandlePoolId = header.poolSize + 7;
	const uint32_t soundCtrlExPoolId = header.poolSize + 8;
	const uint32_t soundCtrlPoolId = header.poolSize + 9;
	const uint32_t copyRectPoolId = header.poolSize + 10;
	const std::vector<uint8_t> cinematic = buildTurboCinematic();
	std::vector<uint8_t> nativeData;
	const uint32_t turboWaveOffset = header.dataSize;
	nativeData.insert(nativeData.end(), fadedTurboWave.begin(), fadedTurboWave.end());
	while (nativeData.size() % sizeof(uint32_t) != 0)
		nativeData.push_back(0);
	const uint32_t cinematicOffset = header.dataSize + static_cast<uint32_t>(nativeData.size());
	nativeData.insert(nativeData.end(), cinematic.begin(), cinematic.end());
	while (nativeData.size() % sizeof(uint32_t) != 0)
		nativeData.push_back(0);

	Assembler assembler(header.codeSize);
	const std::vector<uint8_t> guestCode = buildGuestCode(originalUpdatePoolId,
		originalFlipPoolId, turboStatePoolId, turboWavePoolId, cinematicPoolId,
		soundInitPoolId, soundGetHandlePoolId, soundCtrlExPoolId, soundCtrlPoolId,
		copyRectPoolId, assembler);

	std::vector<uint8_t> nativeStrings;
	auto appendSyscallName = [&](const char* name) {
		const uint32_t offset = header.stringSize + static_cast<uint32_t>(nativeStrings.size());
		nativeStrings.insert(nativeStrings.end(), name, name + std::strlen(name) + 1);
		return offset;
	};
	const uint32_t soundInitString = appendSyscallName("vSoundInit");
	const uint32_t soundGetHandleString = appendSyscallName("vSoundGetHandle");
	const uint32_t soundCtrlExString = appendSyscallName("vSoundCtrlEx");
	const uint32_t soundCtrlString = appendSyscallName("vSoundCtrl");
	const uint32_t copyRectString = appendSyscallName("vCopyRect");

	std::vector<uint8_t> newPool(oldPool, oldPool + header.poolSize * PoolItemSize);
	writeLittleU32(newPool.data() + (CarUpdatePoolId - 1) * PoolItemSize, 0x11);
	writeLittleU32(newPool.data() + (CarUpdatePoolId - 1) * PoolItemSize + 4,
		assembler.labelOffset("TurboCarUpdateWrapper"));
	writeLittleU32(newPool.data() + (FlipScreenPoolId - 1) * PoolItemSize, 0x11);
	writeLittleU32(newPool.data() + (FlipScreenPoolId - 1) * PoolItemSize + 4,
		assembler.labelOffset("TurboFlipWrapper"));
	appendPoolItem(newPool, 0x11, 0, CarUpdateCodeOffset);
	appendPoolItem(newPool, 0x02, flipItem.segmentoffset, 0);
	appendPoolItem(newPool, 0x41, 0, header.bssSize);
	appendPoolItem(newPool, 0x21, 0, turboWaveOffset);
	appendPoolItem(newPool, 0x21, 0, cinematicOffset);
	appendPoolItem(newPool, 0x02, soundInitString, 0);
	appendPoolItem(newPool, 0x02, soundGetHandleString, 0);
	appendPoolItem(newPool, 0x02, soundCtrlExString, 0);
	appendPoolItem(newPool, 0x02, soundCtrlString, 0);
	appendPoolItem(newPool, 0x02, copyRectString, 0);

	std::vector<uint8_t> output;
	output.reserve(input.size() + guestCode.size() + nativeData.size() +
		10 * PoolItemSize + nativeStrings.size());
	output.insert(output.end(), input.begin(), input.begin() + sizeof(VMGPHeader));
	output.insert(output.end(), input.begin() + sizeof(VMGPHeader), input.begin() + oldDataOffset);
	output.insert(output.end(), guestCode.begin(), guestCode.end());
	output.insert(output.end(), input.begin() + oldDataOffset, input.begin() + oldResourceOffset);
	output.insert(output.end(), nativeData.begin(), nativeData.end());
	output.insert(output.end(), input.begin() + oldResourceOffset, input.begin() + oldPoolOffset);
	output.insert(output.end(), newPool.begin(), newPool.end());
	output.insert(output.end(), input.begin() + oldStringOffset,
		input.begin() + oldStringOffset + header.stringSize);
	output.insert(output.end(), nativeStrings.begin(), nativeStrings.end());

	writeLittleU32(output.data() + 12, header.codeSize + static_cast<uint32_t>(guestCode.size()));
	writeLittleU32(output.data() + 16, header.dataSize + static_cast<uint32_t>(nativeData.size()));
	writeLittleU32(output.data() + 20, header.bssSize + TurboStateSize);
	writeLittleU32(output.data() + 32, header.poolSize + 10);
	writeLittleU32(output.data() + 36,
		header.stringSize + static_cast<uint32_t>(nativeStrings.size()));
	std::cout << "Embedded native cinematic: " << CinematicFrameCount << " frames at "
		<< CinematicFrameRate << " FPS (" << cinematic.size() << " bytes), PCM WAVE "
		<< fadedTurboWave.size() << " bytes / " << waveDuration
		<< " seconds with a one-second fade-out\n";
	return output;
}

} // namespace

int main(int argc, char* argv[])
{
	if (argc != 4)
	{
		std::cerr << "Usage: " << argv[0]
			<< " <original-vrally2.mpn> <modded-vrally2.mpn> <turbo-music.wav>\n";
		return 2;
	}
	try
	{
		const std::vector<uint8_t> output = buildModdedMpn(readFile(argv[1]),
			readFile(argv[3]));
		writeFile(argv[2], output);
		std::cout << "Native turbo mod written to " << argv[2] << " (" << output.size()
			<< " bytes)\n";
	}
	catch (const std::exception& error)
	{
		std::cerr << "Turbo mod build failed: " << error.what() << '\n';
		return 1;
	}
	return 0;
}
