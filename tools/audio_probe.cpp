#include "audio.h"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

int main(int argc, char** argv)
{
	Audio audio;
	std::string error;
	if (argc == 2)
	{
		std::ifstream stream(argv[1], std::ios::binary);
		const std::vector<uint8_t> wave((std::istreambuf_iterator<char>(stream)),
			std::istreambuf_iterator<char>());
		if (!stream.is_open() || stream.bad() || wave.empty())
		{
			std::cerr << "Unable to read WAVE probe: " << argv[1] << std::endl;
			return 1;
		}
		if (!audio.playWave(wave.data(), wave.size(), false, error))
		{
			std::cerr << "WAVE probe failed: " << error << std::endl;
			return 1;
		}
		std::cout << "Playing embedded-music WAVE probe..." << std::endl;
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		audio.stop();
		std::cout << "WAVE probe completed." << std::endl;
		return 0;
	}

	// A two-second, format-0 General MIDI arpeggio for testing the complete
	// host synthesizer/output path independently of the emulated game.
	const uint8_t midi[] = {
		'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 96,
		'M', 'T', 'r', 'k', 0, 0, 0, 46,
		0, 0xff, 0x51, 3, 0x07, 0xa1, 0x20,
		0, 0xc0, 0,
		0, 0x90, 60, 100, 96, 0x80, 60, 0,
		0, 0x90, 64, 100, 96, 0x80, 64, 0,
		0, 0x90, 67, 100, 96, 0x80, 67, 0,
		0, 0x90, 72, 100, 96, 0x80, 72, 0,
		0, 0xff, 0x2f, 0
	};

	if (!audio.playMidi(midi, sizeof(midi), true, error))
	{
		std::cerr << "Audio probe failed: " << error << std::endl;
		return 1;
	}

	std::cout << "Playing the looping host MIDI probe..." << std::endl;
	std::this_thread::sleep_for(std::chrono::milliseconds(2500));
	audio.stop();
	std::cout << "Audio probe completed." << std::endl;
	return 0;
}
