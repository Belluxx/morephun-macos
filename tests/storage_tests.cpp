#include "storage.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

bool require(bool condition, const std::string& message)
{
	if (!condition)
		std::cerr << "Storage test failed: " << message << std::endl;
	return condition;
}

bool writeFixture(const std::string& path)
{
	std::ofstream output(path, std::ios::binary);
	output << "MPC fixture";
	return output.good();
}

} // namespace

int main()
{
	char temporaryPath[] = "/tmp/morephun-storage-tests.XXXXXX";
	char* rootValue = mkdtemp(temporaryPath);
	if (!require(rootValue != nullptr, "could not create temporary directory"))
		return 1;

	const std::string root = rootValue;
	const std::string saves = root + "/saves";
	const std::string pack = root + "/VRally2_multipack.mpc";
	const std::string save = saves + "/career.sav";
	bool success = require(mkdir(saves.c_str(), 0700) == 0, "could not create save directory") &&
		require(writeFixture(pack), "could not create MPC fixture");

	if (success)
	{
		Storage storage(saves);
		storage.mountPacksNextTo(root + "/VRally2.mpn");

		std::string path;
		bool mountedPack = false;
		success = require(storage.resolveReadPath("multipack", path, mountedPack),
			"logical MPC name did not resolve") && success;
		success = require(mountedPack && path == pack, "logical MPC name resolved incorrectly") && success;
		success = require(storage.resolveReadPath("MuLtIpAcK.MpC", path, mountedPack),
			"case-insensitive MPC alias did not resolve") && success;
		success = require(mountedPack && path == pack, "MPC extension alias resolved incorrectly") && success;

		success = require(storage.resolveWritePath("career.sav", path),
			"save name did not resolve") && success;
		success = require(path == save, "save escaped preference directory") && success;
		success = require(writeFixture(path), "could not create resolved save file") && success;
		success = require(storage.resolveReadPath("career.sav", path, mountedPack),
			"created save did not resolve for reading") && success;
		success = require(!mountedPack && path == save, "created save resolved incorrectly") && success;
		success = require(storage.resolveWritePath("../career.sav", path),
			"unsafe guest name did not resolve safely") && success;
		success = require(path == saves + "/..%2Fcareer.sav",
			"guest path separator was not escaped") && success;

		success = require(storage.resolveWritePath("multipack", path),
			"write path for mounted name did not resolve") && success;
		success = require(path == saves + "/multipack",
			"write path attempted to overwrite a mounted MPC") && success;
	}

	std::remove(pack.c_str());
	std::remove(save.c_str());
	rmdir(saves.c_str());
	rmdir(root.c_str());
	return success ? 0 : 1;
}
