#include "mophun_os.h"
#include <chrono>

MophunOS::MophunOS()
{
	status = true;
	osdata.timer = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	osdata.streamCounter = 0;

	syscalls["DbgPrintf"] = std::bind(&MophunOS::DbgPrintf, this);
	syscalls["vClearScreen"] = std::bind(&MophunOS::vClearScreen, this);
	syscalls["vFlipScreen"] = std::bind(&MophunOS::vFlipScreen, this);
	syscalls["vGetButtonData"] = std::bind(&MophunOS::vGetButtonData, this);
	syscalls["vGetRandom"] = std::bind(&MophunOS::vGetRandom, this);
	syscalls["vGetTickCount"] = std::bind(&MophunOS::vGetTickCount, this);
	syscalls["vPrint"] = std::bind(&MophunOS::vPrint, this);
	syscalls["vSetActiveFont"] = std::bind(&MophunOS::vSetActiveFont, this);
	syscalls["vSetForeColor"] = std::bind(&MophunOS::vSetForeColor, this);
	syscalls["vSpriteClear"] = std::bind(&MophunOS::vSpriteClear, this);
	syscalls["vSpriteCollision"] = std::bind(&MophunOS::vSpriteCollision, this);
	syscalls["vSpriteInit"] = std::bind(&MophunOS::vSpriteInit, this);
	syscalls["vSpriteSet"] = std::bind(&MophunOS::vSpriteSet, this);
	syscalls["vStrCpy"] = std::bind(&MophunOS::vStrCpy, this);
	syscalls["vStrLen"] = std::bind(&MophunOS::vStrLen, this);
	syscalls["vStreamClose"] = std::bind(&MophunOS::vStreamClose, this);
	syscalls["vStreamOpen"] = std::bind(&MophunOS::vStreamOpen, this);
	syscalls["vStreamRead"] = std::bind(&MophunOS::vStreamRead, this);
	syscalls["vStreamWrite"] = std::bind(&MophunOS::vStreamWrite, this);
	syscalls["vTerminateVMGP"] = std::bind(&MophunOS::vTerminateVMGP, this);
	syscalls["vUpdateSprite"] = std::bind(&MophunOS::vUpdateSprite, this);
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

		poolData.fun = iter->second;
	}
}
