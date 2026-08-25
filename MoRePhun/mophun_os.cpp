#include "mophun_os.h"
#include "registers.h"
#include <chrono>
#include <cstdlib>

MophunOS::MophunOS()
{
	status = true;
	osdata.timer = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	osdata.streamCounter = 0;

	syscalls["DbgPrintf"] = std::bind(&MophunOS::DbgPrintf, this);
	syscalls["vCheckDataCert"] = std::bind(&MophunOS::vCheckDataCert, this);
	syscalls["vCheckIMEI"] = std::bind(&MophunOS::vCheckIMEI, this);
	syscalls["vClearScreen"] = std::bind(&MophunOS::vClearScreen, this);
	syscalls["vDecompress"] = std::bind(&MophunOS::vDecompress, this);
	syscalls["vDrawFlatPolygon"] = std::bind(&MophunOS::vDrawFlatPolygon, this);
	syscalls["vDrawLine"] = std::bind(&MophunOS::vDrawLine, this);
	syscalls["vDrawObject"] = std::bind(&MophunOS::vDrawObject, this);
	syscalls["vFillRect"] = std::bind(&MophunOS::vFillRect, this);
	syscalls["vFlipScreen"] = std::bind(&MophunOS::vFlipScreen, this);
	syscalls["vGetButtonData"] = std::bind(&MophunOS::vGetButtonData, this);
	syscalls["vGetCaps"] = std::bind(&MophunOS::vGetCaps, this);
	syscalls["vGetPaletteEntry"] = std::bind(&MophunOS::vGetPaletteEntry, this);
	syscalls["vGetRandom"] = std::bind(&MophunOS::vGetRandom, this);
	syscalls["vGetTickCount"] = std::bind(&MophunOS::vGetTickCount, this);
	syscalls["vMapInit"] = std::bind(&MophunOS::vMapInit, this);
	syscalls["vGetTimeDate"] = std::bind(&MophunOS::vGetTimeDate, this);
	syscalls["vPrint"] = std::bind(&MophunOS::vPrint, this);
	syscalls["vMsgBox"] = std::bind(&MophunOS::vMsgBox, this);
	syscalls["vMsgBoxU"] = std::bind(&MophunOS::vMsgBoxU, this);
	syscalls["vPlayResource"] = std::bind(&MophunOS::vPlayResource, this);
	syscalls["vSelectFont"] = std::bind(&MophunOS::vSelectFont, this);
	syscalls["vSetActiveFont"] = std::bind(&MophunOS::vSetActiveFont, this);
	syscalls["vSetForeColor"] = std::bind(&MophunOS::vSetForeColor, this);
	syscalls["vSetClipWindow"] = std::bind(&MophunOS::vSetClipWindow, this);
	syscalls["vSetPaletteEntry"] = std::bind(&MophunOS::vSetPaletteEntry, this);
	syscalls["vSetRandom"] = std::bind(&MophunOS::vSetRandom, this);
	syscalls["vSetTransferMode"] = std::bind(&MophunOS::vSetTransferMode, this);
	syscalls["vSpriteClear"] = std::bind(&MophunOS::vSpriteClear, this);
	syscalls["vSpriteCollision"] = std::bind(&MophunOS::vSpriteCollision, this);
	syscalls["vSpriteInit"] = std::bind(&MophunOS::vSpriteInit, this);
	syscalls["vSpriteSet"] = std::bind(&MophunOS::vSpriteSet, this);
	syscalls["vStrCpy"] = std::bind(&MophunOS::vStrCpy, this);
	syscalls["vStrLen"] = std::bind(&MophunOS::vStrLen, this);
	syscalls["vStreamClose"] = std::bind(&MophunOS::vStreamClose, this);
	syscalls["vStreamOpen"] = std::bind(&MophunOS::vStreamOpen, this);
	syscalls["vStreamRead"] = std::bind(&MophunOS::vStreamRead, this);
	syscalls["vStreamReady"] = std::bind(&MophunOS::vStreamReady, this);
	syscalls["vStreamSeek"] = std::bind(&MophunOS::vStreamSeek, this);
	syscalls["vStreamWrite"] = std::bind(&MophunOS::vStreamWrite, this);
	syscalls["vSysCtl"] = std::bind(&MophunOS::vSysCtl, this);
	syscalls["vTerminateVMGP"] = std::bind(&MophunOS::vTerminateVMGP, this);
	syscalls["vTextExtent"] = std::bind(&MophunOS::vTextExtent, this);
	syscalls["vTextExtentU"] = std::bind(&MophunOS::vTextExtentU, this);
	syscalls["vTextOut"] = std::bind(&MophunOS::vTextOut, this);
	syscalls["vTextOutU"] = std::bind(&MophunOS::vTextOutU, this);
	syscalls["vUID"] = std::bind(&MophunOS::vUID, this);
	syscalls["vUpdateMap"] = std::bind(&MophunOS::vUpdateMap, this);
	syscalls["vUpdateSprite"] = std::bind(&MophunOS::vUpdateSprite, this);
	syscalls["vitoa"] = std::bind(&MophunOS::vitoa, this);
}


MophunOS::~MophunOS()
{
	// Clean sprites
	for (size_t i = 0; i < osdata.spriteSlots.size(); i++)
	{
		SDL_DestroyTexture(osdata.spriteSlots[i].spriteTexture);
	}

	// Clean open files
	for (auto it = osdata.streamSlots.begin(); it != osdata.streamSlots.end(); ++it)
	{
		if (it->second.fd != nullptr)
			fclose(it->second.fd);
	}

	delete mophunVM;
	delete video;
	delete input;
}

bool MophunOS::loadRom(const std::string& romPath)
{
	if (mophunVM->loadRom(romPath))
	{
		std::cout << "ROM loaded: " << romPath << std::endl;
	}
	else {
		std::cerr << "Unable to load ROM: " << romPath << std::endl;
		return false;
	}
	return true;
}

void MophunOS::emulate(uint64_t maxInstructions)
{
	setupSyscalls();
	uint64_t executedInstructions = 0;
	while (status && (maxInstructions == 0 || executedInstructions < maxInstructions))
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_QUIT)
				status = false;
		}
		if (!status)
			break;

		mophunVM->emulate();
		executedInstructions++;
	}

	if (status && maxInstructions != 0 && executedInstructions == maxInstructions)
		std::cout << "Instruction limit reached: " << executedInstructions << std::endl;
}

void MophunOS::setupSyscalls()
{
	std::vector<PoolData>* poolDataList = mophunVM->getPoolEntries();
	const bool traceSyscalls = std::getenv("MOPHUN_TRACE_SYSCALLS") != nullptr;

	for (PoolData &poolData: *poolDataList)
	{
		if (!poolData.isSyscall)
			continue;
		std::string syscall = reinterpret_cast<char*>(mophunVM->getRamAddress(poolData.value));
		if (syscall.length() == 0)
			continue;

		auto iter = syscalls.find(syscall);
		if (iter == syscalls.end())
		{
			std::cout << "Unimplemented syscall: " << syscall << std::endl;
			poolData.fun = [this, syscall]() {
				std::cerr << "Stopping at unsupported syscall: " << syscall << std::endl;
				status = false;
			};
			continue;
		}

		const std::function<void()> implementation = iter->second;
		poolData.fun = [this, syscall, implementation, traceSyscalls]() {
			if (traceSyscalls && syscall != "vGetTickCount" && syscall != "vGetButtonData")
			{
				std::cout << syscall << "(0x" << std::hex << mophunVM->readReg(p0)
					<< ", 0x" << mophunVM->readReg(p1) << ", 0x" << mophunVM->readReg(p2)
					<< ", 0x" << mophunVM->readReg(p3) << ")" << std::dec << std::endl;
			}
			implementation();
		};
	}
}
