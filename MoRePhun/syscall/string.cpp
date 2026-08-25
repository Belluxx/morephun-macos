#include "../mophun_os.h"
#include "../registers.h"

#include <algorithm>
#include <string>

void MophunOS::vStrCpy()
{
	char* str1 = reinterpret_cast<char*>(mophunVM->getRamAddress(mophunVM->readReg(p0)));
	const char* str2 = reinterpret_cast<char*>(mophunVM->getRamAddress(mophunVM->readReg(p1)));

	if (str1 == nullptr)
		return;

	while (*str2 != '\0')
	{
		*str1 = *str2;
		str1++;
		str2++;
	}
	*str1 = '\0';
	// FIXME return str2 in r0
}

void MophunOS::vStrLen()
{
	char* str = reinterpret_cast<char*>(mophunVM->getRamAddress(mophunVM->readReg(p0)));
	int32_t len = std::strlen(str);
	mophunVM->writeReg(r0, len);
}

void MophunOS::vitoa()
{
	const int32_t value = static_cast<int32_t>(mophunVM->readReg(p0));
	const uint32_t bufferAddress = mophunVM->readReg(p1);
	const uint8_t minimumLength = static_cast<uint8_t>(mophunVM->readReg(p2));
	const char padding = static_cast<char>(mophunVM->readReg(p3));
	std::string converted = std::to_string(value);
	const int paddingCount = static_cast<int>(minimumLength) - static_cast<int>(converted.size());
	converted.append(static_cast<size_t>(std::max(0, paddingCount)), padding);
	char* const buffer = reinterpret_cast<char*>(mophunVM->getRamAddress(bufferAddress));
	std::memcpy(buffer, converted.c_str(), converted.size() + 1);
	mophunVM->writeReg(r0, bufferAddress + static_cast<uint32_t>(converted.size()));
}
