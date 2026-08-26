#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

class MophunVM;

// Optional development aids controlled by environment variables. Nothing here
// runs unless the matching variable is set, so release behaviour is unchanged.
//
//   MOPHUN_INPUT_SCRIPT="10-40:F;200-900:U;300-320:UL"
//       Hold the listed keys during the inclusive frame ranges (frame = flip
//       index). Keys: U D L R F(ire) S(elect/back) 2(fire2).
//   MOPHUN_SCREENSHOT_DIR=/dir  [MOPHUN_SCREENSHOT_EVERY=30]
//       Save every Nth presented frame as frame_NNNNNN.bmp.
//   MOPHUN_RAM_DUMP=/path/dump.bin
//       Append the guest data+bss segments after every flip for offline scans.
//   MOPHUN_FRAME_STATS=1
//       Print per-frame instruction and vGetTickCount counts.
//   MOPHUN_POKE="840-960:0x13e28=90000,0x13e24=90000"
//       Write 32-bit guest values every vGetButtonData call in the frame range.
class DevTools {
	public:
		DevTools();
		~DevTools();

		void onFlip(MophunVM& vm, uint64_t executedInstructions, uint32_t virtualTicks);
		void onTickCount() { ++tickCallsThisFrame; }
		uint32_t scriptedKeys() const;
		void applyPokes(MophunVM& vm) const;
		std::string screenshotPathForFrame() const;
		uint32_t frameIndex() const { return frame; }
		bool enabled() const { return active; }

	private:
		struct ScriptEntry {
			uint32_t firstFrame;
			uint32_t lastFrame;
			uint32_t keys;
		};

		bool active = false;
		uint32_t frame = 0;
		uint64_t instructionsAtLastFlip = 0;
		uint32_t tickCallsThisFrame = 0;
		bool frameStats = false;
		struct PokeEntry {
			uint32_t firstFrame;
			uint32_t lastFrame;
			uint32_t address;
			uint32_t value;
		};
		std::vector<ScriptEntry> script;
		std::vector<PokeEntry> pokes;
		std::string screenshotDirectory;
		uint32_t screenshotEvery = 30;
		FILE* ramDump = nullptr;
};
