#include "embedded_assets.h"

#ifdef __APPLE__
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>
#endif

#include <stdexcept>
#include <string>


namespace {

EmbeddedAsset readAsset(const char* fileName, const char* logicalName, const char* sectionName)
{
#ifdef __APPLE__
	unsigned long size = 0;
	const uint8_t* data = getsectiondata(&_mh_execute_header, "__DATA", sectionName, &size);
	if (data == nullptr || size == 0)
		throw std::runtime_error(std::string("Missing embedded Mach-O section: ") + sectionName);
	return {fileName, logicalName, data, static_cast<size_t>(size)};
#else
	(void)fileName;
	(void)logicalName;
	(void)sectionName;
	throw std::runtime_error("Embedded game assets are supported only on macOS");
#endif
}

} // namespace

EmbeddedGameAssets getEmbeddedGameAssets()
{
	return {
		readAsset("VRally2_[RC14EU]_[multiscreen]_M5.mpn", "V-Rally 2", "__vr_mpn"),
		{{
			readAsset("VRally2_multipack.mpc", "multipack", "__vr_multi"),
			readAsset("VRally2_extrapack1.mpc", "extrapack1", "__vr_extra1"),
			readAsset("VRally2_extrapack2.mpc", "extrapack2", "__vr_extra2"),
			readAsset("VRally2_extrapack3.mpc", "extrapack3", "__vr_extra3"),
			readAsset("VRally2_extrapack4.mpc", "extrapack4", "__vr_extra4")
		}}
	};
}
