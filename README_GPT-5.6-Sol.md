# MoRePhun — V-Rally 2 compatibility

An experimental continuation of [MoRePhun](https://github.com/Luca1991/MoRePhun),
focused on running Mophun software on modern Linux and macOS. V-Rally 2 is the primary
compatibility target.

> [!IMPORTANT]
> This repository contains emulator source code only. It does not contain V-Rally 2,
> Mophun games, SDK files, certificates, ROMs, or other proprietary assets. You must
> supply the files.

This is an unofficial preservation and interoperability project. V-Rally, Mophun,
and all associated names and assets belong to their respective owners. The project
is not affiliated with or endorsed by any rights holder.

## Current status

On Linux and Apple Silicon macOS, the emulator can load a user-supplied V-Rally 2 MPN, mount
sibling MPC expansion packs, and run through menus and races. Implemented work
includes:

- PIP2 arithmetic, branching, calls, stack, memory, and load/store instructions used
  by the game;
- in-memory commercial-code decoding and Mophun LZ resource decompression;
- palette, sprite, tile-map, clipping, primitives, fonts, road geometry, HUD, and
  persistent-framebuffer rendering;
- Sony Ericsson T610 capabilities and keyboard input;
- MIDI playback through the macOS Core Audio DLS synthesizer or FluidSynth on Linux;
- automatic sibling MPC mounting and isolated persistent guest storage;
- bounded execution plus opt-in syscall, audio, file, and screenshot diagnostics.

The implementation remains experimental. Timing, rendering, and compatibility
outside the tested title still need work.

## Requirements

The supported environments are Linux and Apple Silicon macOS 11 or newer.

- CMake 3.18+
- a C++14 compiler, such as GCC, Clang, or Apple Clang
- SDL2
- pkg-config
- FluidSynth 2.0.5+ and a General MIDI SoundFont on Linux
- Xcode Command Line Tools on macOS

Install the Debian/Ubuntu dependencies with:

```sh
sudo apt install build-essential cmake pkg-config libsdl2-dev \
  libfluidsynth-dev fluid-soundfont-gm
```

Install the Homebrew dependencies on macOS with:

```sh
brew install cmake pkg-config sdl2
```

## Build

```sh
cmake -S . -B build/morephun -DCMAKE_BUILD_TYPE=Release
cmake --build build/morephun --parallel
ctest --test-dir build/morephun --output-on-failure
```

No game assets are needed to configure, compile, or test the emulator.

## Run

Pass the path to an obtained MPN file:

```sh
build/morephun/MoRePhun '/path/to/game.mpn'
```

An optional instruction limit is useful for diagnostics and automated runs:

```sh
build/morephun/MoRePhun '/path/to/game.mpn' 20000000
```

MPC files found beside the MPN are mounted automatically. Guest-created files are
stored under the user's normal application-support directory. Set
`MOPHUN_SAVE_DIR` to an existing directory to override that location.

### Controls

- Arrow keys or WASD: navigate and steer
- Return: confirm / phone `5`
- Left Control: phone fire / `5`
- Space: secondary fire
- Escape: select/back softkey
- Close the window: quit

### Diagnostics

- `MOPHUN_TRACE_SYSCALLS=1`: log guest OS calls
- `MOPHUN_TRACE_AUDIO=1`: log audio requests and results
- `MOPHUN_TRACE_FILES=1`: log guest file resolution
- `MOPHUN_DISABLE_AUDIO=1`: run silently
- `MOPHUN_SCREENSHOT=/path/frame.bmp`: capture the latest presented frame

The build also provides `MPNInspect`; builds with a host MIDI backend provide
`AudioProbe`. Linux additionally supports `MOPHUN_SOUNDFONT=/path/to/file.sf2`
and `MOPHUN_AUDIO_DRIVER=pulseaudio|alsa` overrides.

## Optional standalone build

On Linux, build a launcher that accepts the game directory or MPN file at runtime:

```sh
scripts/build-standalone-linux.sh
dist/V-Rally-2 /absolute/path/to/vrally2-assets
```

On Apple Silicon macOS, use:

```sh
scripts/build-standalone-macos.sh
dist/V-Rally-2 /absolute/path/to/vrally2-assets
```

The runtime argument may be the asset directory or the MPN file itself. To make
a personal executable with the assets embedded, pass that path to the build script:

```sh
scripts/build-standalone-macos.sh /absolute/path/to/vrally2-assets
```

The asset directory must contain:

```text
VRally2_[RC14EU]_[multiscreen]_M5.mpn
VRally2_multipack.mpc
VRally2_extrapack1.mpc
VRally2_extrapack2.mpc
VRally2_extrapack3.mpc
VRally2_extrapack4.mpc
```

The result is written to `dist/V-Rally-2`. Do not redistribute a build containing
embedded assets unless you have the rights to do so.

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

## Development

Useful format and preservation references:

- [MPN Specification by modezair](https://github.com/modezair/MPN-Specification)
- [Mophun preservation project](https://github.com/ptnnx/Mophun)

## License

The emulator source is distributed under the GNU General Public License v3.0 only.
See [LICENSE](LICENSE). Third-party names and user-supplied game data are not covered
by this license.
