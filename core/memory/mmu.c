#include "mmu.h"
#include "cgb.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../cpu/cpu.h"
#include "../audio/apu.h"
#include "../video/ppu.h"
#include "../timer/timer.h"
#include "../input/joyp.h"

void mmu_init(MMU *mmu, struct CPU *cpu, struct Timer *timer, struct PPU *ppu,
              struct Joypad *joypad, struct APU *apu) {
    mmu->cpu                = cpu;
    mmu->timer              = timer;
    mmu->ppu                = ppu;
    mmu->joypad             = joypad;
    mmu->apu                = apu;
    mmu->cgb_mode           = false;

    /* initialize cartridge pointers to NULL */
    mmu->cartridge_rom      = NULL;
    mmu->cartridge_ram      = NULL;
    mmu->cartridge_rom_size = 0;
    mmu->cartridge_ram_size = 0;

    mmu_reset(mmu);
}

void mmu_reset(MMU *mmu) {
    /* clear legacy ROM and ERAM areas */
    memset(mmu->rom, 0, sizeof(mmu->rom));
    memset(mmu->eram, 0, sizeof(mmu->eram));

    /* clear other memory areas (VRAM, WRAM, OAM, IO, HRAM) */
    memset(mmu->vram, 0, sizeof(mmu->vram));
    memset(mmu->wram, 0, sizeof(mmu->wram));
    memset(mmu->oam, 0, sizeof(mmu->oam));
    memset(mmu->io, 0, sizeof(mmu->io));
    memset(mmu->hram, 0, sizeof(mmu->hram));
    mmu_set_cgb_mode(mmu, mmu->cgb_mode);

    if (!mmu->boot_rom_enabled) {
        mmu->io[0x00] = 0xCF; /* JOYP - all buttons released */
        mmu->io[0x40] = 0x91; /* LCDC - LCD enabled */
        mmu->io[0x41] = 0x85; /* STAT - Mode 1 (VBlank), coincidence flag on */
        mmu->io[0x47] = 0xFC; /* BGP - background palette */
        mmu->io[0x48] = 0xFF; /* OBP0 - object palette 0 */
        mmu->io[0x49] = 0xFF; /* OBP1 - object palette 1 */
    }

    /* reset MBC state */
    mbc_reset(&mmu->mbc);
}

uint8_t mmu_read(MMU *mmu, uint16_t addr) {
    if (addr < 0x8000) {
        /* check if reading from boot ROM area */
        if (mmu->boot_rom_enabled && addr < 0x0100) {
            return mmu->boot_rom[addr]; /* read from boot ROM */
        }

        /* ROM area - use MBC for bank switching */
        if (mmu->cartridge_rom) {
            return mbc_read_rom(&mmu->mbc, mmu, addr);
        } else {
            /* fallback to legacy ROM for backwards compatibility */
            return mmu->rom[addr];
        }
    } else if (addr < 0xA000) {
        if (cgb_vram_blocked(mmu)) return 0xFF;
        unsigned bank = mmu->cgb_mode ? mmu->io[VBK - 0xFF00] : 0;
        return mmu_vram_read(mmu, bank, addr);
    } else if (addr < 0xC000) {
        /* external RAM area - use MBC for bank switching */
        if (mmu->cartridge_ram) {
            return mbc_read_ram(&mmu->mbc, mmu, addr);
        } else {
            /* fallback to legacy ERAM */
            return mmu->eram[addr - 0xA000];
        }
    } else if (addr < 0xFE00) {
        return *cgb_wram_address(mmu, addr);
    } else if (addr < 0xFEA0) {
        return mmu->oam[addr - 0xFE00]; /* read from OAM */
    } else if (addr < 0xFF00) {
        return 0xFF; /* prohibited area */
    } else if (addr < 0xFF80) {
        uint8_t value;
        if (cgb_read_register(mmu, addr, &value)) return value;
        switch (addr) {
            case JOYP:   return joypad_read(mmu->joypad);      /* JOYP register */
            case DIV:    return mmu->timer->div >> 8;          /* DIV register */
            case TIMA:   return mmu->timer->tima;              /* TIMA register */
            case TMA:    return mmu->timer->tma;               /* TMA register */
            case TAC:    return mmu->timer->tac;               /* TAC register */
            case IF:     return (mmu->cpu->ifr & 0x1F) | 0xE0; /* IFR register */
            case NR10:
            case NR11:
            case NR12:
            case NR13:
            case NR14:
            case NR21:
            case NR22:
            case NR23:
            case NR24:
            case NR30:
            case NR31:
            case NR32:
            case NR33:
            case NR34:
            case NR41:
            case NR42:
            case NR43:
            case NR44:
            case NR50:
            case NR51:
            case NR52:   return apu_read(mmu->apu, addr);   /* read from APU registers */
            case LY:     return mmu->ppu->current_scanline; /* LY register */
            case STAT:   return (mmu->io[0x41] & 0xF8) | (mmu->ppu->mode & 0x03); /* STAT register */
            case 0xFF56: return 0xFF;
            default:
                if (addr >= 0xFF30 && addr <= 0xFF3F) {
                    return apu_read(mmu->apu, addr);  /* wave RAM */
                }
                return mmu->io[addr - 0xFF00]; /* read from other IO registers */
        }
    } else if (addr < IE) {
        return mmu->hram[addr - 0xFF80]; /* read from HRAM */
    } else if (addr == IE) {
        return (mmu->cpu->ier); /* IER register */
    }
    return 0xFF; /* invalid address */
}

uint16_t mmu_read16(MMU *mmu, uint16_t addr) {
    uint8_t low  = mmu_read(mmu, addr);     /* read the low byte */
    uint8_t high = mmu_read(mmu, addr + 1); /* read the high byte */
    return (high << 8) | low;               /* combine the two bytes */
}

static void hdma_block(MMU *mmu) {
    uint8_t *vram = mmu->io[VBK - 0xFF00] & 1 ? mmu->vram_bank1 : mmu->vram;
    for (unsigned i = 0; i < 16; ++i) {
        uint16_t source = mmu->hdma_source + i;
        bool valid = source < 0x8000 || (source >= 0xA000 && source < 0xE000);
        vram[(mmu->hdma_destination + i) & 0x1FFF] = valid ? mmu_read(mmu, source) : 0xFF;
    }
    mmu->hdma_source += 16;
    mmu->hdma_destination = 0x8000 | ((mmu->hdma_destination + 16) & 0x1FFF);
    mmu->hdma_stall_cycles += mmu->double_speed ? 64 : 32;
    mmu->hdma_blocks--;
    mmu->io[HDMA1 - 0xFF00] = mmu->hdma_source >> 8;
    mmu->io[HDMA2 - 0xFF00] = mmu->hdma_source & 0xF0;
    mmu->io[HDMA3 - 0xFF00] = (mmu->hdma_destination >> 8) & 0x1F;
    mmu->io[HDMA4 - 0xFF00] = mmu->hdma_destination & 0xF0;
    mmu->io[HDMA5 - 0xFF00] = (uint8_t)(mmu->hdma_blocks - 1);
    if (!mmu->hdma_blocks) mmu->hdma_active = false;
}

void mmu_hdma_hblank(MMU *mmu) {
    if (mmu->cgb_mode && mmu->hdma_active) hdma_block(mmu);
}

static void hdma_start(MMU *mmu, uint8_t value) {
    if (!mmu->cgb_mode) return;
    if (mmu->hdma_active) {
        if (!(value & 0x80)) {
            mmu->hdma_active = false;
            mmu->io[HDMA5 - 0xFF00] |= 0x80;
        }
        return;
    }
    mmu->hdma_source = (mmu->io[HDMA1 - 0xFF00] << 8) |
                       (mmu->io[HDMA2 - 0xFF00] & 0xF0);
    mmu->hdma_destination = 0x8000 | ((mmu->io[HDMA3 - 0xFF00] & 0x1F) << 8) |
                            (mmu->io[HDMA4 - 0xFF00] & 0xF0);
    mmu->hdma_blocks = (value & 0x7F) + 1;
    mmu->io[HDMA5 - 0xFF00] = value & 0x7F;
    if ((value & 0x80) && (mmu->io[LCDC - 0xFF00] & 0x80)) {
        mmu->hdma_active = true;
        if (mmu->ppu && mmu->ppu->mode == PPU_MODE_HBLANK &&
            mmu->ppu->current_scanline < 144) hdma_block(mmu);
        return;
    }
    while (mmu->hdma_blocks) hdma_block(mmu);
}

void mmu_write(MMU *mmu, uint16_t addr, uint8_t value) {
    if (addr < 0x8000) {
        /* ROM area - handle MBC control writes */
        mbc_write_control(&mmu->mbc, addr, value);
        return;
    } else if (addr < 0xA000) {
        if (cgb_vram_blocked(mmu)) return;
        uint8_t *vram = mmu->cgb_mode && (mmu->io[VBK - 0xFF00] & 1) ?
                        mmu->vram_bank1 : mmu->vram;
        vram[addr - 0x8000] = value;
        return;
    } else if (addr < 0xC000) {
        /* external RAM area - use MBC for bank switching */
        if (mmu->cartridge_ram) {
            mbc_write_ram(&mmu->mbc, mmu, addr, value);
        } else {
            /* fallback to legacy ERAM */
            mmu->eram[addr - 0xA000] = value;
        }
        return;
    } else if (addr < 0xFE00) {
        *cgb_wram_address(mmu, addr) = value;
        return;
    } else if (addr < 0xFEA0) {
        mmu->oam[addr - 0xFE00] = value; /* write to OAM */
        return;
    } else if (addr < 0xFF00) {
        return; /* prohibited area */
    } else if (addr < 0xFF80) {
        if (cgb_write_register(mmu, addr, value)) return;
        switch (addr) {
            case HDMA5: hdma_start(mmu, value); break;
            case LCDC:
                mmu->io[LCDC - 0xFF00] = value;
                if (mmu->cgb_mode && mmu->hdma_active && !(value & 0x80)) {
                    while (mmu->hdma_blocks) hdma_block(mmu);
                }
                break;
            case JOYP: joypad_write(mmu->joypad, value); break;    /* JOYP register */
            case DIV:  timer_write_div(mmu->timer); break;         /* reset the DIV register */
            case TIMA: timer_write_tima(mmu->timer, value); break; /* TIMA register */
            case TMA:  timer_write_tma(mmu->timer, value); break;  /* TMA register */
            case TAC:  timer_write_tac(mmu->timer, value); break;  /* TAC register */
            case NR10:
            case NR11:
            case NR12:
            case NR13:
            case NR14:
            case NR21:
            case NR22:
            case NR23:
            case NR24:
            case NR30:
            case NR31:
            case NR32:
            case NR33:
            case NR34:
            case NR41:
            case NR42:
            case NR43:
            case NR44:
            case NR50:
            case NR51:
            case NR52: apu_write(mmu->apu, addr, value); break;
            case DMA:  ppu_dma_transfer(mmu->ppu, value); break; /* DMA transfer */
            case IF:   mmu->cpu->ifr = value & 0x1F; break;        /* IFR register */
            case BOOT:
                if (value & 0x01) {
                    mmu->boot_rom_enabled = false;
                    printf("Boot ROM disabled\n");
                }
            default:
                if (addr >= 0xFF30 && addr <= 0xFF3F) {
                    apu_write(mmu->apu, addr, value);  /* wave RAM */
                } else {
                    mmu->io[addr - 0xFF00] = value; /* write to other IO registers */
                }
                break;
        }
        return;
    } else if (addr < IE) {
        mmu->hram[addr - 0xFF80] = value; /* write to HRAM */
        return;
    } else if (addr == IE) {
        mmu->cpu->ier = value & 0x1F; /* write to IER register */
    }
}

void mmu_write16(MMU *mmu, uint16_t addr, uint16_t value) {
    mmu_write(mmu, addr, value & 0xFF);     /* write the low byte */
    mmu_write(mmu, addr + 1, (value >> 8)); /* write the high byte */
}

/* helper function to free dynamically allocated cartridge memory */
void mmu_cleanup(MMU *mmu) {
    if (mmu->cartridge_rom) {
        free(mmu->cartridge_rom);
        mmu->cartridge_rom      = NULL;
        mmu->cartridge_rom_size = 0;
    }

    if (mmu->cartridge_ram) {
        free(mmu->cartridge_ram);
        mmu->cartridge_ram      = NULL;
        mmu->cartridge_ram_size = 0;
    }
}
