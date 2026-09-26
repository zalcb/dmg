#ifndef CORE_MACHINE_H
#define CORE_MACHINE_H

#include "cpu.h"
#include "joyp.h"

typedef struct {
    CPU cpu;
    MMU mmu;
    Timer timer;
    PPU ppu;
    APU apu;
    Joypad joypad;
} CoreMachine;

static void core_init(CoreMachine *m) {
    memset(m, 0, sizeof(*m));
    mmu_init(&m->mmu, &m->cpu, &m->timer, &m->ppu, &m->joypad, &m->apu);
    cpu_init(&m->cpu, &m->mmu, &m->timer, &m->ppu, &m->apu);
    timer_init(&m->timer, &m->cpu, &m->mmu);
    ppu_init(&m->ppu, &m->mmu, &m->cpu);
    apu_init(&m->apu, &m->cpu, &m->mmu);
    joypad_init(&m->joypad, &m->mmu, &m->cpu);
    m->cpu.pc = 0x100;
}

static void core_cleanup(CoreMachine *m) {
    free(m->apu.audio_buffer);
    mmu_cleanup(&m->mmu);
}

static void core_synthetic(CoreMachine *m) {
    static const uint8_t program[] = {
        0x21, 0x00, 0xC0, 0x34, 0x7E, 0xCB, 0x07, 0xEE, 0x5A,
        0xE0, 0x43, 0x23, 0x7D, 0xE6, 0x1F, 0x6F, 0x18, 0xF1
    };
    memcpy(m->mmu.rom + 0x100, program, sizeof(program));
    for (unsigned i = 0; i < sizeof(m->mmu.vram); ++i)
        m->mmu.vram[i] = (uint8_t)((i * 37) ^ (i >> 3));
    for (unsigned i = 0; i < 40; ++i) {
        m->mmu.oam[i * 4] = (uint8_t)(16 + (i * 13) % 144);
        m->mmu.oam[i * 4 + 1] = (uint8_t)(8 + (i * 17) % 160);
        m->mmu.oam[i * 4 + 2] = (uint8_t)i;
        m->mmu.oam[i * 4 + 3] = (uint8_t)((i & 7) << 4);
    }
    mmu_write(&m->mmu, LCDC, 0xF3);
    mmu_write(&m->mmu, BGP, 0xE4);
    mmu_write(&m->mmu, OBP0, 0xE4);
    mmu_write(&m->mmu, OBP1, 0xD8);
    mmu_write(&m->mmu, WY, 48);
    mmu_write(&m->mmu, WX, 79);
    mmu_write(&m->mmu, TMA, 0xB7);
    mmu_write(&m->mmu, TAC, 0x05);
    mmu_write(&m->mmu, NR52, 0x80);
    mmu_write(&m->mmu, NR50, 0x77);
    mmu_write(&m->mmu, NR51, 0xFF);
    mmu_write(&m->mmu, NR11, 0x80);
    mmu_write(&m->mmu, NR12, 0xF3);
    mmu_write(&m->mmu, NR13, 0x40);
    mmu_write(&m->mmu, NR14, 0x83);
    mmu_write(&m->mmu, NR21, 0x40);
    mmu_write(&m->mmu, NR22, 0xA2);
    mmu_write(&m->mmu, NR23, 0x80);
    mmu_write(&m->mmu, NR24, 0x84);
    for (unsigned i = 0; i < 16; ++i)
        mmu_write(&m->mmu, (uint16_t)(0xFF30 + i), (uint8_t)(i * 17));
    mmu_write(&m->mmu, NR30, 0x80);
    mmu_write(&m->mmu, NR32, 0x20);
    mmu_write(&m->mmu, NR33, 0x80);
    mmu_write(&m->mmu, NR34, 0x83);
    mmu_write(&m->mmu, NR42, 0x92);
    mmu_write(&m->mmu, NR43, 0x35);
    mmu_write(&m->mmu, NR44, 0x80);
}

#endif
