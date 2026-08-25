#pragma once

#include <cstdint>
#include <string>
#include "mophun_vm.h"
#include "syscall/os_data.h"
#include "video.h"
#include "input.h"


class MophunOS {

	public:
		MophunOS();
		~MophunOS();
		bool loadRom(const std::string& romPath);
		void emulate(uint64_t maxInstructions = 0);

	private:
		MophunVM* mophunVM = new MophunVM();
		Video* video = new Video();
		Input* input = new Input();
		OSData osdata;
		bool status;
		void setupSyscalls();

		std::unordered_map<std::string, std::function<void()>> syscalls;

		// Debug api
		void DbgPrintf();
		std::string getStringFromMemory(uint32_t addr);

		// Graphics api
		void vClearScreen();
		void vDrawFlatPolygon();
		void vDrawLine();
		void vDrawObject();
		void vFillRect();
		void vFlipScreen();
		void vGetPaletteEntry();
		void vSetClipWindow();
		void vSetForeColor();
		void vSetPaletteEntry();
		void vSetTransferMode();
		void vSpriteInit();
		void vSpriteClear();
		void vSpriteSet();
		void vUpdateSprite();
		void vSetActiveFont();
		void vSelectFont();
		void vPrint();
		void vTextExtent();
		void vTextExtentU();
		void vTextOut();
		void vTextOutU();

		// System api
		void vCheckDataCert();
		void vCheckIMEI();
		void vGetCaps();
		void vDecompress();
		void vGetRandom();
		void vSetRandom();
		void vSysCtl();
		void vTerminateVMGP();
		void vUID();
		void vMsgBox();
		void vMsgBoxU();
		void vPlayResource();

		// Stream IO api
		void vStreamClose();
		void vStreamOpen();
		void vStreamRead();
		void vStreamReady();
		void vStreamSeek();
		void vStreamWrite();

		// String api
		void vStrCpy();
		void vStrLen();
		void vitoa();

		// Tileamp and Sprite api
		void vMapInit();
		void vUpdateMap();
		void vSpriteCollision();

		// Input api
		void vGetButtonData();

		// Time/data api
		void vGetTickCount();
		void vGetTimeDate();
};
