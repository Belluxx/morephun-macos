cmake_minimum_required(VERSION 3.18)

project(MoRePhun LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MOPHUN_BUILD_VRALLY2_STANDALONE
	"Build the V-Rally 2 launcher, optionally with user-supplied assets embedded"
	OFF)
set(MOPHUN_VRALLY2_ASSET_DIR "" CACHE PATH
	"Optional directory containing V-Rally 2 MPN and MPC files to embed")
set(MOPHUN_VRALLY2_GAME_FILE "" CACHE FILEPATH
	"Optional V-Rally 2 MPN file to embed; MPC files are read from the same directory")
option(MOPHUN_ENABLE_FLUIDSYNTH "Enable FluidSynth MIDI playback on Linux" ON)
option(MOPHUN_REQUIRE_FLUIDSYNTH
	"Fail configuration when Linux MIDI support is unavailable" OFF)

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

set(MOPHUN_HAVE_FLUIDSYNTH OFF)
if(APPLE)
	find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
	find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND MOPHUN_ENABLE_FLUIDSYNTH)
	if(NOT PkgConfig_FOUND)
		find_package(PkgConfig QUIET)
	endif()
	if(PkgConfig_FOUND)
		pkg_check_modules(FLUIDSYNTH QUIET IMPORTED_TARGET fluidsynth>=2.0.5)
	endif()
	if(FLUIDSYNTH_FOUND)
		set(MOPHUN_HAVE_FLUIDSYNTH ON)
		message(STATUS "Linux MIDI playback enabled through FluidSynth")
	elseif(MOPHUN_REQUIRE_FLUIDSYNTH)
		message(FATAL_ERROR
			"FluidSynth 2.0.5+ is required; install its development package or disable MOPHUN_REQUIRE_FLUIDSYNTH")
	else()
		message(WARNING
			"FluidSynth 2.0.5+ was not found; Linux builds will run without MIDI audio")
	endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND MOPHUN_REQUIRE_FLUIDSYNTH)
	message(FATAL_ERROR
		"MOPHUN_REQUIRE_FLUIDSYNTH cannot be used when MOPHUN_ENABLE_FLUIDSYNTH is disabled")
endif()

function(configure_morephun_target TARGET)
	target_link_libraries(${TARGET} PRIVATE ${MOPHUN_SDL_TARGET})
	if(APPLE)
		target_link_libraries(${TARGET} PRIVATE
			${AUDIOTOOLBOX_FRAMEWORK}
			${COREFOUNDATION_FRAMEWORK})
	elseif(MOPHUN_HAVE_FLUIDSYNTH)
		target_compile_definitions(${TARGET} PRIVATE MOPHUN_HAVE_FLUIDSYNTH=1)
		target_link_libraries(${TARGET} PRIVATE PkgConfig::FLUIDSYNTH)
	endif()
endfunction()

add_library(mophunmod
	lib/mophunmod/mpn_image.cpp
	lib/mophunmod/mpn_image.h
	lib/mophunmod/patch_builder.cpp
	lib/mophunmod/patch_builder.h
	lib/mophunmod/pip_assembler.cpp
	lib/mophunmod/pip_assembler.h
	lib/mophunmod/pool_table.cpp
	lib/mophunmod/pool_table.h)
target_include_directories(mophunmod PUBLIC lib)

# MoRePhun
file(GLOB_RECURSE MOREPHUN_SOURCES MoRePhun/*.cpp)
file(GLOB_RECURSE MOREPHUN_HEADERS MoRePhun/*.h)
# Native turbo gameplay is injected into the guest MPN by VRally2TurboMod. Its
# offline cinematic generator is not part of the emulator runtime.
list(FILTER MOREPHUN_SOURCES EXCLUDE REGEX "/turbo/")
list(FILTER MOREPHUN_HEADERS EXCLUDE REGEX "/turbo/")

set(SOURCES ${MOREPHUN_SOURCES})

SET(HEADERS ${MOREPHUN_HEADERS})

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

configure_morephun_target(${PROJECT_NAME})

if(APPLE)
	add_executable(AudioProbe tools/audio_probe.cpp MoRePhun/audio.cpp MoRePhun/audio.h)
	target_include_directories(AudioProbe PRIVATE MoRePhun)
	target_link_libraries(AudioProbe PRIVATE
		${MOPHUN_SDL_TARGET}
		${AUDIOTOOLBOX_FRAMEWORK}
		${COREFOUNDATION_FRAMEWORK})
elseif(MOPHUN_HAVE_FLUIDSYNTH)
	add_executable(AudioProbe tools/audio_probe.cpp MoRePhun/audio.cpp MoRePhun/audio.h)
	target_include_directories(AudioProbe PRIVATE MoRePhun)
	target_compile_definitions(AudioProbe PRIVATE MOPHUN_HAVE_FLUIDSYNTH=1)
	target_link_libraries(AudioProbe PRIVATE ${MOPHUN_SDL_TARGET} PkgConfig::FLUIDSYNTH)
endif()

if(MOPHUN_BUILD_VRALLY2_STANDALONE)
	if(NOT APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
		message(FATAL_ERROR "The V-Rally 2 standalone target is supported only on macOS and Linux")
	endif()

	set(VRALLY_EMBED_ASSETS OFF)
	if(MOPHUN_VRALLY2_GAME_FILE)
		get_filename_component(VRALLY_MPN "${MOPHUN_VRALLY2_GAME_FILE}" ABSOLUTE
			BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
		get_filename_component(VRALLY_ASSET_DIR "${VRALLY_MPN}" DIRECTORY)
		set(VRALLY_EMBED_ASSETS ON)
	elseif(MOPHUN_VRALLY2_ASSET_DIR)
		get_filename_component(VRALLY_ASSET_DIR "${MOPHUN_VRALLY2_ASSET_DIR}" ABSOLUTE
			BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
		set(VRALLY_MPN "${VRALLY_ASSET_DIR}/VRally2_[RC14EU]_[multiscreen]_M5.mpn")
		set(VRALLY_EMBED_ASSETS ON)
	endif()

	if(VRALLY_EMBED_ASSETS)
		if(NOT IS_DIRECTORY "${VRALLY_ASSET_DIR}")
			message(FATAL_ERROR "V-Rally 2 asset directory does not exist: ${VRALLY_ASSET_DIR}")
		endif()
		set(VRALLY_PACKS
			"${VRALLY_ASSET_DIR}/VRally2_multipack.mpc"
			"${VRALLY_ASSET_DIR}/VRally2_extrapack1.mpc"
			"${VRALLY_ASSET_DIR}/VRally2_extrapack2.mpc"
			"${VRALLY_ASSET_DIR}/VRally2_extrapack3.mpc"
			"${VRALLY_ASSET_DIR}/VRally2_extrapack4.mpc")
		foreach(GAME_FILE IN LISTS VRALLY_MPN VRALLY_PACKS)
			if(NOT EXISTS "${GAME_FILE}")
				message(FATAL_ERROR "Required local game file is missing: ${GAME_FILE}")
			endif()
		endforeach()
	endif()

	set(STANDALONE_SOURCES ${SOURCES})
	if(NOT VRALLY_EMBED_ASSETS)
		list(REMOVE_ITEM STANDALONE_SOURCES
			"${CMAKE_CURRENT_SOURCE_DIR}/MoRePhun/embedded_assets.cpp")
	elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
		add_executable(AssetEmbedder tools/embed_assets.cpp)
		set(GENERATED_ASSET_SOURCE
			"${CMAKE_CURRENT_BINARY_DIR}/generated/vrally2_embedded_assets.cpp")
		add_custom_command(
			OUTPUT "${GENERATED_ASSET_SOURCE}"
			COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
			COMMAND $<TARGET_FILE:AssetEmbedder>
				"${GENERATED_ASSET_SOURCE}"
				"${VRALLY_MPN}"
				${VRALLY_PACKS}
			DEPENDS AssetEmbedder "${VRALLY_MPN}" ${VRALLY_PACKS}
			COMMENT "Embedding user-supplied V-Rally 2 assets"
			VERBATIM)
		list(REMOVE_ITEM STANDALONE_SOURCES
			"${CMAKE_CURRENT_SOURCE_DIR}/MoRePhun/embedded_assets.cpp")
		list(APPEND STANDALONE_SOURCES "${GENERATED_ASSET_SOURCE}")
	endif()

	add_executable(VRally2Standalone ${STANDALONE_SOURCES} ${HEADERS})
	set_target_properties(VRally2Standalone PROPERTIES OUTPUT_NAME "V-Rally-2")
	target_compile_definitions(VRally2Standalone PRIVATE MOPHUN_STANDALONE=1)
	if(VRALLY_EMBED_ASSETS)
		target_compile_definitions(VRally2Standalone PRIVATE MOPHUN_EMBEDDED_ASSETS=1)
	endif()
	target_include_directories(VRally2Standalone PRIVATE MoRePhun)
	configure_morephun_target(VRally2Standalone)

	if(VRALLY_EMBED_ASSETS AND APPLE)
		function(embed_macho_section TARGET SECTION FILE_PATH)
			target_link_options(${TARGET} PRIVATE
				"LINKER:-sectcreate,__DATA,${SECTION},${FILE_PATH}")
			set_property(TARGET ${TARGET} APPEND PROPERTY LINK_DEPENDS "${FILE_PATH}")
		endfunction()
		embed_macho_section(VRally2Standalone __vr_mpn "${VRALLY_MPN}")
		embed_macho_section(VRally2Standalone __vr_multi "${VRALLY_ASSET_DIR}/VRally2_multipack.mpc")
		embed_macho_section(VRally2Standalone __vr_extra1 "${VRALLY_ASSET_DIR}/VRally2_extrapack1.mpc")
		embed_macho_section(VRally2Standalone __vr_extra2 "${VRALLY_ASSET_DIR}/VRally2_extrapack2.mpc")
		embed_macho_section(VRally2Standalone __vr_extra3 "${VRALLY_ASSET_DIR}/VRally2_extrapack3.mpc")
		embed_macho_section(VRally2Standalone __vr_extra4 "${VRALLY_ASSET_DIR}/VRally2_extrapack4.mpc")
	endif()
endif()

add_executable(MPNInspect tools/mpn_inspect.cpp)

add_executable(PIPDisasm
	tools/pip_disasm.cpp
	MoRePhun/rom_decrypt.cpp)
target_include_directories(PIPDisasm PRIVATE MoRePhun)

add_executable(VRally2TurboMod
	tools/vrally2_turbo_mod.cpp
	MoRePhun/rom_decrypt.cpp
	MoRePhun/turbo/cinematic.cpp
	MoRePhun/turbo/retro3d.cpp)
target_include_directories(VRally2TurboMod PRIVATE MoRePhun)
target_link_libraries(VRally2TurboMod PRIVATE mophunmod ${MOPHUN_SDL_TARGET})

if(BUILD_TESTING)
	if(TARGET VRally2Standalone)
		add_test(NAME VRally2StandaloneHelp COMMAND VRally2Standalone --help)
	endif()

	add_executable(BinaryFormatTests tests/binary_format_tests.cpp)
	target_include_directories(BinaryFormatTests PRIVATE MoRePhun)
	add_test(NAME BinaryFormatTests COMMAND BinaryFormatTests)

	add_executable(ModdingMpnTests tests/modding_mpn_tests.cpp)
	target_link_libraries(ModdingMpnTests PRIVATE mophunmod)
	add_test(NAME ModdingMpnTests COMMAND ModdingMpnTests)

	add_executable(ModdingAssemblerTests tests/modding_assembler_tests.cpp)
	target_link_libraries(ModdingAssemblerTests PRIVATE mophunmod)
	add_test(NAME ModdingAssemblerTests COMMAND ModdingAssemblerTests)

	add_executable(ModdingPatchTests tests/modding_patch_tests.cpp)
	target_link_libraries(ModdingPatchTests PRIVATE mophunmod)
	add_test(NAME ModdingPatchTests COMMAND ModdingPatchTests)

	add_executable(StorageTests
		tests/storage_tests.cpp
		MoRePhun/storage.cpp
		MoRePhun/storage.h)
	target_include_directories(StorageTests PRIVATE MoRePhun)
	target_link_libraries(StorageTests PRIVATE ${MOPHUN_SDL_TARGET})
	add_test(NAME StorageTests COMMAND StorageTests)
endif()
