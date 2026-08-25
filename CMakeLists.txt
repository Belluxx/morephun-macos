cmake_minimum_required(VERSION 3.18)

project(MoRePhun LANGUAGES CXX)

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

add_executable(MPNInspect tools/mpn_inspect.cpp)
