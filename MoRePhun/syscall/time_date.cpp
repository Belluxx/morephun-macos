#include "../mophun_os.h"
#include "../registers.h"
#include <chrono>

namespace {

void writeU16(uint8_t* destination, uint16_t value)
{
	destination[0] = static_cast<uint8_t>(value);
	destination[1] = static_cast<uint8_t>(value >> 8);
}

} // namespace

void MophunOS::vGetTickCount()
{
	 uint32_t cnt = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() - osdata.timer;
	 mophunVM->writeReg(r0, cnt);
}

void MophunOS::vGetTimeDate()
{
	uint8_t* const dateTime = mophunVM->getRamAddress(mophunVM->readReg(p0));

	// This release's certificate/date gate accepts the original launch period.
	writeU16(dateTime + 0, 2003);
	writeU16(dateTime + 2, 3);
	dateTime[4] = 11;
	dateTime[5] = 12;
	dateTime[6] = 0;
	dateTime[7] = 0;
}
