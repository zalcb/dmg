#include <assert.h>
#include <stdio.h>

#include "core_machine.h"

static void cgb_init(CoreMachine *m) {
    core_init(m);
    mmu_set_cgb_mode(&m->mmu, true);
    cpu_init(&m->cpu, &m->mmu, &m->timer, &m->ppu, &m->apu);
    m->cpu.pc = 0x100;
    mmu_write(&m->mmu, NR52, 0x80);
}

static void test_mode_and_banks(void) {
    CoreMachine m;
    core_init(&m);
    core_synthetic(&m);
    assert(m.cpu.af == 0x01B0 && m.cpu.bc == 0x0013);
    assert(m.cpu.de == 0x00D8 && m.cpu.hl == 0x014D);
    mmu_write(&m.mmu, 0xC000, 0x42);
    mmu_set_cgb_mode(&m.mmu, true);
    assert(m.cpu.af == 0x01B0 && m.cpu.pc == 0x100);
    assert(mmu_read(&m.mmu, 0xC000) == 0x42);
    cpu_init(&m.cpu, &m.mmu, &m.timer, &m.ppu, &m.apu);
    assert(m.cpu.af == 0x1180 && m.cpu.bc == 0);
    assert(m.cpu.de == 0xFF56 && m.cpu.hl == 0x000D);
    assert(m.cpu.sp == 0xFFFE && m.cpu.pc == 0);
    assert(mmu_read(&m.mmu, VBK) == 0xFE);
    for (unsigned bank = 1; bank < 8; ++bank) {
        mmu_write(&m.mmu, SVBK, bank);
        mmu_write(&m.mmu, 0xD123, 0x30 + bank);
        mmu_write(&m.mmu, 0xFDFF, 0x60 + bank);
    }
    for (unsigned bank = 1; bank < 8; ++bank) {
        mmu_write(&m.mmu, SVBK, bank);
        assert(mmu_read(&m.mmu, SVBK) == (0xF8 | bank));
        assert(mmu_read(&m.mmu, 0xF123) == 0x30 + bank);
        assert(mmu_read(&m.mmu, 0xDDFF) == 0x60 + bank);
        assert(mmu_read(&m.mmu, 0xE000) == 0x42);
    }
    mmu_write(&m.mmu, SVBK, 0);
    assert(mmu_read(&m.mmu, SVBK) == 0xF8);
    assert(mmu_read(&m.mmu, 0xD123) == 0x31 && m.mmu.wram[0x1123] == 0x31);
    mmu_write(&m.mmu, 0x8123, 0x11);
    mmu_write(&m.mmu, VBK, 0xFF);
    mmu_write(&m.mmu, 0x8123, 0x22);
    assert(mmu_read(&m.mmu, VBK) == 0xFF && mmu_read(&m.mmu, 0x8123) == 0x22);
    assert(mmu_vram_read(&m.mmu, 0, 0x8123) == 0x11);
    assert(mmu_vram_read(&m.mmu, 1, 0x8123) == 0x22);
    m.ppu.mode = PPU_MODE_DRAWING;
    mmu_write(&m.mmu, 0x8123, 0x33);
    assert(mmu_read(&m.mmu, 0x8123) == 0xFF);
    assert(mmu_vram_read(&m.mmu, 1, 0x8123) == 0x22);
    mmu_write(&m.mmu, LCDC, 0);
    assert(mmu_read(&m.mmu, 0x8123) == 0x22);
    mmu_reset(&m.mmu);
    assert(m.mmu.cgb_mode && !m.mmu.double_speed);
    assert(mmu_read(&m.mmu, VBK) == 0xFE && mmu_read(&m.mmu, SVBK) == 0xF9);
    assert(m.mmu.vram_bank1[0x123] == 0 && m.mmu.wram_banks[5][0x123] == 0);
    core_cleanup(&m);
}

static void test_palettes_and_registers(void) {
    CoreMachine m;
    cgb_init(&m);
    assert(mmu_read(&m.mmu, KEY1) == 0x7E && mmu_read(&m.mmu, HDMA5) == 0xFF);
    mmu_write(&m.mmu, KEY1, 0xFF);
    assert(mmu_read(&m.mmu, KEY1) == 0x7F && !m.mmu.double_speed);
    mmu_write(&m.mmu, OPRI, 0xFF);
    assert(mmu_read(&m.mmu, OPRI) == 0xFF);
    mmu_write(&m.mmu, OPRI, 0xFE);
    assert(mmu_read(&m.mmu, OPRI) == 0xFE);
    mmu_write(&m.mmu, BGPI, 0xFF);
    mmu_write(&m.mmu, BGPD, 0x12);
    assert(m.mmu.bg_palette[63] == 0x12 && mmu_read(&m.mmu, BGPI) == 0xC0);
    mmu_write(&m.mmu, BGPD, 0x34);
    assert(m.mmu.bg_palette[0] == 0x34 && mmu_read(&m.mmu, BGPI) == 0xC1);
    mmu_write(&m.mmu, OBPI, 0x80);
    mmu_write(&m.mmu, OBPD, 0x56);
    assert(m.mmu.obj_palette[0] == 0x56 && m.mmu.bg_palette[0] == 0x34);
    m.ppu.mode = PPU_MODE_DRAWING;
    mmu_write(&m.mmu, BGPD, 0x77);
    mmu_write(&m.mmu, OBPD, 0x88);
    assert(mmu_read(&m.mmu, BGPD) == 0xFF && mmu_read(&m.mmu, OBPD) == 0xFF);
    assert(m.mmu.bg_palette[1] == 0xFF && m.mmu.obj_palette[1] == 0xFF);
    assert(mmu_read(&m.mmu, BGPI) == 0xC2 && mmu_read(&m.mmu, OBPI) == 0xC2);
    mmu_write(&m.mmu, LCDC, 0);
    mmu_write(&m.mmu, BGPI, 0);
    mmu_write(&m.mmu, OBPI, 0);
    assert(mmu_read(&m.mmu, BGPD) == 0x34 && mmu_read(&m.mmu, OBPD) == 0x56);
    mmu_write(&m.mmu, BGPD, 0x90);
    assert(mmu_read(&m.mmu, BGPI) == 0x40 && mmu_read(&m.mmu, BGPD) == 0x90);
    mmu_set_cgb_mode(&m.mmu, false);
    const uint16_t registers[] = {KEY1, VBK, SVBK, BGPI, BGPD, OBPI, OBPD,
                                  OPRI, HDMA1, HDMA2, HDMA3, HDMA4, HDMA5};
    for (unsigned i = 0; i < sizeof(registers) / sizeof(registers[0]); ++i) {
        mmu_write(&m.mmu, registers[i], 0xFF);
        assert(mmu_read(&m.mmu, registers[i]) == 0xFF);
    }
    assert(!m.mmu.double_speed && !m.mmu.hdma_active && !m.mmu.hdma_stall_cycles);
    assert(m.mmu.io[VBK - 0xFF00] == 0 && m.mmu.io[KEY1 - 0xFF00] == 0);
    core_cleanup(&m);
}

static void dma_address(MMU *mmu, uint16_t source, uint16_t destination) {
    mmu_write(mmu, HDMA1, source >> 8);
    mmu_write(mmu, HDMA2, source & 0xFF);
    mmu_write(mmu, HDMA3, destination >> 8);
    mmu_write(mmu, HDMA4, destination & 0xFF);
}

static void test_gdma(void) {
    CoreMachine m;
    cgb_init(&m);
    for (unsigned i = 0; i < 0x800; ++i) m.mmu.wram[i] = (uint8_t)(i ^ 0xA5);
    mmu_write(&m.mmu, VBK, 1);
    m.ppu.mode = PPU_MODE_DRAWING;
    dma_address(&m.mmu, 0xC00F, 0xFFEF);
    mmu_write(&m.mmu, HDMA5, 2);
    assert(mmu_read(&m.mmu, HDMA5) == 0xFF && !m.mmu.hdma_active);
    assert(m.mmu.hdma_stall_cycles == 96 && m.cpu.cycles == 0);
    assert(m.mmu.hdma_source == 0xC030 && m.mmu.hdma_destination == 0x8010);
    for (unsigned i = 0; i < 48; ++i)
        assert(m.mmu.vram_bank1[(0x1FE0 + i) & 0x1FFF] == (uint8_t)(i ^ 0xA5));
    assert(m.mmu.vram[0x1FE0] == 0);
    cpu_step(&m.cpu);
    assert(m.cpu.pc == 0x100 && m.cpu.cycles == 96 && m.cpu.base_cycles == 96);
    assert(m.timer.div == 96 && m.apu.cycles == 96 && !m.mmu.hdma_stall_cycles);
    mmu_write(&m.mmu, LCDC, 0);
    dma_address(&m.mmu, 0xC000, 0x8000);
    mmu_write(&m.mmu, HDMA5, 0x7F);
    assert(m.mmu.hdma_stall_cycles == 4096);
    tick(&m.cpu, 0);
    assert(m.timer.div == 4192 && m.cpu.base_cycles == 4192);
    assert(memcmp(m.mmu.vram_bank1, m.mmu.wram, 0x800) == 0);
    dma_address(&m.mmu, 0x8000, 0x8000);
    mmu_write(&m.mmu, HDMA5, 0);
    assert(m.mmu.vram_bank1[0] == 0xFF && m.mmu.vram_bank1[15] == 0xFF);
    core_cleanup(&m);
}

static void test_hdma(void) {
    CoreMachine m;
    cgb_init(&m);
    for (unsigned i = 0; i < 64; ++i) m.mmu.wram[i] = 0x80 + i;
    dma_address(&m.mmu, 0xC000, 0x8000);
    mmu_write(&m.mmu, HDMA5, 0x82);
    assert(m.mmu.hdma_active && mmu_read(&m.mmu, HDMA5) == 2);
    assert(m.mmu.vram[0] == 0 && m.mmu.hdma_stall_cycles == 0);
    m.ppu.mode = PPU_MODE_HBLANK;
    mmu_hdma_hblank(&m.mmu);
    assert(m.mmu.vram[0] == 0x80 && m.mmu.vram[15] == 0x8F && m.mmu.vram[16] == 0);
    assert(m.mmu.hdma_stall_cycles == 32 && m.cpu.cycles == 0);
    assert(mmu_read(&m.mmu, HDMA5) == 1);
    mmu_write(&m.mmu, HDMA5, 0xFF);
    assert(m.mmu.hdma_blocks == 2);
    mmu_write(&m.mmu, HDMA5, 0);
    assert(!m.mmu.hdma_active && mmu_read(&m.mmu, HDMA5) == 0x81);
    mmu_hdma_hblank(&m.mmu);
    assert(m.mmu.vram[16] == 0 && m.mmu.hdma_stall_cycles == 32);
    tick(&m.cpu, 0);
    mmu_write(&m.mmu, HDMA5, 0x81);
    assert(m.mmu.hdma_active && m.mmu.hdma_blocks == 1);
    assert(m.mmu.vram[16] == 0x90 && m.mmu.hdma_stall_cycles == 32);
    mmu_hdma_hblank(&m.mmu);
    assert(!m.mmu.hdma_active && mmu_read(&m.mmu, HDMA5) == 0xFF);
    assert(m.mmu.vram[32] == 0xA0 && m.mmu.hdma_stall_cycles == 64);
    tick(&m.cpu, 0);
    mmu_write(&m.mmu, LCDC, 0);
    dma_address(&m.mmu, 0xC000, 0x8100);
    mmu_write(&m.mmu, HDMA5, 0x81);
    assert(!m.mmu.hdma_active && m.mmu.hdma_stall_cycles == 64);
    assert(m.mmu.vram[0x100] == 0x80 && m.mmu.vram[0x11F] == 0x9F);
    tick(&m.cpu, 0);
    mmu_write(&m.mmu, LCDC, 0x80);
    m.ppu.mode = PPU_MODE_OAM_SEARCH;
    dma_address(&m.mmu, 0xC000, 0x8200);
    mmu_write(&m.mmu, HDMA5, 0x82);
    mmu_write(&m.mmu, LCDC, 0);
    assert(!m.mmu.hdma_active && m.mmu.hdma_stall_cycles == 96);
    assert(m.mmu.vram[0x22F] == 0xAF);
    core_cleanup(&m);
}

static void test_speed_switch(void) {
    CoreMachine m;
    cgb_init(&m);
    const uint8_t program[] = {0x10, 0x00, 0x00, 0x10, 0x00, 0x00};
    memcpy(m.mmu.rom + 0x100, program, sizeof(program));
    mmu_write(&m.mmu, KEY1, 1);
    cpu_step(&m.cpu);
    assert(m.mmu.double_speed && m.cpu.pc == 0x102 && !m.cpu.halt);
    assert(mmu_read(&m.mmu, KEY1) == 0xFE && m.timer.div == 0);
    assert(m.cpu.cycles == 4 && m.cpu.base_cycles == 4);
    cpu_step(&m.cpu);
    assert(m.cpu.cycles == 8 && m.cpu.base_cycles == 6);
    assert(m.timer.div == 4 && m.ppu.scanline_cycles == 6 && m.apu.cycles == 6);
    mmu_write(&m.mmu, KEY1, 1);
    cpu_step(&m.cpu);
    assert(!m.mmu.double_speed && m.cpu.pc == 0x105 && !m.cpu.halt);
    assert(mmu_read(&m.mmu, KEY1) == 0x7E && m.timer.div == 0);
    assert(m.cpu.cycles == 12 && m.cpu.base_cycles == 8);
    cpu_step(&m.cpu);
    assert(m.cpu.cycles == 16 && m.cpu.base_cycles == 12 && m.apu.cycles == 12);
    m.mmu.double_speed = true;
    tick(&m.cpu, 1);
    assert(m.cpu.base_cycles == 12);
    tick(&m.cpu, 1);
    assert(m.cpu.base_cycles == 13);
    dma_address(&m.mmu, 0xC000, 0x8000);
    mmu_write(&m.mmu, HDMA5, 1);
    assert(m.mmu.hdma_stall_cycles == 128);
    uint64_t cpu_before = m.cpu.cycles;
    uint64_t base_before = m.cpu.base_cycles;
    uint16_t pc_before = m.cpu.pc;
    m.cpu.ime = 1;
    m.cpu.ier = m.cpu.ifr = 1;
    cpu_step(&m.cpu);
    assert(m.cpu.pc == pc_before && m.cpu.ime == 1);
    assert(m.cpu.cycles == cpu_before + 128 && m.cpu.base_cycles == base_before + 64);
    assert(m.apu.cycles == m.cpu.base_cycles);
    core_cleanup(&m);
}

static void test_hblank_clock_integration(void) {
    CoreMachine m;
    cgb_init(&m);
    mmu_write(&m.mmu, SVBK, 6);
    for (unsigned i = 0; i < 32; ++i) mmu_write(&m.mmu, 0xD000 + i, 0x40 + i);
    dma_address(&m.mmu, 0xD000, 0x8000);
    mmu_write(&m.mmu, HDMA5, 0x81);
    tick(&m.cpu, 252);
    assert(m.cpu.cycles == 284 && m.cpu.base_cycles == 284);
    assert(m.ppu.mode == PPU_MODE_HBLANK && m.ppu.scanline_cycles == 32);
    assert(m.mmu.hdma_active && m.mmu.hdma_blocks == 1);
    assert(m.mmu.vram[0] == 0x40 && m.mmu.vram[15] == 0x4F);
    assert(!m.mmu.hdma_stall_cycles && m.mmu.vram[16] == 0);
    mmu_write(&m.mmu, VBK, 1);
    tick(&m.cpu, 424);
    assert(m.cpu.cycles == 740 && m.cpu.base_cycles == 740);
    assert(m.ppu.current_scanline == 1 && m.ppu.scanline_cycles == 32);
    assert(!m.mmu.hdma_active && mmu_read(&m.mmu, HDMA5) == 0xFF);
    assert(m.mmu.vram_bank1[16] == 0x50 && m.mmu.vram[16] == 0);
    assert(m.timer.div == 740 && m.apu.cycles == 740);
    core_cleanup(&m);
}

int main(void) {
    test_mode_and_banks();
    test_palettes_and_registers();
    test_gdma();
    test_hdma();
    test_speed_switch();
    test_hblank_clock_integration();
    puts("CGB memory/CPU tests passed");
    return 0;
}
