#include "audio.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

int main()
{
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

	Audio audio;
	std::string error;
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
