cmake_minimum_required(VERSION 3.18)

project(MoRePhun LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MOPHUN_BUILD_VRALLY2_STANDALONE
	"Build a local macOS executable with user-supplied V-Rally 2 assets embedded"
	OFF)
set(MOPHUN_VRALLY2_ASSET_DIR "" CACHE PATH
	"Directory containing V-Rally 2 MPN and MPC files")

set(MOPHUN_STATIC_SDL2_ROOT "" CACHE PATH "Prefix containing a static SDL2 installation")
if(MOPHUN_STATIC_SDL2_ROOT)
	find_package(SDL2 REQUIRED CONFIG
		PATHS "${MOPHUN_STATIC_SDL2_ROOT}/lib/cmake/SDL2"
		NO_DEFAULT_PATH)
	if(NOT TARGET SDL2::SDL2-static)
		message(FATAL_ERROR "MOPHUN_STATIC_SDL2_ROOT does not provide SDL2::SDL2-static")
	endif()
	set(MOPHUN_SDL_TARGET SDL2::SDL2-static)
else()
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
	set(MOPHUN_SDL_TARGET PkgConfig::SDL2)
endif()

# MoRePhun
file(GLOB_RECURSE MOREPHUN_SOURCES MoRePhun/*.cpp)
file(GLOB_RECURSE MOREPHUN_HEADERS MoRePhun/*.h)

set(SOURCES ${MOREPHUN_SOURCES})

SET(HEADERS ${MOREPHUN_HEADERS})

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_link_libraries(${PROJECT_NAME} PRIVATE ${MOPHUN_SDL_TARGET})

if(APPLE)
	find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
	find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
	target_link_libraries(${PROJECT_NAME} PRIVATE
		${AUDIOTOOLBOX_FRAMEWORK}
		${COREFOUNDATION_FRAMEWORK})

	add_executable(AudioProbe tools/audio_probe.cpp MoRePhun/audio.cpp MoRePhun/audio.h)
	target_include_directories(AudioProbe PRIVATE MoRePhun)
	target_link_libraries(AudioProbe PRIVATE
		${AUDIOTOOLBOX_FRAMEWORK}
		${COREFOUNDATION_FRAMEWORK})
endif()

if(MOPHUN_BUILD_VRALLY2_STANDALONE)
	if(NOT APPLE)
		message(FATAL_ERROR "The embedded V-Rally 2 target is supported only on macOS")
	endif()
	if(NOT IS_DIRECTORY "${MOPHUN_VRALLY2_ASSET_DIR}")
		message(FATAL_ERROR
			"Set MOPHUN_VRALLY2_ASSET_DIR to a directory containing the game files")
	endif()

	set(VRALLY_MPN "${MOPHUN_VRALLY2_ASSET_DIR}/VRally2_[RC14EU]_[multiscreen]_M5.mpn")
	set(VRALLY_PACKS
		"${MOPHUN_VRALLY2_ASSET_DIR}/VRally2_multipack.mpc"
		"${MOPHUN_VRALLY2_ASSET_DIR}/VRally2_extrapack1.mpc"
		"${MOPHUN_VRALLY2_ASSET_DIR}/VRally2_extrapack2.mpc"
		"${MOPHUN_VRALLY2_ASSET_DIR}/VRally2_extrapack3.mpc"
		"${MOPHUN_VRALLY2_ASSET_DIR}/VRally2_extrapack4.mpc")
	foreach(GAME_FILE IN LISTS VRALLY_MPN VRALLY_PACKS)
		if(NOT EXISTS "${GAME_FILE}")
			message(FATAL_ERROR "Required local game file is missing: ${GAME_FILE}")
		endif()
	endforeach()

	add_executable(VRally2Standalone ${SOURCES} ${HEADERS})
	set_target_properties(VRally2Standalone PROPERTIES OUTPUT_NAME "V-Rally-2")
	target_compile_definitions(VRally2Standalone PRIVATE MOPHUN_STANDALONE=1)
	target_link_libraries(VRally2Standalone PRIVATE
		${MOPHUN_SDL_TARGET}
		${AUDIOTOOLBOX_FRAMEWORK}
		${COREFOUNDATION_FRAMEWORK})

	function(embed_macho_section TARGET SECTION FILE_PATH)
		target_link_options(${TARGET} PRIVATE
			"LINKER:-sectcreate,__DATA,${SECTION},${FILE_PATH}")
		set_property(TARGET ${TARGET} APPEND PROPERTY LINK_DEPENDS "${FILE_PATH}")
	endfunction()
	embed_macho_section(VRally2Standalone __vr_mpn "${VRALLY_MPN}")
	embed_macho_section(VRally2Standalone __vr_multi "${MOPHUN_VRALLY2_ASSET_DIR}/VRally2_multipack.mpc")
	embed_macho_section(VRally2Standalone __vr_extra1 "${MOPHUN_VRALLY2_ASSET_DIR}/VRally2_extrapack1.mpc")
	embed_macho_section(VRally2Standalone __vr_extra2 "${MOPHUN_VRALLY2_ASSET_DIR}/VRally2_extrapack2.mpc")
	embed_macho_section(VRally2Standalone __vr_extra3 "${MOPHUN_VRALLY2_ASSET_DIR}/VRally2_extrapack3.mpc")
	embed_macho_section(VRally2Standalone __vr_extra4 "${MOPHUN_VRALLY2_ASSET_DIR}/VRally2_extrapack4.mpc")
endif()

add_executable(MPNInspect tools/mpn_inspect.cpp)

if(BUILD_TESTING)
	add_executable(StorageTests
		tests/storage_tests.cpp
		MoRePhun/storage.cpp
		MoRePhun/storage.h)
	target_include_directories(StorageTests PRIVATE MoRePhun)
	target_link_libraries(StorageTests PRIVATE ${MOPHUN_SDL_TARGET})
	add_test(NAME StorageTests COMMAND StorageTests)
endif()
