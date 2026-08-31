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
- Space or B: secondary fire
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

## Turbo boost (native MPN mod)

`VRally2TurboMod` patches the supported RC14EU M5 executable with PIP2 guest code.
It uses the built-in `vrally2-rc14eu-m5` target profile: Mophun OS calls are
discovered by import name, while V-Rally internals are typed and signature-checked.
Drive fast and clean to fill the cyan-framed meter at the bottom-right of the race
HUD. When its fill turns white, press **Space** to start the 19.28-second activation
sequence. The race car update is paused during the cinematic; gameplay and an
eight-second boost resume together at the musical handoff. The yellow meter shows
the remaining boost, and the music continues until that meter expires, fading over
its final second. Two layered exhaust flames alternate behind the car during boost,
while a one-pixel diagonal shake is applied through the standard Mophun `vCopyRect`
screen-copy call.

The low-poly 3D cinematic covers the car and cockpit, the pilot turning and
smirking, and the hand opening the safety cover and pressing the button. The
patcher depth-sorts the 3D geometry into a compact 290-frame display list embedded
in the MPN data segment. It is rendered at 15 FPS with `vFillRect`,
`vDrawFlatPolygon`, `vSetForeColor`, and `vFlipScreen`. The repository contains only
the pre-cut, handset-grade mono PCM WAVE excerpt, which the patcher embeds beside
the frames; guest code plays it through `vSoundGetHandle` and `vSoundCtrlEx`. This
handle-based playback domain is independent of `vPlayResource`, so gameplay effects
can sound without stopping the track. No turbo-specific emulator hook, SDL overlay,
external runtime asset, virtual input, or virtual clock is involved.

Build and apply the mod with:

```sh
cmake --build build/dev --target VRally2TurboMod
build/dev/VRally2TurboMod \
  '/path/to/VRally2_[RC14EU]_[multiscreen]_M5.mpn' \
  '/path/to/VRally2_Turbo_[RC14EU]_M5.mpn' \
  assets/turbo_music_clip.wav
```

The recording is stored directly as PCM WAVE and uses the platform's native
sound-handle API; there is no MP3 decoder or MIDI conversion step at runtime.

`build/dev/MPNInspect game.mpn` reports the selected target and its resolved
symbols. Turbo outputs include a lightweight modification marker, allowing both
the inspector and patcher to identify and safely reject reapplication.

The meter charges fastest near maximum road speed. Braking, reversing, driving off
the road, sustained low speed, and sudden impacts reduce it. The patcher verifies
the exact executable header and hook signatures before writing an output, so it
will reject other releases rather than patching unknown code.

## Native V-Rally 2 bazooka mod

`VRally2BazookaMod` patches the same RC14EU M5 executable with a complete
bazooka combat loop for races against rivals. A wooden weapon crate appears on
the road ahead; drive over it to load the launcher with three rockets (top-left
HUD). Press **B** (or Space) to raise the crosshair - the car cruises straight
ahead while the arrow keys steer the sight. Sweep it over a rival and it locks
on: the crosshair snaps to the target, flashes and beeps so you cannot lose it.
Press **B** again to fire. If the mark still holds, the rocket homes in and the
rival explodes with a fireball, screen flash and shake; its burnt wreck is left
burning by the roadside, out of the race for good, as the position counter
confirms.

The rivals play the same game. A crate they drive over arms them ("RIVAL
ARMED!") and they stalk the nearest victim ahead or behind - you included. When
one locks onto you, "INCOMING!" flashes inside a red border with a siren;
swerve hard before impact to dodge the rocket. Take a direct hit and you are
WRECKED: a few burning seconds at a standstill before rejoining the race.

Crates, targeting, rockets, the explosion and fire sprites, the pixel font, and
all six procedural sound effects are generated by the patcher and executed as
PIP2 guest code through standard Mophun OS calls (`vDrawObject`,
`vSetPaletteEntry`, `vFillRect`, `vCopyRect`, `vSoundGetHandle`/`vSoundCtrlEx`).
The crate itself rides the game's own renderer as a phantom rival that exists
only during the render phase, so perspective, scaling and depth sorting are
V-Rally's own. The emulator contains no bazooka-specific runtime behaviour.

```sh
cmake --build build/dev --target VRally2BazookaMod
build/dev/VRally2BazookaMod \
  '/path/to/VRally2_[RC14EU]_[multiscreen]_M5.mpn' \
  '/path/to/VRally2_Bazooka_[RC14EU]_M5.mpn'
```

Run the output like the original game file, and pick ARCADE or CHAMPIONSHIP so
there is someone to shoot at.

## Development

Target descriptions use the public `mophunmod::TargetProfile` API in
`lib/mophunmod/target.h`. Built-in profiles are registered by
`mophunmod::builtInTargets()`; the V-Rally definition in `target_vrally2.cpp`
shows fixed fingerprints, import-discovered pool entries, typed offsets and
constants, and plaintext code signatures. A tool can also construct its own
`TargetCatalog`, so adding another game does not require changing the patching
machinery.

Useful format and preservation references:

- [MPN Specification by modezair](https://github.com/modezair/MPN-Specification)
- [Mophun preservation project](https://github.com/ptnnx/Mophun)

## License

The emulator source is distributed under the GNU General Public License v3.0 only.
See [LICENSE](LICENSE). Third-party names and user-supplied game data are not covered
by this license.
