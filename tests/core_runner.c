#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <inttypes.h>
#include <time.h>

#include "core_machine.h"
#include "../core/memory/rom.h"

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t size) {
    const uint8_t *bytes = data;
    for (size_t i = 0; i < size; ++i)
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    return hash;
}

static uint64_t state_hash(CoreMachine *m) {
    uint64_t hash = UINT64_C(14695981039346656037);
    const uint64_t values[] = {
        m->cpu.af, m->cpu.bc, m->cpu.de, m->cpu.hl, m->cpu.sp, m->cpu.pc,
        m->cpu.cycles, m->cpu.ime, m->cpu.ime_delay, m->cpu.ifr, m->cpu.ier,
        m->cpu.halt, m->cpu.halt_bug, m->cpu.dma_flag, m->cpu.last_opcode,
        m->timer.div, m->timer.tima, m->timer.tma, m->timer.tac,
        m->timer.prev_div_bit, m->timer.overflow_phase,
        m->ppu.scanline_cycles, m->ppu.current_scanline, m->ppu.mode,
        m->ppu.window_line_counter, m->ppu.window_was_visible,
        m->apu.cycles, m->apu.frame_sequencer_counter, m->apu.frame_sequencer_step
    };
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        for (unsigned shift = 0; shift < 64; shift += 8) {
            uint8_t byte = (uint8_t)(values[i] >> shift);
            hash = hash_bytes(hash, &byte, 1);
        }
    }
    hash = hash_bytes(hash, m->mmu.vram, sizeof(m->mmu.vram));
    hash = hash_bytes(hash, m->mmu.wram, sizeof(m->mmu.wram));
    hash = hash_bytes(hash, m->mmu.eram, sizeof(m->mmu.eram));
    hash = hash_bytes(hash, m->mmu.oam, sizeof(m->mmu.oam));
    hash = hash_bytes(hash, m->mmu.io, sizeof(m->mmu.io));
    hash = hash_bytes(hash, m->mmu.hram, sizeof(m->mmu.hram));
    if (m->mmu.cartridge_ram)
        hash = hash_bytes(hash, m->mmu.cartridge_ram, m->mmu.cartridge_ram_size);
    return hash;
}

static double seconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

int main(int argc, char **argv) {
    uint64_t limit = 4194304;
    const char *rom = NULL, *boot = NULL, *trace = NULL, *expect = NULL;
    for (int i = 1; i < argc; ++i) {
        if (i + 1 == argc) goto usage;
        const char *arg = argv[++i];
        if (!strcmp(argv[i - 1], "--cycles")) {
            char *end;
            errno = 0;
            limit = strtoull(arg, &end, 10);
            if (errno || *end || !*arg || *arg == '-' || !limit || limit > UINT64_MAX - 1024)
                goto usage;
        } else if (!strcmp(argv[i - 1], "--rom")) rom = arg;
        else if (!strcmp(argv[i - 1], "--boot")) boot = arg;
        else if (!strcmp(argv[i - 1], "--trace")) trace = arg;
        else if (!strcmp(argv[i - 1], "--expect")) expect = arg;
        else goto usage;
    }
    if ((boot || expect) && !rom) goto usage;
    if (trace && !(cpu_log = fopen(trace, "w"))) {
        perror(trace);
        return 1;
    }
    CoreMachine m;
    core_init(&m);
    if (rom) {
        load_rom(&m.mmu, rom);
        if (boot) {
            load_boot_rom(&m.mmu, boot);
            m.cpu.pc = 0;
        }
    } else core_synthetic(&m);

    uint64_t frames = 0, samples = 0, steps = 0;
    uint64_t frame_hash = UINT64_C(14695981039346656037);
    uint64_t audio_hash = UINT64_C(14695981039346656037);
    char serial[4096] = {0};
    size_t serial_size = 0;
    bool matched = false;
    double start = seconds();
    while (m.cpu.cycles < limit) {
        cpu_step(&m.cpu);
        ++steps;
        if (m.ppu.frame_completed) {
            frame_hash = hash_bytes(frame_hash, m.ppu.framebuffer, sizeof(m.ppu.framebuffer));
            m.ppu.frame_completed = 0;
            ++frames;
            if (frames % 60 == 0) mbc_update_rtc(&m.mmu.mbc);
        }
        if (m.apu.buffer_position != m.apu.buffer_read_position) {
            float audio[2];
            apu_get_samples(&m.apu, audio, 1);
            audio_hash = hash_bytes(audio_hash, audio, sizeof(audio));
            ++samples;
        }
        if (rom && (m.mmu.io[2] & 0x81) == 0x81) {
            unsigned char byte = m.mmu.io[1];
            putchar(byte);
            m.mmu.io[2] &= 0x7F;
            m.cpu.ifr |= 0x08;
            if (serial_size == sizeof(serial) - 1) {
                memmove(serial, serial + 1, --serial_size);
            }
            serial[serial_size++] = (char)byte;
            serial[serial_size] = 0;
            if (expect && strstr(serial, expect)) {
                matched = true;
                break;
            }
        }
    }
    if (cpu_log && fflush(cpu_log)) {
        perror("trace flush");
        core_cleanup(&m);
        fclose(cpu_log);
        return 1;
    }
    double elapsed = seconds() - start;
    if (serial_size) putchar('\n');
    printf("cycles=%" PRIu64 " steps=%" PRIu64 " frames=%" PRIu64 " samples=%" PRIu64
           " state=%016" PRIx64 " frame=%016" PRIx64 " audio=%016" PRIx64 "\n",
           m.cpu.cycles, steps, frames, samples, state_hash(&m), frame_hash, audio_hash);
    printf("seconds=%.6f Mcycles/s=%.3f trace=%s\n", elapsed,
           m.cpu.cycles / elapsed / 1e6, trace ? trace : "off");
    core_cleanup(&m);
    if (cpu_log && fclose(cpu_log)) return 1;
    if (expect && !matched) {
        fprintf(stderr, "Expected serial text not seen within cycle budget: %s\n", expect);
        return 2;
    }
    return 0;

usage:
    fprintf(stderr, "usage: %s [--cycles N] [--trace FILE] [--rom FILE [--boot FILE] [--expect TEXT]]\n", argv[0]);
    return 1;
}
