#!/bin/sh
set -eu

if [ "$#" -gt 1 ]; then
	echo "Usage: $0 [game-directory-or-mpn-file]" >&2
	exit 2
fi

script_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_directory/.." && pwd)
build_root="$project_root/build/standalone-linux-release"
asset_directory=
game_file=

if [ "$(uname -s)" != "Linux" ]; then
	echo "This builder requires Linux." >&2
	exit 1
fi

for command_name in cmake c++ pkg-config; do
	if ! command -v "$command_name" >/dev/null 2>&1; then
		echo "Missing build tool: $command_name" >&2
		exit 1
	fi
done

for package_name in sdl2 fluidsynth; do
	if ! pkg-config --exists "$package_name"; then
		echo "Missing development package exposed through pkg-config: $package_name" >&2
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

mkdir -p "$build_root" "$project_root/dist"

if [ -n "$game_file" ]; then
	echo "Building a Linux executable with user-supplied assets embedded..."
else
	echo "Building a Linux standalone launcher without embedded game data..."
fi
if [ -n "$game_file" ]; then
	cmake -S "$project_root" -B "$build_root" \
		-DCMAKE_BUILD_TYPE=Release \
		-DMOPHUN_BUILD_VRALLY2_STANDALONE=ON \
		-DMOPHUN_VRALLY2_ASSET_DIR= \
		-DMOPHUN_VRALLY2_GAME_FILE="$game_file" \
		-DMOPHUN_REQUIRE_FLUIDSYNTH=ON \
		-DBUILD_TESTING=ON
else
	cmake -S "$project_root" -B "$build_root" \
		-DCMAKE_BUILD_TYPE=Release \
		-DMOPHUN_BUILD_VRALLY2_STANDALONE=ON \
		-DMOPHUN_VRALLY2_ASSET_DIR= \
		-DMOPHUN_VRALLY2_GAME_FILE= \
		-DMOPHUN_REQUIRE_FLUIDSYNTH=ON \
		-DBUILD_TESTING=ON
fi
cmake --build "$build_root" --target VRally2Standalone BinaryFormatTests StorageTests --parallel
ctest --test-dir "$build_root" --output-on-failure

if [ -n "$game_file" ]; then
	smoke_saves="$build_root/smoke-saves"
	mkdir -p "$smoke_saves"
	SDL_VIDEODRIVER=dummy MOPHUN_DISABLE_AUDIO=1 MOPHUN_SAVE_DIR="$smoke_saves" \
		"$build_root/V-Rally-2" 2000000
fi

cmake -E copy "$build_root/V-Rally-2" "$project_root/dist/V-Rally-2"
if [ -n "$game_file" ]; then
	echo "Local standalone executable with embedded game data: $project_root/dist/V-Rally-2"
else
	echo "Standalone launcher: $project_root/dist/V-Rally-2"
	echo "Run it with a game directory or MPN file: $project_root/dist/V-Rally-2 /path/to/game"
fi
echo "Set MOPHUN_SOUNDFONT if your distribution does not install a default GM SoundFont."
if [ -n "$game_file" ]; then
	echo "Do not redistribute the executable unless you have rights to every embedded asset."
fi
