cmake_minimum_required(VERSION 3.18)

project(MoRePhun LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(OPENSSL REQUIRED IMPORTED_TARGET openssl)

# MoRePhun
file(GLOB_RECURSE MOREPHUN_SOURCES MoRePhun/*.cpp)
file(GLOB_RECURSE MOREPHUN_HEADERS MoRePhun/*.h)

set(SOURCES ${MOREPHUN_SOURCES})

SET(HEADERS ${MOREPHUN_HEADERS})

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_link_libraries(${PROJECT_NAME} PRIVATE PkgConfig::SDL2 PkgConfig::OPENSSL)

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

add_executable(MPNInspect tools/mpn_inspect.cpp)

if(BUILD_TESTING)
	add_executable(StorageTests
		tests/storage_tests.cpp
		MoRePhun/storage.cpp
		MoRePhun/storage.h)
	target_include_directories(StorageTests PRIVATE MoRePhun)
	target_link_libraries(StorageTests PRIVATE PkgConfig::SDL2)
	add_test(NAME StorageTests COMMAND StorageTests)
endif()
