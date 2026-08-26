# MoRePhun (macOS + Linux)

TLDR: Follow intructions below to play [V-Rally2](https://www.youtube.com/watch?v=X85Sj3bQvDs&themeRe) on your Mac!

I recently found my old Sony Ericsson T630 and was surprised to see it still fully working. I immediately tried V-Rally 2, as I used to play it all day long as a kid, and was amazed by how well designed it was for the time. I wanted to play it on my Mac.

This is a continuation of the great work by Luca1991: [MoRePhun](https://github.com/Luca1991/MoRePhun).

I pointed GPT-5.6-Sol at the repo, provided the game files, and asked it to get V-Rally 2 running on my Mac.

After some back and forth, Sol was able to get it working. Menus, races, graphics music, controls, expansion packs, and saves all work.

## Try it

This is currently tested on Apple Silicon Macs running macOS 11 or newer.

First install the required tools:

```sh
xcode-select --install
brew install cmake
```

Download the Mophun version of the game data from
[My Abandonware](https://www.myabandonware.com/game/v-rally-2-yy1) and extract these
files. You'll see these files as a result:

```text
VRally2_[RC14EU]_[multiscreen]_M5.mpn
VRally2_multipack.mpc
VRally2_extrapack1.mpc
VRally2_extrapack2.mpc
VRally2_extrapack3.mpc
VRally2_extrapack4.mpc
```

Then run:

```sh
# Compile the launcher
./scripts/build-standalone-macos.sh

# Run the game
./dist/V-Rally-2 /absolute/path/to/the/game/files

# If you want a portable static build with embedded game data
./scripts/build-standalone-macos.sh /absolute/path/to/the/game/files
./dist/V-Rally-2
```

## Linux

Linux should be supported too, but is currently untested. First install the required tools:

```sh
sudo apt update
sudo apt install build-essential cmake pkg-config libsdl2-dev libfluidsynth-dev fluid-soundfont-gm
```

Then use the same game files listed above (from [My Abandonware](https://www.myabandonware.com/game/v-rally-2-yy1)):

```sh
# Compile the launcher
./scripts/build-standalone-linux.sh

# Run the game
./dist/V-Rally-2 /absolute/path/to/the/game/files
```

If your Linux distribution does not install a default MIDI SoundFont, run the game with `MOPHUN_SOUNDFONT=/absolute/path/to/a/general-midi.sf2`.

## Development build

Development builds work the same way on macOS and Linux. After installing the required tools, run:

```sh
cmake -S . -B build/dev -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build/dev --parallel
ctest --test-dir build/dev --output-on-failure

build/dev/MoRePhun '/absolute/path/to/game.mpn'
```

On Linux, add `-DMOPHUN_REQUIRE_FLUIDSYNTH=ON` to the first command if you want configuration to fail instead of building without MIDI support.

## Controls

- Arrow keys or WASD: navigate and steer
- Return or Left Control: confirm
- Space: secondary fire
- Escape: back

## Credits

Original emulator by [Luca1991](https://github.com/Luca1991/MoRePhun).

V-Rally 2 and Mophun belong to their respective owners. This is an unofficial project.

To see the README written by Sol: [README_GPT-5.6-Sol.md](README_GPT-5.6-Sol.md).
