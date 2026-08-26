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

## Turbo boost (emulator-side feature)

The launcher adds a hidden turbo mechanic on top of the unmodified game. Drive fast
and clean to fill the blue meter in the bottom-right corner of the race HUD; when it
reads 100% it pulses, and pressing **UP** (release it first if you were holding it)
starts a slow-motion cinematic of the driver arming and pressing the turbo button.
The camera flies out through the rear window into the normal chase view, the music
drops, control returns and the car is boosted for a few seconds with flames, dust
and an air dome.

* The meter charges above ~70% of full speed and faster above ~90%. Braking,
  reversing, driving off the road, slowing down and sudden impacts drain it.
* Music: `assets/turbo_music.mp3` is used automatically; you can also put a
  `turbo_music.mp3` next to the game files or the launcher, or point
  `MOPHUN_TURBO_MUSIC` at any file. The sequence plays from 1:49 and is cut so the
  drop (2:08.28) lands on the exact frame gameplay resumes. Without a music file the
  cinematic runs on a silent clock.
* Every value is tunable through `MOPHUN_TURBO_<NAME>` environment variables, one per
  field of `MoRePhun/turbo/turbo_config.h` (for example
  `MOPHUN_TURBO_MUSIC_DROP_OFFSET=128.3`, `MOPHUN_TURBO_DURATION=10`,
  `MOPHUN_TURBO_SPEED_MULTIPLIER=2`, `MOPHUN_TURBO_CINEMATIC_TIME_SCALE=0.1`).
  `MOPHUN_TURBO_DISABLED=1` removes the feature entirely.

Development keys while the game runs: `F1` fill the meter, `F2`/`F5` trigger the
sequence, `F3` skip the cinematic to the drop, `F4` toggle the debug overlay
(state, charge, guest speed, shot, offsets), `F6`/`F7` nudge the drop offset by
0.05 s, `F8`/`F9` scale the slow-motion factor, `F10`/`F11` move the music start
by 0.25 s. Headless helpers: `MOPHUN_TURBO_DEV_TRIGGER_FRAME=<frame>` auto-triggers,
`MOPHUN_TURBO_DEV_SHOT_DIRECTORY=<dir>` dumps cinematic frames, and the
`TurboPreview` tool renders the cinematic offline
(`build/dev/TurboPreview <out-dir> [step|t1,t2,...]`).

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
