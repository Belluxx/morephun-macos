#pragma once

#include <cstdint>

class MophunVM;

// Player-car state as V-Rally 2 [RC14EU] keeps it in guest RAM. The layout was
// recovered by diffing per-frame memory dumps against scripted input:
//   +0x00  race-started flag (low byte becomes 1 when the lights go green)
//   +0x04  distance along the track (accumulates speed every frame)
//   +0x18  (lap << 16) | segment index
//   +0x24  target speed: recomputed by the game every frame from the car's
//          top speed, throttle, steering and road surface, then the current
//          speed eases toward it. Off-road surfaces cap it far lower.
//   +0x28  current speed in distance units per game frame
//   +0x34  lateral position on the road (16.16)
//   +0x3c  steering input (about +/-55000)
struct CarSnapshot {
	bool started = false;
	int32_t distance = 0;
	uint32_t lap = 0;
	uint32_t segment = 0;
	int32_t targetSpeed = 0;
	int32_t speed = 0;
	int32_t lateral = 0;
	int32_t steering = 0;
};

class VRally2Probe {
	public:
		VRally2Probe(MophunVM& vm, uint32_t carStructAddress);
		CarSnapshot read() const;
		// Overrides the target speed the game computed for this frame. Called
		// between the game's own computation and its physics step (i.e. from
		// the vGetButtonData hook) so the boost is applied on top of surface and
		// steering modifiers rather than replacing them.
		void writeTargetSpeed(int32_t value);

	private:
		MophunVM& vm;
		uint32_t base;
		int32_t readWord(uint32_t offset) const;
};
