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
- Space or B: secondary fire
- Escape: back

## Native V-Rally 2 turbo mod

`VRally2TurboMod` patches the RC14EU M5 executable itself. The charge logic,
HUD, 19.28-second low-poly 3D cinematic, soundtrack, race pause, and boost all
execute from the patched cartridge; the emulator has no turbo-specific runtime
hooks. The patcher selects the `vrally2-rc14eu-m5` target profile, resolves Mophun
OS calls by import name, and validates the release-specific hook and code signature
before modifying anything. The cinematic shows the car and cockpit, a pilot
close-up with an animated smirk, and the hand opening the safety cover and pressing
the turbo button. The
soundtrack continues through the complete eight-second boost and stops with it.
The patcher applies a one-second fade-out at the end of the embedded recording.

```sh
cmake --build build/dev --target VRally2TurboMod
build/dev/VRally2TurboMod \
  '/path/to/VRally2_[RC14EU]_[multiscreen]_M5.mpn' \
  '/path/to/VRally2_Turbo_[RC14EU]_M5.mpn' \
  assets/turbo_music_clip.wav
build/dev/MoRePhun '/path/to/VRally2_Turbo_[RC14EU]_M5.mpn'
```

Run `build/dev/MPNInspect game.mpn` to see the detected target, compatibility
status, and every typed symbol resolved from its profile. Generated Turbo files
carry a small target/mod marker, so inspection and reapplication report that they
were previously modified instead of treating them as an unknown release.

Drive fast and clean to fill the cyan-framed meter. A full white meter is
ready; press Space to play the native activation sequence, then the yellow
meter counts down the active boost. During boost, layered exhaust flames flicker
behind the car while the gameplay framebuffer alternates a one-pixel diagonal
shake. The patcher depth-sorts the 3D geometry into 290 guest drawing frames at
15 FPS and embeds them with the pre-cut, handset-grade PCM recording. At runtime
PIP2 code draws the triangles with `vDrawFlatPolygon` and plays the WAVE through
Mophun's `vSoundGetHandle`/`vSoundCtrlEx` API. Handle-based WAVE playback remains independent of
`vPlayResource`, allowing V-Rally's effects to sound without interrupting the
turbo track. The shake uses Mophun's standard `vCopyRect` screen-copy API; neither
effect relies on a turbo-specific host hook.

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

## Modding targets

Target descriptions use the public `mophunmod::TargetProfile` API in
`lib/mophunmod/target.h`. Built-in profiles are registered by
`mophunmod::builtInTargets()`; `target_vrally2.cpp` demonstrates exact release
fingerprints, import-discovered pool entries, typed offsets and constants, and
plaintext code signatures. Tools may also construct a separate `TargetCatalog`
to support another game without changing the patching machinery.

## Credits

Original emulator by [Luca1991](https://github.com/Luca1991/MoRePhun).

V-Rally 2 and Mophun belong to their respective owners. This is an unofficial project.

To see the README written by Sol: [README_GPT-5.6-Sol.md](README_GPT-5.6-Sol.md).
