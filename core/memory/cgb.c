#include "cgb.h"
#include "mmu.h"

#include <string.h>

#include "../video/ppu.h"

void mmu_set_cgb_mode(MMU *mmu, bool enabled) {
    mmu->cgb_mode = enabled;
    mmu->double_speed = false;
    mmu->hdma_active = false;
    mmu->hdma_source = 0;
    mmu->hdma_destination = 0x8000;
    mmu->hdma_blocks = 0;
    mmu->hdma_stall_cycles = 0;
    memset(mmu->vram_bank1, 0, sizeof(mmu->vram_bank1));
    memset(mmu->wram_banks, 0, sizeof(mmu->wram_banks));
    memset(mmu->bg_palette, 0xFF, sizeof(mmu->bg_palette));
    memset(mmu->obj_palette, 0xFF, sizeof(mmu->obj_palette));
    mmu->io[KEY1 - 0xFF00] = 0;
    mmu->io[VBK - 0xFF00] = 0;
    mmu->io[SVBK - 0xFF00] = enabled ? 1 : 0;
    mmu->io[BGPI - 0xFF00] = 0;
    mmu->io[OBPI - 0xFF00] = 0;
    mmu->io[OPRI - 0xFF00] = 0;
    memset(&mmu->io[HDMA1 - 0xFF00], enabled ? 0xFF : 0, 5);
}

bool cgb_vram_blocked(const MMU *mmu) {
    return mmu->cgb_mode && (mmu->io[LCDC - 0xFF00] & 0x80) &&
           mmu->ppu && mmu->ppu->mode == PPU_MODE_DRAWING;
}

uint8_t mmu_vram_read(MMU *mmu, unsigned bank, uint16_t addr) {
    unsigned offset = addr & 0x1FFF;
    return bank & 1 ? mmu->vram_bank1[offset] : mmu->vram[offset];
}

uint8_t *cgb_wram_address(MMU *mmu, uint16_t addr) {
    unsigned offset = addr & 0x1FFF;
    unsigned bank = mmu->io[SVBK - 0xFF00] & 7;
    if (!mmu->cgb_mode || offset < 0x1000 || bank < 2)
        return &mmu->wram[offset];
    return &mmu->wram_banks[bank - 2][offset - 0x1000];
}

bool cgb_read_register(MMU *mmu, uint16_t addr, uint8_t *value) {
    uint8_t raw = mmu->io[addr - 0xFF00];
    switch (addr) {
        case KEY1: *value = 0x7E | (raw & 1) | (mmu->double_speed ? 0x80 : 0); break;
        case VBK: *value = 0xFE | raw; break;
        case SVBK: *value = 0xF8 | raw; break;
        case BGPI:
        case OBPI: *value = 0x40 | raw; break;
        case BGPD: *value = cgb_vram_blocked(mmu) ? 0xFF :
                      mmu->bg_palette[mmu->io[BGPI - 0xFF00] & 0x3F]; break;
        case OBPD: *value = cgb_vram_blocked(mmu) ? 0xFF :
                      mmu->obj_palette[mmu->io[OBPI - 0xFF00] & 0x3F]; break;
        case OPRI: *value = 0xFE | raw; break;
        case HDMA1:
        case HDMA2:
        case HDMA3:
        case HDMA4: *value = 0xFF; break;
        case HDMA5: *value = raw; break;
        default: return false;
    }
    if (!mmu->cgb_mode) *value = 0xFF;
    return true;
}

static void write_palette(MMU *mmu, uint16_t addr, uint8_t value) {
    uint8_t *index = &mmu->io[addr - 0xFF01];
    uint8_t *palette = addr == BGPD ? mmu->bg_palette : mmu->obj_palette;
    if (!cgb_vram_blocked(mmu)) palette[*index & 0x3F] = value;
    if (*index & 0x80) *index = 0x80 | ((*index + 1) & 0x3F);
}

bool cgb_write_register(MMU *mmu, uint16_t addr, uint8_t value) {
    uint8_t mask;
    switch (addr) {
        case KEY1:
        case VBK:
        case OPRI: mask = 1; break;
        case SVBK: mask = 7; break;
        case BGPI:
        case OBPI: mask = 0xBF; break;
        case BGPD:
        case OBPD:
            if (mmu->cgb_mode) write_palette(mmu, addr, value);
            return true;
        case HDMA1: mask = 0xFF; break;
        case HDMA2:
        case HDMA4: mask = 0xF0; break;
        case HDMA3: mask = 0x1F; break;
        default: return false;
    }
    if (mmu->cgb_mode) mmu->io[addr - 0xFF00] = value & mask;
    return true;
}
