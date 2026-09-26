# dmg

A GameBoy (DMG) emulator written in C.

The final goal is to have a fully functional program that can faithfully emulate the Gameboy hardware and its games, specially commercial titles. The emulator is currently stable, and is able to run the most popular games.

Want to read more about the project and my learning experience? Check out my personal website and (WIP) blog: [zalcberg.me](https://zalcberg.me).

---

## Media

### Games

|                         Kirby's Dream Land 2                         |                                Tetris 2                                |                                Zelda                                 |                                 Pokémon Red                                  |
| :------------------------------------------------------------------: | :--------------------------------------------------------------------: | :------------------------------------------------------------------: | :--------------------------------------------------------------------------: |
| <img src="public/videos/kirby.gif" alt="Kirby Demo" width="250px" /> | <img src="public/videos/tetris.gif" alt="Tetris Demo" width="250px" /> | <img src="public/videos/zelda.gif" alt="Zelda Demo" width="250px" /> | <img src="public/videos/pokemon.gif" alt="Pokémon Red Demo" width="250px" /> |

### Test ROMs

|                                  CPU Instructions Test                                  |                                DMG Acid2 Test                                |
| :-------------------------------------------------------------------------------------: | :--------------------------------------------------------------------------: |
| <img src="public/images/cpu_instrs.png" alt="Blargg's cpu_instrs test" width="250px" /> | <img src="public/images/dmg_acid2.png" alt="dmg-acid2 test" width="250px" /> |

## Roadmap

| Stage | Description                                                 | Status |
| ----- | ----------------------------------------------------------- | ------ |
| 0     | Project setup                                               | ✅     |
| 1     | Core emulator w/ CPU emulation and complete instruction set | ✅     |
| 2     | PPU emulation (part 1)                                      | ✅     |
| 3     | Bank Switching                                              | ✅     |
| 4     | PPU emulation (part 2)                                      | ✅     |
| 5     | Joypad support (keyboard)                                   | ✅     |
| 6     | MBC2 and MBC3 support (RTC)                                 | ✅     |
| 7     | APU                                                         | ✅     |
| 8     | QOL and timing improvements                                 | ❌     |

After that, the plan is to add support for modern joypads such as the Dualsense and Xbox controllers. Eventually, GBC support should be added as well.

---

## Building and running locally

The desktop emulator is developed on macOS. Install raylib, pkg-config and Python 3, then build from the repository root:

```bash
brew install raylib pkg-config python
export PATH=/opt/homebrew/bin:$PATH
make
```

This will create a binary file called `gb` in the root directory of the project. You can run the emulator by executing the following command:

```bash
./gb <path_to_rom>
```

Instruction tracing is **off by default**. Enable the existing register/PCMEM trace format explicitly:

```bash
./gb --trace cpu.log <path_to_rom>
```

The trace is buffered and flushed on normal exit, not after every instruction. An interrupted process can lose the buffered tail. Release, debug (`make debug`) and AddressSanitizer/UndefinedBehaviorSanitizer (`make asan`) builds use separate object directories and track header dependencies, so switching modes does not require cleaning.

### Headless regression tests and ROMs

```bash
make -j4 all debug asan headless
make test test-debug test-asan
make test-timer test-ppu
```

The headless runner links the real CPU, MMU, timer, PPU, APU, cartridge and joypad code. It never opens a window or audio device; raylib remains a link dependency for the unused keyboard frontend. `make test` discovers `tests/*_test.c`, including timer and PPU suites when present. CI runs the release, O0 debug and sanitizer suites on macOS. The core suite checks arithmetic/flags, CB memory operations, call/return, interrupt wakeup, MMU mapping, APU register effects and real audio output, plus deterministic full-core execution, trace equivalence and serial/cycle limits.

Without a ROM, the runner executes a deterministic synthetic instruction loop that writes RAM and scroll registers while rendering background/window/sprites, advancing an enabled timer, and generating all four audio channels:

```bash
./build/release/gb-headless --cycles 4194304
make bench
```

It reports actual cycles, instructions/steps, completed frames, stereo sample frames and FNV-1a hashes. `state` hashes CPU and timer registers, PPU timing/window state, APU cycle/sequencer counters and mutable memory; it is an observable-state fingerprint, not a complete save state. `frame` hashes every completed framebuffer in order. `audio` hashes the raw float stereo samples consumed in deterministic emulated time, so compare it using the same floating-point platform/toolchain. Timing includes stepping, hashes and any trace flushing, but excludes initialization. The budget stops between CPU steps, so the final instruction (including DMA) may overshoot it.

External test ROMs are not bundled. Run a trusted ROM with a bounded cycle budget and optionally require serial output:

```bash
./build/release/gb-headless --rom /path/to/instr_timing.gb --cycles 100000000 --expect Passed
./build/release/gb-headless --rom /path/to/cpu_instrs.gb --cycles 1000000000 --expect Passed
./build/asan/gb-headless --rom /path/to/instr_timing.gb --cycles 100000000 --expect Passed
```

The default starts at PC `0100` with the emulator's reset state and no boot ROM. `--boot FILE` loads a 256-byte boot ROM and starts at PC `0000`. Serial testing polls SB/SC after each CPU step, prints bytes for internal-clock transfers (`SC & 0x81 == 0x81`), clears the start bit and requests the serial interrupt. This is a test-console shortcut, not cycle-accurate link emulation. `--expect TEXT` exits successfully as soon as the text appears, or exits with status 2 when the budget expires without it. Without `--expect`, reaching the cycle budget is successful completion, **not** a conformance pass. `--trace FILE` also works headlessly.

### Reproducing the baseline comparison

```bash
python3 tests/bench_compare.py --cycles 41943040 --runs 3
```

This builds the same harness against archived commit `a3f8b97` and the current checkout, checks matching state/frame/audio hashes for every run, and byte-compares a short explicit trace. It reports four separate cases: unchanged baseline with per-instruction flushing to `/dev/null`, a baseline **control** with only the trace call removed in a temporary copy, final buffered tracing to `/dev/null`, and final default tracing off. The control is deliberately not presented as the original baseline. No tracked baseline source is modified. `/dev/null` avoids filesystem throughput noise; real trace files will have different costs.

An Apple Silicon run with Apple clang 21, raylib 6.0, `-O3`, 41,943,040 requested cycles and three repetitions measured these medians for the tracing/harness change alone:

| Mode | Seconds |
| --- | ---: |
| Original baseline, trace + per-instruction flush | 4.385244 |
| Baseline control, trace call removed | 0.253667 |
| Buffered explicit trace | 2.245082 |
| Default, trace off | 0.258982 |

The default throughput improvement was **16.93×**; the core-only ratio against the no-trace control was **0.98×**, so this result demonstrates removal of tracing overhead, not a CPU/timer/PPU algorithm speedup. Every case produced `cycles=41943044 steps=4993219 frames=597 samples=480000 state=daab8f506cd87ea4 frame=540a66195770dc6c audio=24164f96c4bc876d`. Re-run the script to measure subsequent core changes rather than relying on these historical timings.

---

## Sources

This project was only possible thanks to the following resources:

1. PanDocs: [link](https://gbdev.io/pandocs/)
2. Gameboy Memory Map: [link](http://gameboy.mongenel.com/dmg/asmmemmap.html)
3. gbops, an accurate opcode table: [link](https://izik1.github.io/gbops/)
4. gbz80(7) opcode reference: [link](https://rgbds.gbdev.io/docs/v0.9.2/gbz80.7)
5. Complete technical reference: [link](https://gekkio.fi/files/gb-docs/gbctr.pdf)
