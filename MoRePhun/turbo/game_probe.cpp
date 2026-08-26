#include "game_probe.h"
#include "../binary_io.h"
#include "../mophun_vm.h"

namespace {

constexpr uint32_t FlagOffset = 0x00;
constexpr uint32_t DistanceOffset = 0x04;
constexpr uint32_t SegmentOffset = 0x18;
constexpr uint32_t TargetSpeedOffset = 0x24;
constexpr uint32_t SpeedOffset = 0x28;
constexpr uint32_t LateralOffset = 0x34;
constexpr uint32_t SteeringOffset = 0x3c;

} // namespace

VRally2Probe::VRally2Probe(MophunVM& vm, uint32_t carStructAddress)
	: vm(vm), base(carStructAddress)
{
}

int32_t VRally2Probe::readWord(uint32_t offset) const
{
	return static_cast<int32_t>(readLittleU32(vm.getRamAddress(base + offset)));
}

CarSnapshot VRally2Probe::read() const
{
	CarSnapshot car;
	car.started = (static_cast<uint32_t>(readWord(FlagOffset)) & 0xffU) == 1;
	car.distance = readWord(DistanceOffset);
	const uint32_t lapAndSegment = static_cast<uint32_t>(readWord(SegmentOffset));
	car.lap = lapAndSegment >> 16;
	car.segment = lapAndSegment & 0xffffU;
	car.targetSpeed = readWord(TargetSpeedOffset);
	car.speed = readWord(SpeedOffset);
	car.lateral = readWord(LateralOffset);
	car.steering = readWord(SteeringOffset);
	return car;
}

void VRally2Probe::writeTargetSpeed(int32_t value)
{
	writeLittleU32(vm.getRamAddress(base + TargetSpeedOffset), static_cast<uint32_t>(value));
}
