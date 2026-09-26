#include "runner_output.h"

#include <inttypes.h>
#include <string.h>

#include "core_machine.h"
#include "runner_options.h"

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t size) {
    const uint8_t *bytes = data;
    for (size_t i = 0; i < size; ++i)
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    return hash;
}

static uint64_t hash_values(uint64_t hash, const uint64_t *values, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        for (unsigned shift = 0; shift < 64; shift += 8) {
            uint8_t byte = (uint8_t)(values[i] >> shift);
            hash = hash_bytes(hash, &byte, 1);
        }
    }
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
    hash = hash_values(hash, values, sizeof(values) / sizeof(values[0]));
    hash = hash_bytes(hash, m->mmu.vram, sizeof(m->mmu.vram));
    hash = hash_bytes(hash, m->mmu.wram, sizeof(m->mmu.wram));
    hash = hash_bytes(hash, m->mmu.eram, sizeof(m->mmu.eram));
    hash = hash_bytes(hash, m->mmu.oam, sizeof(m->mmu.oam));
    hash = hash_bytes(hash, m->mmu.io, sizeof(m->mmu.io));
    hash = hash_bytes(hash, m->mmu.hram, sizeof(m->mmu.hram));
    if (m->mmu.cartridge_ram)
        hash = hash_bytes(hash, m->mmu.cartridge_ram, m->mmu.cartridge_ram_size);
    if (m->mmu.cgb_mode) {
        const uint64_t cgb[] = {
            m->cpu.base_cycles, m->mmu.cgb_mode, m->mmu.double_speed, m->mmu.hdma_active,
            m->mmu.hdma_source, m->mmu.hdma_destination,
            m->mmu.hdma_blocks, m->mmu.hdma_stall_cycles
        };
        hash = hash_values(hash, cgb, sizeof(cgb) / sizeof(cgb[0]));
        hash = hash_bytes(hash, m->mmu.vram_bank1, sizeof(m->mmu.vram_bank1));
        hash = hash_bytes(hash, m->mmu.wram_banks, sizeof(m->mmu.wram_banks));
        hash = hash_bytes(hash, m->mmu.bg_palette, sizeof(m->mmu.bg_palette));
        hash = hash_bytes(hash, m->mmu.obj_palette, sizeof(m->mmu.obj_palette));
    }
    return hash;
}

static uint64_t runner_frame_hash(uint64_t hash, const CoreMachine *m) {
    if (!m->mmu.cgb_mode)
        return hash_bytes(hash, m->ppu.framebuffer, sizeof(m->ppu.framebuffer));
    for (unsigned y = 0; y < LCD_HEIGHT; ++y) {
        for (unsigned x = 0; x < LCD_WIDTH; ++x) {
            uint16_t color = m->ppu.color_framebuffer[y][x];
            uint8_t bytes[] = {(uint8_t)color, (uint8_t)(color >> 8)};
            hash = hash_bytes(hash, bytes, sizeof(bytes));
        }
    }
    return hash;
}

bool runner_output_open(RunnerOutput *output, const RunnerOptions *options) {
    memset(output, 0, sizeof(*output));
    output->frame_hash = output->audio_hash = UINT64_C(14695981039346656037);
    if (options->trace && !(cpu_log = fopen(options->trace, "w"))) {
        perror(options->trace);
        return false;
    }
    if (options->video && !(output->video = fopen(options->video, "wb"))) {
        perror(options->video);
        return false;
    }
    return true;
}

static void runner_rgb(RunnerOutput *output, const CoreMachine *m) {
    for (unsigned y = 0; y < LCD_HEIGHT; ++y) {
        for (unsigned x = 0; x < LCD_WIDTH; ++x) {
            for (unsigned channel = 0; channel < 3; ++channel) {
                uint8_t shade = (uint8_t)(255 - m->ppu.framebuffer[y][x] * 85);
                if (m->mmu.cgb_mode) {
                    unsigned component = (m->ppu.color_framebuffer[y][x] >> (channel * 5)) & 31;
                    shade = (uint8_t)((component << 3) | (component >> 2));
                }
                output->rgb[y][x][channel] = shade;
            }
        }
    }
}

bool runner_capture(RunnerOutput *output, CoreMachine *m, const RunnerOptions *options) {
    if (m->ppu.frame_completed) {
        output->frame_hash = runner_frame_hash(output->frame_hash, m);
        m->ppu.frame_completed = 0;
        ++output->frames;
        if (output->frames % 60 == 0) mbc_update_rtc(&m->mmu.mbc);
        if (options->frame || output->video) runner_rgb(output, m);
        if (output->video && fwrite(output->rgb, 1, sizeof(output->rgb), output->video) != sizeof(output->rgb)) {
            perror("video write");
            return false;
        }
    }
    if (m->apu.buffer_position != m->apu.buffer_read_position) {
        float audio[2];
        apu_get_samples(&m->apu, audio, 1);
        output->audio_hash = hash_bytes(output->audio_hash, audio, sizeof(audio));
        ++output->samples;
    }
    return true;
}

void runner_serial(RunnerOutput *output, CoreMachine *m, const RunnerOptions *options) {
    if (!options->rom || (m->mmu.io[2] & 0x81) != 0x81) return;
    unsigned char byte = m->mmu.io[1];
    putchar(byte);
    m->mmu.io[2] &= 0x7F;
    m->cpu.ifr |= 0x08;
    if (output->serial_size == sizeof(output->serial) - 1)
        memmove(output->serial, output->serial + 1, --output->serial_size);
    output->serial[output->serial_size++] = (char)byte;
    output->serial[output->serial_size] = 0;
    output->matched = options->expect && strstr(output->serial, options->expect);
}

bool runner_output_close(RunnerOutput *output) {
    bool ok = true;
    if (output->video && fclose(output->video)) {
        perror("video close");
        ok = false;
    }
    if (cpu_log && fclose(cpu_log)) {
        perror("trace close");
        ok = false;
    }
    cpu_log = NULL;
    return ok;
}

bool runner_save_frame(const RunnerOutput *output, const char *path) {
    if (!path) return true;
    if (!output->frames) {
        fprintf(stderr, "Cannot save frame: no completed frames within cycle budget\n");
        return false;
    }
    FILE *file = fopen(path, "wb");
    if (!file) {
        perror(path);
        return false;
    }
    bool ok = fprintf(file, "P6\n%d %d\n255\n", LCD_WIDTH, LCD_HEIGHT) > 0;
    if (fwrite(output->rgb, 1, sizeof(output->rgb), file) != sizeof(output->rgb)) ok = false;
    if (fclose(file)) ok = false;
    if (!ok) perror(path);
    return ok;
}

void runner_summary(const RunnerOutput *output, CoreMachine *m,
                    const RunnerOptions *options, double elapsed) {
    if (output->serial_size) putchar('\n');
    printf("cycles=%" PRIu64 " steps=%" PRIu64 " frames=%" PRIu64 " samples=%" PRIu64
           " state=%016" PRIx64 " frame=%016" PRIx64 " audio=%016" PRIx64 "\n",
           m->cpu.cycles, output->steps, output->frames, output->samples,
           state_hash(m), output->frame_hash, output->audio_hash);
    printf("seconds=%.6f Mcycles/s=%.3f trace=%s\n", elapsed,
           m->cpu.cycles / elapsed / 1e6, options->trace ? options->trace : "off");
}
