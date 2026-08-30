#include "mophunmod/pip_assembler.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool require(bool condition, const std::string& message)
{
	if (!condition)
		std::cerr << "Modding assembler test failed: " << message << '\n';
	return condition;
}

uint32_t readU32(const uint8_t* bytes)
{
	return static_cast<uint32_t>(bytes[0]) |
		static_cast<uint32_t>(bytes[1]) << 8 |
		static_cast<uint32_t>(bytes[2]) << 16 |
		static_cast<uint32_t>(bytes[3]) << 24;
}

template <typename Function>
bool throwsAssemblerError(Function function)
{
	try
	{
		function();
	}
	catch (const mophunmod::AssemblerError&)
	{
		return true;
	}
	return false;
}

} // namespace

int main()
{
	using namespace mophunmod;
	bool success = true;

	PipAssembler assembler(0x100);
	const PipLabel begin("begin");
	const PipLabel end("end");
	assembler.label(begin);
	assembler.op(NOP);
	assembler.branchImmediate(BNEI, s0, 1, begin);
	assembler.call(end);
	assembler.label(end);
	const PipProgram program = assembler.finish();
	const std::vector<uint8_t>& code = program.bytes();
	success = require(code.size() == 16 && code[7] == 0xff,
		"backward short fixup uses signed instruction units") && success;
	success = require(readU32(code.data() + 12) == encodePipImmediate(8),
		"forward long fixup is relative to the instruction") && success;
	success = require(program.symbolOffset(end) == 0x110,
		"finalized symbol offsets include the code-section base") && success;
	success = require(throwsAssemblerError([&] { program.symbolOffset("missing"); }),
		"unknown finalized symbols have a diagnostic") && success;
	success = require(assembler.finish().bytes() == code,
		"finish is deterministic and repeatable") && success;

	PipAssembler forward;
	forward.branchImmediate(BEQI, r0, 0, "target");
	forward.op(NOP);
	forward.label("target");
	success = require(forward.finish().bytes()[3] == 2,
		"forward short fixup is resolved") && success;

	PipAssembler duplicate;
	duplicate.label("same");
	success = require(throwsAssemblerError([&] { duplicate.label("same"); }),
		"duplicate labels have a diagnostic") && success;
	PipAssembler undefined;
	undefined.jump("missing");
	success = require(throwsAssemblerError([&] { undefined.finish(); }),
		"undefined labels have a diagnostic") && success;
	PipAssembler distant;
	distant.branchImmediate(BEQI, r0, 0, "far");
	for (int index = 0; index < 128; ++index)
		distant.op(NOP);
	distant.label("far");
	success = require(throwsAssemblerError([&] { distant.finish(); }),
		"out-of-range short branches fail") && success;
	PipAssembler badPool;
	success = require(throwsAssemblerError([&] { badPool.callPool(0); }),
		"pool ID zero is rejected") && success;
	success = require(throwsAssemblerError([&] { encodePipImmediate(0x40000000); }),
		"unrepresentable immediates are rejected") && success;

	return success ? 0 : 1;
}
