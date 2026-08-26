#include "../mophun_os.h"
#include "../registers.h"
#include "../binary_io.h"
#include <cstring>
#include <sstream>

void MophunOS::DbgPrintf()
{
	if (mophunVM->readReg(sp) == RAM_SIZE) // FIXME THI IS WRONG!!
		return;
	const std::string& str = reinterpret_cast<char*>(mophunVM->getRamAddress(mophunVM->readReg(p0)));
	std::stringstream ss;
	uint32_t stackTmpPnt = mophunVM->readReg(sp);
	for (size_t i = 0; i < str.size();)
	{
		if (str[i] == '%')
		{
			i++;
			if (i >= str.size())
				break;
			switch (str[i])
			{
			case 's':
				ss << getStringFromMemory(stackTmpPnt);
				stackTmpPnt += sizeof(uint32_t);
				break;
			case '%':
				ss << '%';
				break;
			case 'd':
				ss << static_cast<int32_t>(readLittleU32(mophunVM->getRamAddress(stackTmpPnt)));
				stackTmpPnt += sizeof(uint32_t);
				break;
			case 'l':
			case 'f':
			{
				const uint8_t* const bytes = mophunVM->getRamAddress(stackTmpPnt);
				const uint64_t bits = static_cast<uint64_t>(readLittleU32(bytes)) |
					static_cast<uint64_t>(readLittleU32(bytes + 4)) << 32;
				double value = 0;
				std::memcpy(&value, &bits, sizeof(value));
				ss << value;
				stackTmpPnt += sizeof(uint64_t);
				break;
			}
			default:
				ss << "%" << str[i];
				break;
			}
			i++;
		}
		else
		{
			ss << str[i];
			i++;
		}
	}

	std::cout << ss.str() << std::endl;
}

std::string MophunOS::getStringFromMemory(uint32_t addr)
{
	const uint32_t ref = readLittleU32(mophunVM->getRamAddress(addr));
	return reinterpret_cast<char*>(mophunVM->getRamAddress(ref));
}
