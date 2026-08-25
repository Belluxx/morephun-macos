#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

class Audio {
	public:
		Audio();
		~Audio();

		Audio(const Audio&) = delete;
		Audio& operator=(const Audio&) = delete;

		bool midiSupported() const;
		bool playMidi(const uint8_t* data, size_t size, bool loop, std::string& error);
		void stop();

	private:
		struct Impl;
		std::unique_ptr<Impl> impl;
};
