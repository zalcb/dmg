<h1 align="center">dmg</h1>

<p align="center">
  A GameBoy (DMG) emulator written in C.
</p>

<p align="center">
  <a href="https://github.com/zalcb/dmg/actions/workflows/core.yml"><img src="https://github.com/zalcb/dmg/actions/workflows/core.yml/badge.svg?branch=main" alt="Core regression tests"></a>
  <a href="core"><img src="https://img.shields.io/badge/written_in-C18-556b2f?style=flat-square" alt="Written in C18"></a>
  <a href="https://www.raylib.com/"><img src="https://img.shields.io/badge/frontend-raylib-445b4b?style=flat-square" alt="Raylib frontend"></a>
  <a href="#building-and-running-locally"><img src="https://img.shields.io/badge/desktop-macOS-303b32?style=flat-square" alt="Desktop: macOS"></a>
</p>

<p align="center">
  <a href="#media">Media</a> ·
  <a href="#features">Features</a> ·
  <a href="#building-and-running-locally">Building and running</a> ·
  <a href="docs/development.md">Developer guide</a> ·
  <a href="https://zalcberg.me">My website</a>
</p>

The goal is to have a fully functional program that can faithfully emulate the Gameboy hardware and its games, especially commercial titles. The emulator is already able to run games such as Kirby, Tetris 2, Zelda and Pokémon Red, as shown below.

Want to read more about the project and my learning experience? Check out my personal website and (WIP) blog: [zalcberg.me](https://zalcberg.me).

## Media

### Games

These recordings show games running in the emulator. Click a preview to watch the video.

<table>
  <tr>
    <td align="center" width="50%">
      <strong>Kirby</strong><br><br>
      <a href="public/showcase/kirby.mp4"><img src="public/showcase/kirby.gif" width="320" alt="Kirby moving through a side-scrolling level in dmg"></a><br>
      <a href="public/showcase/kirby.mp4">Watch gameplay</a>
    </td>
    <td align="center" width="50%">
      <strong>Pokémon Red</strong><br><br>
      <a href="public/showcase/pokemon.mp4"><img src="public/showcase/pokemon.gif" width="320" alt="Exploring a town in Pokémon Red in dmg"></a><br>
      <a href="public/showcase/pokemon.mp4">Watch gameplay</a>
    </td>
  </tr>
  <tr>
    <td align="center" width="50%">
      <strong>Tetris 2</strong><br><br>
      <a href="public/showcase/tetris.mp4"><img src="public/showcase/tetris.gif" width="320" alt="A falling-block puzzle in Tetris 2 in dmg"></a><br>
      <a href="public/showcase/tetris.mp4">Watch gameplay</a>
    </td>
    <td align="center" width="50%">
      <strong>Zelda</strong><br><br>
      <a href="public/showcase/zelda.mp4"><img src="public/showcase/zelda.gif" width="320" alt="Link exploring the village in Zelda in dmg"></a><br>
      <a href="public/showcase/zelda.mp4">Watch gameplay</a>
    </td>
  </tr>
</table>

<p align="center"><sub>The recordings do not include sound, but the emulator supports audio.</sub></p>

## Features

- CPU emulation with the complete SM83 instruction set.
- PPU emulation with background, window and sprite rendering at the original 160 × 144 resolution.
- Four-channel APU with two pulse channels, a wave channel and a noise channel, with 48 kHz stereo output.
- Bank switching for ROM-only, MBC1, MBC2, MBC3 and MBC5 cartridges.
- Keyboard input, display and audio output using [raylib](https://www.raylib.com/).
- A headless mode for testing and debugging on macOS and Linux, without opening a window or audio device.

The project currently focuses on the original GameBoy (DMG). Compatibility and hardware accuracy are still being improved.

## Building and running locally

The desktop emulator is currently supported on macOS, which is my development environment. You will need [Homebrew](https://brew.sh/) and Xcode Command Line Tools installed.

Clone the repository, install the dependencies and build with Makefile:

```bash
git clone https://github.com/zalcb/dmg.git
cd dmg
brew install raylib pkg-config
make
```

This will create a binary file called `gb` in the root directory of the project. You can run the emulator by executing:

```bash
./gb <path_to_rom>
```

Run this command from the repository root so the emulator can find its boot ROM. Commercial game ROMs are not included; you will need to provide your own legally obtained copies.

### Controls

| Game Boy | Keyboard |
| :--- | :--- |
| D-pad | Arrow keys |
| A | <kbd>Z</kbd> |
| B | <kbd>X</kbd> |
| Start | <kbd>Enter</kbd> |
| Select | <kbd>Space</kbd> |

## Tests and development

The emulator is split into CPU, PPU, APU, memory, timer and joypad components. The core is written in C and is independent of raylib, so it can also run without the desktop interface.

The emulator passes Blargg's CPU instruction and timing tests. Automated tests also check the timer, rendering, audio and input behavior on macOS and Linux, including debug and sanitizer builds.

<details>
<summary><strong>Test ROMs</strong></summary>

<p>Screenshots from the CPU instructions and DMG Acid2 tests:</p>

<table>
  <tr>
    <td align="center"><strong>Blargg CPU instructions</strong><br><img src="public/images/cpu_instrs.png" width="300" alt="Blargg CPU instruction test results displayed by dmg"></td>
    <td align="center"><strong>DMG Acid2</strong><br><img src="public/images/dmg_acid2.png" width="300" alt="DMG Acid2 graphics test image displayed by dmg"></td>
  </tr>
</table>

</details>

Recent timer and PPU optimizations improved core performance by **37% in a synthetic benchmark**, with instruction logging disabled in both versions and matching outputs. Results will vary between games. The measurements and instructions to reproduce them are in the [developer guide](docs/development.md).

You can find the implementations in [core/cpu](core/cpu), [core/video](core/video) and [core/audio](core/audio). More information about building, testing and debugging is available in the [developer guide](docs/development.md).

## Roadmap

The next steps are to improve hardware timing, test more games and add quality-of-life features. After that, the plan is to add support for modern joypads such as the DualSense and Xbox controllers. Eventually, GBC support should be added as well.

## Sources

This project was only possible thanks to the following resources:

1. [PanDocs](https://gbdev.io/pandocs/).
2. [Gameboy Memory Map](http://gameboy.mongenel.com/dmg/asmmemmap.html).
3. [gbops](https://izik1.github.io/gbops/), an accurate opcode table.
4. [gbz80(7)](https://rgbds.gbdev.io/docs/v0.9.2/gbz80.7), the SM83 opcode reference.
5. [Complete technical reference](https://gekkio.fi/files/gb-docs/gbctr.pdf).
6. [Blargg's test ROMs](https://github.com/retrio/gb-test-roms) and [DMG Acid2](https://github.com/mattcurrie/dmg-acid2).
