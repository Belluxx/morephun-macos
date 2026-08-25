# MoRePhun macOS — V-Rally 2 compatibility

An experimental continuation of [MoRePhun](https://github.com/Luca1991/MoRePhun),
focused on running Mophun software on modern macOS. V-Rally 2 is the primary
compatibility target.

> [!IMPORTANT]
> This repository contains emulator source code only. It does not contain V-Rally 2,
> Mophun games, SDK files, certificates, ROMs, or other proprietary assets. You must
> supply the files .

This is an unofficial preservation and interoperability project. V-Rally, Mophun,
and all associated names and assets belong to their respective owners. The project
is not affiliated with or endorsed by any rights holder.

## Current status

On Apple Silicon macOS, the emulator can load a user-supplied V-Rally 2 MPN, mount
sibling MPC expansion packs, and run through menus and races. Implemented work
includes:

- PIP2 arithmetic, branching, calls, stack, memory, and load/store instructions used
  by the game;
- in-memory commercial-code decoding and Mophun LZ resource decompression;
- palette, sprite, tile-map, clipping, primitives, fonts, road geometry, HUD, and
  persistent-framebuffer rendering;
- Sony Ericsson T610 capabilities and keyboard input;
- MIDI playback through the macOS Core Audio DLS synthesizer;
- automatic sibling MPC mounting and isolated persistent guest storage;
- bounded execution plus opt-in syscall, audio, file, and screenshot diagnostics.

The implementation remains experimental. Timing, rendering, and compatibility
outside the tested title still need work.

## Requirements

The primary tested environment is an Apple Silicon Mac running macOS 11 or newer.

- CMake 3.18+
- a C++14 compiler, normally Apple Clang
- SDL2
- pkg-config
- Xcode Command Line Tools

Install the Homebrew dependencies with:

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

The build also provides `MPNInspect`; macOS builds provide `AudioProbe`.

## Optional local standalone build

On Apple Silicon, a local executable can embed user-supplied V-Rally 2 files:

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

The result is written to `dist/V-Rally-2`.

## Development

Useful format and preservation references:

- [MPN Specification by modezair](https://github.com/modezair/MPN-Specification)
- [Mophun preservation project](https://github.com/ptnnx/Mophun)

## License

The emulator source is distributed under the GNU General Public License v3.0 only.
See [LICENSE](LICENSE). Third-party names and user-supplied game data are not covered
by this license.
