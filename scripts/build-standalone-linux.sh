#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
	echo "Usage: $0 /absolute/path/to/vrally2-assets" >&2
	exit 2
fi

asset_directory=$(CDPATH= cd -- "$1" && pwd)
script_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_directory/.." && pwd)
build_root="$project_root/build/standalone-linux-release"

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

for asset_name in \
	'VRally2_[RC14EU]_[multiscreen]_M5.mpn' \
	'VRally2_multipack.mpc' \
	'VRally2_extrapack1.mpc' \
	'VRally2_extrapack2.mpc' \
	'VRally2_extrapack3.mpc' \
	'VRally2_extrapack4.mpc'
do
	if [ ! -f "$asset_directory/$asset_name" ]; then
		echo "Missing required local asset: $asset_directory/$asset_name" >&2
		exit 1
	fi
done

mkdir -p "$build_root" "$project_root/dist"

echo "Building a Linux executable from user-supplied assets..."
cmake -S "$project_root" -B "$build_root" \
	-DCMAKE_BUILD_TYPE=Release \
	-DMOPHUN_BUILD_VRALLY2_STANDALONE=ON \
	-DMOPHUN_VRALLY2_ASSET_DIR="$asset_directory" \
	-DMOPHUN_REQUIRE_FLUIDSYNTH=ON \
	-DBUILD_TESTING=ON
cmake --build "$build_root" --target VRally2Standalone BinaryFormatTests StorageTests --parallel
ctest --test-dir "$build_root" --output-on-failure

smoke_saves="$build_root/smoke-saves"
mkdir -p "$smoke_saves"
SDL_VIDEODRIVER=dummy MOPHUN_DISABLE_AUDIO=1 MOPHUN_SAVE_DIR="$smoke_saves" \
	"$build_root/V-Rally-2" 2000000

cmake -E copy "$build_root/V-Rally-2" "$project_root/dist/V-Rally-2"
echo "Local standalone executable: $project_root/dist/V-Rally-2"
echo "Set MOPHUN_SOUNDFONT if your distribution does not install a default GM SoundFont."
echo "Do not redistribute the executable unless you have rights to every embedded asset."
