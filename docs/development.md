# Developer guide

[Back to the project showcase](../README.md)

Build modes, headless testing, reproducible benchmarks, and the core architecture live here. For gameplay, features, and a quick start, see the [README](../README.md).

## Building and running locally

The desktop emulator is developed on macOS. Install raylib, pkg-config and Python 3, then build from the repository root:

```bash
brew install raylib pkg-config python
export PATH=/opt/homebrew/bin:$PATH
make
```

This creates `gb` (DMG by default) and `gbc` (automatic cartridge-model selection). Both use the same core and can run concurrently:

```bash
./gb <path_to_rom>
./gbc <path_to_color_rom>
```

Both accept `--model dmg|cgb|auto`. Automatic selection recognizes header flags `80` and `C0`; forcing DMG rejects CGB-only cartridges. Native CGB starts at PC `0100` with CGB CPU registers and no boot ROM. DMG retains the bundled boot sequence. Debug outputs are `gb-debug`/`gbc-debug`, and sanitizer outputs are `gb-asan`/`gbc-asan`.

Linux desktop builds require raylib, pkg-config and raylib's platform development libraries. The CGB frontend has also been built and visually exercised on Linux. Sanitizer builds need the compiler runtime (for example, `libclang-rt-18-dev` for Ubuntu's clang 18).

Instruction tracing is **off by default**. Enable the existing register/PCMEM trace format explicitly:

```bash
./gb --trace cpu.log <path_to_rom>
```

The trace is buffered and flushed on normal exit, not after every instruction. An interrupted process can lose the buffered tail. Release, debug (`make debug`) and AddressSanitizer/UndefinedBehaviorSanitizer (`make asan`) builds use separate object directories and track header dependencies, so switching modes does not require cleaning.

## Headless regression tests and ROMs

```bash
make -j4 all debug asan headless
make test test-debug test-asan
make test-timer test-ppu
```

The headless runner links the real CPU, MMU, timer, PPU, APU, cartridge and joypad code. It never opens a window or audio device and does not require raylib or pkg-config. A C18 compiler, make, Python 3 and the system math library are enough for `make headless test test-debug test-asan`. CI runs these suites on macOS and Linux, and separately builds the raylib frontend on macOS.

`make test` discovers integration tests in `tests/*_test.c` and subsystem tests in `core/*/*_test.c`. The suites check arithmetic/flags, CB memory operations, call/return, interrupt wakeup, MMU mapping, APU register masks/wave RAM/DAC triggers and real audio output, all 65,536 joypad state transitions, 16,185,152 timer differential comparisons, and 54,193 full-state PPU comparisons. The integration checks preserve deterministic execution, trace equivalence and serial/cycle limits.

CGB checks additionally cover VRAM/WRAM banks and echoes, palette access and auto-increment, mode-3 access blocking, DMA transfer/cancellation/stalls, KEY1/STOP clocks, color rendering and priority, model selection, RGB capture, and an original animated ROM. MBC5 checks exercise all 512 ROM banks and cartridge variants, RAM banks, rumble masks and legacy controllers. The DMG golden expectations have not changed.

Without a ROM, the runner executes a deterministic synthetic instruction loop that writes RAM and scroll registers while rendering background/window/sprites, advancing an enabled timer, and generating all four audio channels:

```bash
./build/release/gb-headless --cycles 4194304
make bench
```

It reports actual cycles, instructions/steps, completed frames, stereo sample frames and FNV-1a hashes. `state` hashes CPU and timer registers, PPU timing/window state, APU cycle/sequencer counters and mutable memory; it is an observable-state fingerprint, not a complete save state. `frame` hashes every completed framebuffer in order. `audio` hashes the raw float stereo samples consumed in deterministic emulated time. The audio pipeline uses explicit `fmaf` operations to preserve the original Apple Silicon fused rounding instead of letting the compiler choose different rounding on x86. The golden test is unchanged; arbitrary floating-point modes and math libraries are still not promised bit-identical. Timing includes stepping, hashes and any trace flushing, but excludes initialization. The budget stops between CPU steps, so the final instruction (including DMA) may overshoot it.

External test ROMs are not bundled. Run a trusted ROM with a bounded cycle budget and optionally require serial output:

```bash
./build/release/gb-headless --rom /path/to/instr_timing.gb --cycles 100000000 --expect Passed
./build/release/gb-headless --rom /path/to/cpu_instrs.gb --cycles 1000000000 --expect Passed
./build/asan/gb-headless --rom /path/to/instr_timing.gb --cycles 100000000 --expect Passed
```

The default starts at PC `0100` with the emulator's reset state and no boot ROM. `--boot FILE` loads a 256-byte boot ROM and starts at PC `0000`. Serial testing polls SB/SC after each CPU step, prints bytes for internal-clock transfers (`SC & 0x81 == 0x81`), clears the start bit and requests the serial interrupt. This is a test-console shortcut, not cycle-accurate link emulation. `--expect TEXT` exits successfully as soon as the text appears, or exits with status 2 when the budget expires without it. Without `--expect`, reaching the cycle budget is successful completion, **not** a conformance pass. `--trace FILE` also works headlessly.

The runner defaults to `--model auto`; `--boot` is supported only in DMG mode. `--cycles` counts CPU T-cycles, so CGB double speed takes approximately twice the budget for the same video/audio duration. The CPU tracks base-rate clocks separately for the PPU/APU; timer clocks follow the CPU. In CGB mode, state fingerprints also include the extra banks, palettes, speed and DMA state, and frame fingerprints use little-endian RGB555 pixels.

### Reproducing color evidence

Generate the original demo, capture the last completed frame as PPM and every completed frame as raw 160×144 RGB24:

```bash
python3 tests/cgb_demo.py /tmp/chroma.gbc
./gbc /tmp/chroma.gbc
./build/release/gb-headless --rom /tmp/chroma.gbc --cycles 12582912 \
  --frame /tmp/chroma.ppm --video /tmp/chroma.rgb
ffmpeg -f rawvideo -pixel_format rgb24 -video_size 160x144 -framerate 59.7275 \
  -i /tmp/chroma.rgb -vf 'scale=640:576:flags=neighbor' -c:v libx264 \
  -pix_fmt yuv420p /tmp/chroma.mp4
```

The demo is generated from original code/artwork in `tests/cgb_demo.py`; no external assets or assembler are required. CGB Acid2 v1.1 was separately run for 8,388,608 cycles and its 160×144 output compared against [the upstream reference](https://github.com/mattcurrie/cgb-acid2): **zero differing RGB pixels**, using `(component << 3) | (component >> 2)` conversion. Download external test ROMs yourself; they are not bundled or fetched by the test suite.

Native CGB covers banked VRAM/WRAM, RGB555 palettes, BG/window attributes, sprite priority including OPRI, GDMA/HDMA and double speed. It remains a scanline renderer, without exact pixel-FIFO or speed-switch delay timing. CGB boot ROM execution, battery-save persistence, link/infrared and physical rumble are not implemented. Headless audio generation is tested; desktop recordings are silent and do not establish audio-device playback quality.

## Reproducing the baseline comparison

```bash
python3 tests/bench_compare.py --cycles 41943040 --runs 3
```

This macOS comparison builds the same harness against archived commit `a3f8b97` and the current checkout, checks matching state/frame/audio hashes for every run, and byte-compares a short explicit trace. It reports four separate cases: unchanged baseline with per-instruction flushing to `/dev/null`, a baseline **control** with only the trace call removed in a temporary copy, final buffered tracing to `/dev/null`, and final default tracing off. The control is deliberately not presented as the original baseline. No tracked baseline source is modified. `/dev/null` avoids filesystem throughput noise; real trace files will have different costs.

The archived build uses the DMG-only harness from `a85c4c4`, because the current runner additionally references CGB interfaces absent from the old emulator. Both execute the same synthetic DMG workload; equality of every reported fingerprint and the explicit trace remains mandatory.

An Apple Silicon run with Apple clang 21, raylib 6.0 for the archived baseline, `-O3`, 41,943,040 requested cycles and five repetitions measured these medians after the tracing, timer, PPU and module-boundary changes:

| Mode | Seconds |
| --- | ---: |
| Original baseline, trace + per-instruction flush | 4.335197 |
| Baseline control, trace call removed | 0.250427 |
| Buffered explicit trace | 2.128909 |
| Default, trace off | 0.182232 |

The default throughput improvement was **23.79×**, dominated by removing instruction tracing from the hot path. The core-only improvement against the no-trace control was **1.37×** (27.2% less elapsed time), which separates algorithm/build improvements from logging overhead. Every case produced `cycles=41943044 steps=4993219 frames=597 samples=480000 state=daab8f506cd87ea4 frame=540a66195770dc6c audio=24164f96c4bc876d`; short explicit traces also matched byte-for-byte. These are instrumented synthetic-workload measurements, not a promise of the same gain in every game. Re-run the script to measure subsequent changes. The comparison script still needs raylib/pkg-config to compile the original archived emulator; current headless builds do not.

The final sanitizer build also passes Blargg `cpu_instrs` (all 11 subtests, 224,317,604 cycles) and `instr_timing` (2,760,516 cycles), with the original baseline's reported state/frame/audio fingerprints. The timer and rendering optimizations preserve existing behavior; passing these suites does not establish complete Game Boy hardware accuracy.

## Core organization and structural checks

The core is grouped into `audio`, `cpu`, `input`, `memory` (bus, cartridge banking and ROM loading), `timer`, and `video`. Each subsystem keeps its C source, public header and focused tests together. Opcode implementation helpers are private to `core/cpu/opcodes.c`; `opcodes.h` only exposes instruction dispatch. Headers use forward declarations where only pointers are needed, and explicit relative includes make both compiler and static-analysis dependencies unambiguous.

Raylib keyboard polling, windowing and audio-device ownership stay in `src/main.c`. The core accepts active-low button state through `joypad_set_state` and exposes audio cleanup through `apu_cleanup`. No emulation timing or rendering behavior is changed by these boundaries. Generate a local, current compilation database with `make compile-commands`; it is intentionally not versioned because it contains checkout-specific paths.

With Sentrux 0.5.7 installed:

```bash
sentrux check .
sentrux gate .
```

The rules require zero dependency cycles and prohibit core-to-frontend imports. Sentrux 0.5.7 reports **8,034/10,000**, above the improvement front's 8,000 target, versus **6,363** at `a3f8b97`. The original reported cycle was a C-include resolution artifact: a control that changed only header paths scored **7,089** with zero cycles. The remaining improvement comes from narrower APIs, cohesive subsystem boundaries and tests; the score gain must not be presented as removing a real runtime dependency cycle.

The complete CGB implementation scores **8,155/10,000** under the recorded baseline's header classification, versus **8,034** before this work, with zero dependency cycles and zero god files. Sentrux 0.5.7's C and Objective-C plugins both claim `.h`; filesystem-dependent registration order changes the language assigned to headers and the resulting call graph. With all headers correctly classified as C, the same pre-CGB source (`a85c4c4`) scores **7,573**, and the CGB implementation scores **7,776**. These are two distinct, like-for-like improvements, not interchangeable score scales.

CI pins Sentrux 0.5.7 and explicitly assigns header ownership in an isolated plugin directory. It runs architectural checks and the gate for both classifications, enforces the original 8,034 floor under the historical classification, and requires the C-native score to be no lower than the same analyzer's measurement of `a85c4c4`. Only temporary exported copies receive a C-native comparison baseline; the repository's original `.sentrux/baseline.json` is unchanged. No source files are excluded. Reproduce both checks on Linux with `SENTRUX=/path/to/sentrux bash .github/scripts/check-structure.sh` from a checkout with full history and the intended changes staged. New runner helpers use declaration headers and separate implementation files rather than implementation-heavy headers; the core remains independent of the frontend.

The checked-in baseline protects the accepted structural state; do not regenerate it merely to hide a regression. Sentrux measures structure, not emulator throughput or hardware fidelity, so performance changes must also pass the regression suites and benchmark comparison.
