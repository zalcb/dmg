#include "mbc.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mmu.h"

/* helper function to get ROM size in bytes from header code */
static uint32_t get_rom_size_bytes(uint8_t rom_size_code) {
    if (rom_size_code <= 8) {
        return 0x8000 << rom_size_code;  // 32KB * 2^code
    }
    return 0x8000;  // default to 32KB for unknown codes
}

/* helper function to get RAM size in bytes from header code */
static uint32_t get_ram_size_bytes(uint8_t ram_size_code) {
    switch (ram_size_code) {
        case 0:  return 0;       // no RAM
        case 1:  return 0x800;   // 2KB
        case 2:  return 0x2000;  // 8KB
        case 3:  return 0x8000;  // 32KB
        default: return 0;
    }
}

/* helper function to convert MBC type byte to enum */
static mbc_type_t cartridge_type_to_mbc_type(uint8_t cart_type) {
    switch (cart_type) {
        case 0x00: return MBC_NONE;
        case 0x01: return MBC1;
        case 0x02: return MBC1_RAM;
        case 0x03: return MBC1_RAM_BAT;
        case 0x05: return MBC2;
        case 0x06: return MBC2;
        case 0x0F: return MBC3;
        case 0x10: return MBC3_RAM_BAT;
        case 0x11: return MBC3;
        case 0x12: return MBC3;
        case 0x13: return MBC3_RAM_BAT;
        case 0x19: return MBC5;
        default:   return MBC_NONE;
    }
}

void mbc_init(MBC *mbc, uint8_t cartridge_type, uint8_t rom_size_code, uint8_t ram_size_code) {
    memset(mbc, 0, sizeof(MBC));

    mbc->type     = cartridge_type_to_mbc_type(cartridge_type);
    mbc->rom_size = get_rom_size_bytes(rom_size_code);

    if (mbc->type == MBC2) {
        mbc->ram_size = 512;  // MBC2 has 512 bytes of RAM
    } else {
        mbc->ram_size = get_ram_size_bytes(ram_size_code);
    }

    mbc->rom_banks = mbc->rom_size / 0x4000;                              // 16KB per ROM bank
    mbc->ram_banks = (mbc->ram_size > 0) ? (mbc->ram_size / 0x2000) : 0;  // 8KB per RAM bank

    mbc_reset(mbc);

    printf("MBC initialized: type=%d, ROM=%dKB (%d banks), RAM=%s\n", mbc->type,
           mbc->rom_size / 1024, mbc->rom_banks,
           mbc->type == MBC2 ? "512x4bit"
           : mbc->ram_size > 0
               ? mbc->ram_size >= 1024
                     ? (char[32]){0} + sprintf((char[32]){0}, "%dKB", mbc->ram_size / 1024)
                     : (char[32]){0} + sprintf((char[32]){0}, "%dB", mbc->ram_size)
               : "None");
}

void mbc_reset(MBC *mbc) {
    mbc->rom_bank_low      = 1;  // ROM bank 1 is selected by default (bank 0 maps to bank 1)
    mbc->rom_bank_high     = 0;
    mbc->ram_enable        = (mbc->type == MBC2);
    mbc->ram_bank          = 0;
    mbc->mbc1_mode         = MBC1_MODE_16_8;  // default mode

    /* MBC3 specific */
    mbc->mbc3_mode         = 0;
    mbc->rtc_latch_pending = false;
    memset(&mbc->rtc, 0, sizeof(mbc3_rtc_t));
}

uint8_t mbc_get_current_rom_bank(MBC *mbc) {
    switch (mbc->type) {
        case MBC1:
        case MBC1_RAM:
        case MBC1_RAM_BAT: {
            uint8_t bank = mbc->rom_bank_low & 0x1F;  // 5 bits
            if (bank == 0)
                bank = 1;  // bank 0 maps to bank 1

            // in mode 1, upper bits affect bank selection for 0x4000-0x7FFF
            if (mbc->mbc1_mode == MBC1_MODE_4_32) {
                bank |= (mbc->rom_bank_high & 0x03) << 5;  // add upper 2 bits
            }

            return bank % mbc->rom_banks;  // ensure we don't exceed available banks
        }

        case MBC2: {
            // MBC2 has only one bank, and the lower 4 bits of the value are used
            uint8_t bank = mbc->rom_bank_low & 0x0F;  // 4 bits for MBC2
            if (bank == 0) {
                bank = 1;  // MBC2 bank 0 maps to bank 1
            }
            return bank % mbc->rom_banks;  // ensure we don't exceed available banks
        }

        case MBC3:
        case MBC3_RAM_BAT: {
            uint8_t bank = mbc->rom_bank_low & 0x7F;  // 7 bits for MBC3
            if (bank == 0) {
                bank = 1;  // MBC3 bank 0 maps to bank 1
            }
            return bank % mbc->rom_banks;  // ensure we don't exceed available banks
        }

        case MBC_NONE:
        default:       return 1;  // no banking, always bank 1 for 0x4000-0x7FFF
    }
}

uint8_t mbc_get_current_ram_bank(MBC *mbc) {
    switch (mbc->type) {
        case MBC1_RAM:
        case MBC1_RAM_BAT:
            if (mbc->mbc1_mode == MBC1_MODE_4_32 && mbc->ram_banks > 1) {
                return (mbc->rom_bank_high & 0x03) % mbc->ram_banks;
            }
            return 0;  // in mode 0, always RAM bank 0

        case MBC3:
        case MBC3_RAM_BAT:
            return (mbc->mbc3_mode <= 0x03) ? (mbc->mbc3_mode & 0x03)
                                            : 0;  // MBC3 uses lower 2 bits for RAM bank selection

        case MBC_NONE:
        case MBC1:
        default:       return 0;
    }
}

uint8_t mbc_read_rom(MBC *mbc, struct MMU *mmu, uint16_t addr) {
    uint32_t physical_addr;

    if (addr < 0x4000) {
        // bank 0 area (0x0000-0x3FFF)
        if (mbc->type == MBC1 || mbc->type == MBC1_RAM || mbc->type == MBC1_RAM_BAT) {
            if (mbc->mbc1_mode == MBC1_MODE_4_32) {
                // in mode 1, this area can be banked with upper bits
                uint8_t bank  = (mbc->rom_bank_high & 0x03) << 5;
                physical_addr = (bank * 0x4000) + addr;
            } else {
                // Mode 0: always bank 0
                physical_addr = addr;
            }
        } else {
            // no MBC: direct access
            physical_addr = addr;
        }
    } else {
        // switchable bank area (0x4000-0x7FFF)
        uint8_t bank  = mbc_get_current_rom_bank(mbc);
        physical_addr = (bank * 0x4000) + (addr - 0x4000);
    }

    // bounds check
    if (physical_addr >= mmu->cartridge_rom_size) {
        return 0xFF;  // return 0xFF for out-of-bounds reads
    }

    return mmu->cartridge_rom[physical_addr];
}

uint8_t mbc_read_ram(MBC *mbc, struct MMU *mmu, uint16_t addr) {
    if (!mbc->ram_enable || mbc->ram_size == 0) {
        return 0xFF;  // RAM disabled or not present
    }

    if (mbc->type == MBC2) {
        // MBC2 has only 512 bytes of RAM, accessed directly
        // direct mapping for MBC2
        uint16_t offset = (addr - 0xA000) & 0x01FF;  // 512 bytes, 9 bits
        if (offset < 512 && offset < mmu->cartridge_ram_size) {
            return 0xF0 | (mmu->cartridge_ram[offset] & 0x0F);  // MBC2 uses lower 4 bits
        }
        return 0xFF;  // out of bounds for MBC2
    }

    if ((mbc->type == MBC3 || mbc->type == MBC3_RAM_BAT) && mbc->mbc3_mode >= 0x08 &&
        mbc->mbc3_mode <= 0x0C) {
        switch (mbc->mbc3_mode) {
            case MBC3_RTC_SECONDS:
                return mbc->rtc.latch ? mbc->rtc.latch_seconds : mbc->rtc.seconds;
            case MBC3_RTC_MINUTES:
                return mbc->rtc.latch ? mbc->rtc.latch_minutes : mbc->rtc.minutes;
            case MBC3_RTC_HOURS:  return mbc->rtc.latch ? mbc->rtc.latch_hours : mbc->rtc.hours;
            case MBC3_RTC_DAY_LO: return mbc->rtc.latch ? mbc->rtc.latch_day_lo : mbc->rtc.day_lo;
            case MBC3_RTC_DAY_HI: return mbc->rtc.latch ? mbc->rtc.latch_day_hi : mbc->rtc.day_hi;
            default:              return 0xFF;  // invalid RTC register
        }
    }

    uint8_t bank           = mbc_get_current_ram_bank(mbc);
    uint32_t physical_addr = (bank * 0x2000) + (addr - 0xA000);

    if (physical_addr >= mmu->cartridge_ram_size) {
        return 0xFF;  // out of bounds
    }

    return mmu->cartridge_ram[physical_addr];
}

void mbc_write_control(MBC *mbc, uint16_t addr, uint8_t value) {
    switch (mbc->type) {
        case MBC1:
        case MBC1_RAM:
        case MBC1_RAM_BAT:
            if (addr < 0x2000) {
                // RAM Enable (0x0000-0x1FFF)
                mbc->ram_enable = (value & 0x0F) == 0x0A;
            } else if (addr < 0x4000) {
                // ROM Bank Number (0x2000-0x3FFF)
                mbc->rom_bank_low = value & 0x1F;  // 5 bits
                if (mbc->rom_bank_low == 0) {
                    mbc->rom_bank_low = 1;  // Bank 0 maps to bank 1
                }
            } else if (addr < 0x6000) {
                // RAM Bank Number / Upper ROM Bank bits (0x4000-0x5FFF)
                mbc->rom_bank_high = value & 0x03;  // 2 bits
            } else if (addr < 0x8000) {
                // Banking Mode Select (0x6000-0x7FFF)
                mbc->mbc1_mode = (value & 0x01) ? MBC1_MODE_4_32 : MBC1_MODE_16_8;
            }
            break;

        case MBC2:
            if (addr < 0x4000) {
                // MBC2 uses 8 bits of address to determine operation
                if (addr & 0x0100) {
                    // ROM bank select (bit 8 = 1)
                    mbc->rom_bank_low = value & 0x0F;  // 4 bits for MBC2
                    if (mbc->rom_bank_low == 0) {
                        mbc->rom_bank_low = 1;  // MBC2 bank 0 maps to bank 1
                    }
                }
            }
            break;

        case MBC3:
        case MBC3_RAM_BAT:
            if (addr < 0x2000) {
                // RAM Enable (0x0000-0x1FFF)
                mbc->ram_enable = (value & 0x0F);
            } else if (addr < 0x4000) {
                // ROM Bank Number (0x2000-0x3FFF)
                mbc->rom_bank_low = value & 0x7F;  // 7 bits
                if (mbc->rom_bank_low == 0) {
                    mbc->rom_bank_low = 1;  // Bank 0 maps to bank 1
                }
            } else if (addr < 0x6000) {
                // RAM Bank Number / RTC Register Select (0x4000-0x5FFF)
                mbc->mbc3_mode = value;  // 8 bits
            } else if (addr < 0x8000) {
                // RTC latch control (0x6000-0x7FFF)
                if ((value == 0x01) && mbc->rtc_latch_pending) {
                    mbc->rtc.latch_seconds = mbc->rtc.seconds;
                    mbc->rtc.latch_minutes = mbc->rtc.minutes;
                    mbc->rtc.latch_hours   = mbc->rtc.hours;
                    mbc->rtc.latch_day_lo  = mbc->rtc.day_lo;
                    mbc->rtc.latch_day_hi  = mbc->rtc.day_hi;
                    mbc->rtc.latch         = true;  // set latch flag
                }
                mbc->rtc_latch_pending = (value == 0x00);
            }
            break;

        case MBC_NONE:
        default:
            // No banking, ignore writes
            break;
    }
}

void mbc_write_ram(MBC *mbc, struct MMU *mmu, uint16_t addr, uint8_t value) {
    if (!mbc->ram_enable || mbc->ram_size == 0) {
        return;  // RAM disabled or not present
    }

    if (mbc->type == MBC2) {
        // MBC2 has only 512 bytes of RAM, accessed directly
        uint16_t offset = (addr - 0xA000) & 0x01FF;  // 512 bytes, 9 bits
        if (offset < 512 && offset < mmu->cartridge_ram_size) {
            mmu->cartridge_ram[offset] = value & 0x0F;  // MBC2 uses lower 4 bits
        }
        return;
    }

    if ((mbc->type == MBC3 || mbc->type == MBC3_RAM_BAT) && mbc->mbc3_mode >= 0x08 &&
        mbc->mbc3_mode <= 0x0C) {
        // MBC3 RTC registers
        switch (mbc->mbc3_mode) {
            case MBC3_RTC_SECONDS: mbc->rtc.seconds = value & 0x3F; break;  // 0-59
            case MBC3_RTC_MINUTES: mbc->rtc.minutes = value & 0x3F; break;  // 0-59
            case MBC3_RTC_HOURS:   mbc->rtc.hours = value & 0x1F; break;    // 0-23
            case MBC3_RTC_DAY_LO:  mbc->rtc.day_lo = value; break;  // 0-127 (low byte of day count)
            case MBC3_RTC_DAY_HI:
                mbc->rtc.day_hi = value & 0xC1;
                break;  // only bits 0, 6 and 7 are used
        }
        return;
    }

    uint8_t bank           = mbc_get_current_ram_bank(mbc);
    uint32_t physical_addr = (bank * 0x2000) + (addr - 0xA000);

    if (physical_addr >= mmu->cartridge_ram_size) {
        return;  // out of bounds
    }

    mmu->cartridge_ram[physical_addr] = value;
}

void mbc_update_rtc(MBC *mbc) {
    if ((mbc->type != MBC3 && mbc->type != MBC3_RAM_BAT) || (mbc->rtc.day_hi & 0x40)) {
        return;  // RTC only applicable for MBC3 types, check if day_hi bit 6 is set (halt)
    }

    mbc->rtc.seconds++;
    if (mbc->rtc.seconds >= 60) {
        mbc->rtc.seconds = 0;
        mbc->rtc.minutes++;

        if (mbc->rtc.minutes >= 60) {
            mbc->rtc.minutes = 0;
            mbc->rtc.hours++;

            if (mbc->rtc.hours >= 24) {
                mbc->rtc.hours = 0;

                if (++mbc->rtc.day_lo == 0) {
                    if (mbc->rtc.day_hi & 0x01) {
                        mbc->rtc.day_hi |= 0x80;  // day overflow
                    } else {
                        mbc->rtc.day_hi |= 0x01;  // increment day count
                    }
                }
            }
        }
    }
}

void mbc_print_state(MBC *mbc) {
    printf("\n=== MBC State ===\n");
    printf("Type: %d\n", mbc->type);
    printf("ROM Bank Low: 0x%02X (%d)\n", mbc->rom_bank_low, mbc->rom_bank_low);
    printf("ROM Bank High: 0x%02X (%d)\n", mbc->rom_bank_high, mbc->rom_bank_high);
    printf("Current ROM Bank: %d\n", mbc_get_current_rom_bank(mbc));
    printf("RAM Enabled: %s\n", mbc->ram_enable ? "Yes" : "No");
    printf("Current RAM Bank: %d\n", mbc_get_current_ram_bank(mbc));
    printf("Banking Mode: %s\n",
           (mbc->mbc1_mode == MBC1_MODE_16_8) ? "16Mbit ROM/8KB RAM" : "4Mbit ROM/32KB RAM");
    printf("ROM: %dKB (%d banks)\n", mbc->rom_size / 1024, mbc->rom_banks);
    printf("RAM: %dKB (%d banks)\n", mbc->ram_size / 1024, mbc->ram_banks);
    printf("================\n\n");
}
