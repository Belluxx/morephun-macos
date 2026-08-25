#include "mophun_vm.h"
#include <string>



PoolData MophunVM::decodePoolItem(uint32_t index)
{
	const auto& poolItem = reinterpret_cast<PoolItem*>(memory.ram.data() + memory.poolSegStartAddr)[index];
	PoolData poolData;

	if (poolItem.segment_1 == 0x4)
	{
		if(poolItem.segment_0 != 2)
			throw std::runtime_error("!!! Pool handler error !!!");
		if (poolItem.segmentoffset == 2)
			*reinterpret_cast<uint32_t*>(std::addressof(memory.ram[memory.dataSegStartAddr + poolItem.extra])) += memory.dataSegStartAddr;
		else if (poolItem.segmentoffset == 1)
			*reinterpret_cast<uint32_t*>(std::addressof(memory.ram[memory.dataSegStartAddr + poolItem.extra])) += memory.codeSegStartAddr;
		else if (poolItem.segmentoffset == 4)
			*reinterpret_cast<uint32_t*>(std::addressof(memory.ram[memory.dataSegStartAddr + poolItem.extra])) += memory.bssSegStartAddr;
		else
			throw std::runtime_error("Unsupported relocation segment " + std::to_string(poolItem.segmentoffset));
	}
	else if (poolItem.segment_1 == 0x8)
	{
		poolData.value = poolItem.extra + decodePoolItem(poolItem.segmentoffset-1).value;
	}
	else
	{
		switch (poolItem.segment_0)
		{
		case 0:		// string segment
			poolData.isSyscall = true;
			poolData.value = memory.stringSegStartAddr + poolItem.segmentoffset;
			break;
		case 1:		// code segment
			poolData.value = memory.codeSegStartAddr + poolItem.extra;
			break;
		case 2:		// data segment
			poolData.value = memory.dataSegStartAddr + poolItem.extra;
			break;
		case 4:		// bss segment
			poolData.value = memory.bssSegStartAddr + poolItem.extra;
			break;
		case 6:		// immediate float
			poolData.value = poolItem.extra;
			break;
		default:
			throw std::runtime_error("!!! Pool handler error: " + std::to_string(poolItem.segment_0));
			break;
		}
	}
	return poolData;
}

void MophunVM::poolParser()
{
	int totalPoolItems = (memory.stringSegStartAddr - memory.poolSegStartAddr) / sizeof(PoolItem);
	poolDataList.resize(totalPoolItems);
	for (int i = 0; i < totalPoolItems; i++)
	{
		poolDataList[i] = decodePoolItem(i);
	}
}
