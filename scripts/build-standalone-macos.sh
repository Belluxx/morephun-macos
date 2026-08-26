#!/bin/sh
set -eu

if [ "$#" -gt 1 ]; then
	echo "Usage: $0 [game-directory-or-mpn-file]" >&2
	exit 2
fi

script_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_directory/.." && pwd)
build_root="$project_root/build/standalone-release"
downloads="$project_root/build/vendor-downloads"
sdl_source="$project_root/build/vendor-src/SDL2-2.32.10"
sdl_build="$project_root/build/vendor-build/sdl2-static-release"
sdl_install="$project_root/build/vendor-install/sdl2-static-release"
sdl_archive="$downloads/SDL2-2.32.10.tar.gz"
sdl_url="https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz"
sdl_sha256="5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165"
asset_directory=
game_file=

if [ "$(uname -s)" != "Darwin" ] || [ "$(uname -m)" != "arm64" ]; then
	echo "This builder requires an Apple Silicon Mac." >&2
	exit 1
fi

for command_name in cmake codesign curl shasum tar; do
	if ! command -v "$command_name" >/dev/null 2>&1; then
		echo "Missing build tool: $command_name" >&2
		exit 1
	fi
done

if [ "$#" -eq 1 ]; then
	if [ -d "$1" ]; then
		asset_directory=$(CDPATH= cd -- "$1" && pwd)
		game_file="$asset_directory/VRally2_[RC14EU]_[multiscreen]_M5.mpn"
	elif [ -f "$1" ]; then
		asset_directory=$(CDPATH= cd -- "$(dirname -- "$1")" && pwd)
		game_file="$asset_directory/$(basename -- "$1")"
	else
		echo "Game path is not a directory or file: $1" >&2
		exit 1
	fi

	for asset_path in \
		"$game_file" \
		"$asset_directory/VRally2_multipack.mpc" \
		"$asset_directory/VRally2_extrapack1.mpc" \
		"$asset_directory/VRally2_extrapack2.mpc" \
		"$asset_directory/VRally2_extrapack3.mpc" \
		"$asset_directory/VRally2_extrapack4.mpc"
	do
		if [ ! -f "$asset_path" ]; then
			echo "Missing required local asset: $asset_path" >&2
			exit 1
		fi
	done
fi

mkdir -p "$downloads" "$project_root/build/vendor-src" "$sdl_build" "$sdl_install" "$build_root" "$project_root/dist"

if [ ! -f "$sdl_archive" ]; then
	echo "Downloading SDL 2.32.10 source..."
	curl --fail --location --output "$sdl_archive" "$sdl_url"
fi

actual_sha256=$(shasum -a 256 "$sdl_archive" | awk '{print $1}')
if [ "$actual_sha256" != "$sdl_sha256" ]; then
	echo "SDL archive checksum mismatch: $sdl_archive" >&2
	exit 1
fi

if [ ! -f "$sdl_source/CMakeLists.txt" ]; then
	echo "Extracting SDL source..."
	tar -xzf "$sdl_archive" -C "$project_root/build/vendor-src"
fi

echo "Building static SDL2 for arm64 macOS 11+..."
cmake -S "$sdl_source" -B "$sdl_build" \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_OSX_ARCHITECTURES=arm64 \
	-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
	-DCMAKE_INSTALL_PREFIX="$sdl_install" \
	-DSDL_SHARED=OFF \
	-DSDL_STATIC=ON \
	-DSDL_TESTS=OFF \
	-DSDL_INSTALL_TESTS=OFF
cmake --build "$sdl_build" --target install --parallel

if [ -n "$game_file" ]; then
	echo "Building a local executable with user-supplied assets embedded..."
else
	echo "Building a standalone launcher without embedded game data..."
fi
if [ -n "$game_file" ]; then
	cmake -S "$project_root" -B "$build_root" \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_OSX_ARCHITECTURES=arm64 \
		-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
		-DMOPHUN_STATIC_SDL2_ROOT="$sdl_install" \
		-DMOPHUN_BUILD_VRALLY2_STANDALONE=ON \
		-DMOPHUN_VRALLY2_ASSET_DIR= \
		-DMOPHUN_VRALLY2_GAME_FILE="$game_file" \
		-DBUILD_TESTING=ON
else
	cmake -S "$project_root" -B "$build_root" \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_OSX_ARCHITECTURES=arm64 \
		-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
		-DMOPHUN_STATIC_SDL2_ROOT="$sdl_install" \
		-DMOPHUN_BUILD_VRALLY2_STANDALONE=ON \
		-DMOPHUN_VRALLY2_ASSET_DIR= \
		-DMOPHUN_VRALLY2_GAME_FILE= \
		-DBUILD_TESTING=ON
fi
cmake --build "$build_root" --target VRally2Standalone BinaryFormatTests StorageTests --parallel
ctest --test-dir "$build_root" --output-on-failure
codesign --force --sign - --timestamp=none "$build_root/V-Rally-2"
codesign --verify --strict "$build_root/V-Rally-2"

if [ -n "$game_file" ]; then
	smoke_saves="$build_root/smoke-saves"
	mkdir -p "$smoke_saves"
	SDL_VIDEODRIVER=dummy MOPHUN_DISABLE_AUDIO=1 MOPHUN_SAVE_DIR="$smoke_saves" \
		"$build_root/V-Rally-2" 2000000
fi

cmake -E copy "$build_root/V-Rally-2" "$project_root/dist/V-Rally-2"
# The turbo cinematic music is loaded at runtime from next to the launcher.
if [ -n "$asset_directory" ] && [ -f "$asset_directory/turbo_music.mp3" ]; then
	cmake -E copy "$asset_directory/turbo_music.mp3" "$project_root/dist/turbo_music.mp3"
elif [ -f "$project_root/assets/turbo_music.mp3" ]; then
	cmake -E copy "$project_root/assets/turbo_music.mp3" "$project_root/dist/turbo_music.mp3"
fi
if [ -n "$game_file" ]; then
	echo "Local standalone executable with embedded game data: $project_root/dist/V-Rally-2"
	echo "Do not redistribute it unless you have rights to every embedded asset."
else
	echo "Standalone launcher: $project_root/dist/V-Rally-2"
	echo "Run it with a game directory or MPN file: $project_root/dist/V-Rally-2 /path/to/game"
fi
