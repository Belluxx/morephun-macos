# MoRePhun (macOS)

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
# Compile the game
./scripts/build-standalone-macos.sh /absolute/path/to/the/game/files

# Run the game
./dist/V-Rally-2
```

The result is a single executable with the game data embedded in it.

## Controls

- Arrow keys or WASD: navigate and steer
- Return or Left Control: confirm
- Space: secondary fire
- Escape: back

## Credits

Original emulator by [Luca1991](https://github.com/Luca1991/MoRePhun).

V-Rally 2 and Mophun belong to their respective owners. This is an unofficial project.

To see the README written by Sol: [README_GPT-5.6-Sol.md](README_GPT-5.6-Sol.md).
