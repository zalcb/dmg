#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mbc.h"
#include "mmu.h"

static void select_rom_bank(MBC *mbc, uint16_t bank) {
    mbc_write_control(mbc, 0x2000, bank & 0xFF);
    mbc_write_control(mbc, 0x3000, bank >> 8);
}

static void check_rom_bank(MMU *mmu, uint16_t bank) {
    assert(mbc_get_current_rom_bank(&mmu->mbc) == bank);
    assert(mbc_read_rom(&mmu->mbc, mmu, 0x4000) == (bank & 0xFF));
    assert(mbc_read_rom(&mmu->mbc, mmu, 0x4001) == (bank >> 8));
    assert(mbc_read_rom(&mmu->mbc, mmu, 0x7FFF) == ((bank & 0xFF) ^ 0xA5));
    assert(mbc_read_rom(&mmu->mbc, mmu, 0x0000) == 0);
    assert(mbc_read_rom(&mmu->mbc, mmu, 0x3FFF) == 0xA5);
}

static void test_mbc5_rom(MMU *mmu) {
    MBC *mbc = &mmu->mbc;
    for (uint8_t type = 0x19; type <= 0x1E; type++) {
        mbc_init(mbc, type, 8, 0);
        assert(mbc->type == MBC5);
        assert(mbc->mbc5_rumble == (type >= 0x1C));
        assert(mbc->rom_size == 0x800000 && mbc->rom_banks == 512);
        mbc_write_control(mbc, 0x4000, 0xFF);
        assert(mbc->ram_bank == (type >= 0x1C ? 7 : 15));
        check_rom_bank(mmu, 1);
        for (uint16_t bank = 0; bank < 512; bank++) {
            select_rom_bank(mbc, bank);
            check_rom_bank(mmu, bank);
        }
        mbc_write_control(mbc, 0x2FFF, 0);
        check_rom_bank(mmu, 256);
        mbc_write_control(mbc, 0x3FFF, 0xFE);
        check_rom_bank(mmu, 0);
        mbc_write_control(mbc, 0x3000, 0xFF);
        check_rom_bank(mmu, 256);
        mbc_write_control(mbc, 0x2000, 0xFF);
        check_rom_bank(mmu, 511);
        mbc_write_control(mbc, 0x6000, 0);
        mbc_write_control(mbc, 0x7FFF, 0xFF);
        mbc_write_control(mbc, 0x8000, 0);
        check_rom_bank(mmu, 511);
        mbc_reset(mbc);
        check_rom_bank(mmu, 1);
        assert(!mbc->ram_enable && mbc->ram_bank == 0);
        assert(mbc->mbc5_rumble == (type >= 0x1C));
    }
}

static void test_mbc5_sizes(MMU *mmu) {
    MBC *mbc = &mmu->mbc;
    for (uint8_t code = 0; code <= 8; code++) {
        mbc_init(mbc, 0x1B, code, 0);
        uint16_t banks = 2u << code;
        assert(mbc->rom_banks == banks);
        assert(mbc->rom_size == (uint32_t)banks * 0x4000);
        select_rom_bank(mbc, 511);
        check_rom_bank(mmu, 511 % banks);
        select_rom_bank(mbc, 256);
        check_rom_bank(mmu, 256 % banks);
    }
    const uint32_t sizes[] = {0, 0x800, 0x2000, 0x8000, 0x20000, 0x10000};
    for (uint8_t code = 0; code < sizeof(sizes) / sizeof(sizes[0]); code++) {
        mbc_init(mbc, 0x1B, 8, code);
        assert(mbc->ram_size == sizes[code]);
        assert(mbc->ram_banks == sizes[code] / 0x2000);
        mbc_write_control(mbc, 0x4000, 0xFF);
        uint8_t bank = mbc->ram_banks ? 15 % mbc->ram_banks : 0;
        assert(mbc_get_current_ram_bank(mbc) == bank);
    }
    mbc_init(mbc, 0x1B, 0xFF, 0xFF);
    assert(mbc->rom_size == 0x8000 && mbc->rom_banks == 2);
    assert(mbc->ram_size == 0 && mbc->ram_banks == 0);
}

static void test_mbc5_ram_gating(MMU *mmu) {
    MBC *mbc = &mmu->mbc;
    mbc_init(mbc, 0x1B, 8, 4);
    memset(mmu->cartridge_ram, 0, mmu->cartridge_ram_size);
    assert(mbc_read_ram(mbc, mmu, 0xA000) == 0xFF);
    mbc_write_ram(mbc, mmu, 0xA000, 0x42);
    assert(mmu->cartridge_ram[0] == 0);
    for (unsigned value = 0; value < 256; value++) {
        mbc_write_control(mbc, value & 1 ? 0 : 0x1FFF, value);
        assert(mbc->ram_enable == ((value & 15) == 10));
        mmu->cartridge_ram[0] = 0x31;
        mbc_write_ram(mbc, mmu, 0xA000, 0x72);
        assert(mmu->cartridge_ram[0] == (mbc->ram_enable ? 0x72 : 0x31));
        assert(mbc_read_ram(mbc, mmu, 0xA000) == (mbc->ram_enable ? 0x72 : 0xFF));
    }
    mbc_write_control(mbc, 0, 0x0A);
    mbc_write_control(mbc, 0x4000, 15);
    mbc_write_ram(mbc, mmu, 0xBFFF, 0x5A);
    mbc_reset(mbc);
    assert(!mbc->ram_enable && mbc->ram_bank == 0);
    mbc_write_control(mbc, 0, 0x0A);
    mbc_write_control(mbc, 0x4000, 15);
    assert(mbc_read_ram(mbc, mmu, 0xBFFF) == 0x5A);
    mbc_init(mbc, 0x19, 8, 0);
    mbc_write_control(mbc, 0, 0x0A);
    assert(mbc_read_ram(mbc, mmu, 0xA000) == 0xFF);
    mbc_write_ram(mbc, mmu, 0xA000, 0x42);
    assert(mmu->cartridge_ram[0] == 0x31);
}

static void test_mbc5_ram_banks(MMU *mmu) {
    MBC *mbc = &mmu->mbc;
    const uint8_t types[] = {0x1A, 0x1B, 0x1D, 0x1E};
    for (unsigned i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        mbc_init(mbc, types[i], 8, 4);
        memset(mmu->cartridge_ram, 0, mmu->cartridge_ram_size);
        mbc_write_control(mbc, 0, 0x0A);
        select_rom_bank(mbc, 511);
        uint8_t banks = mbc->mbc5_rumble ? 8 : 16;
        for (uint8_t bank = 0; bank < banks; bank++) {
            mbc_write_control(mbc, 0x4000, bank);
            assert(mbc_get_current_ram_bank(mbc) == bank);
            mbc_write_ram(mbc, mmu, 0xA000, bank + 0x30);
            mbc_write_ram(mbc, mmu, 0xBFFF, bank + 0x70);
        }
        for (unsigned value = 0; value < 256; value++) {
            mbc_write_control(mbc, 0x5FFF, value);
            uint8_t bank = value & (banks - 1);
            assert(mbc->ram_bank == bank);
            assert(mbc_read_ram(mbc, mmu, 0xA000) == bank + 0x30);
            assert(mbc_read_ram(mbc, mmu, 0xBFFF) == bank + 0x70);
            check_rom_bank(mmu, 511);
        }
        if (mbc->mbc5_rumble) {
            for (uint32_t offset = 0x10000; offset < 0x20000; offset++) {
                assert(mmu->cartridge_ram[offset] == 0);
            }
        }
    }
}

static void test_mbc5_small_ram(MMU *mmu) {
    MBC *mbc = &mmu->mbc;
    const uint8_t codes[] = {1, 2, 3, 5};
    for (unsigned i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        mbc_init(mbc, 0x1B, 8, codes[i]);
        mmu->cartridge_ram_size = mbc->ram_size;
        memset(mmu->cartridge_ram, 0, mmu->cartridge_ram_size);
        mbc_write_control(mbc, 0, 0x0A);
        uint8_t banks = mbc->ram_banks ? mbc->ram_banks : 1;
        for (uint8_t bank = 0; bank < banks; bank++) {
            mbc_write_control(mbc, 0x4000, bank);
            mbc_write_ram(mbc, mmu, 0xA000, 0x30 + bank);
        }
        for (uint8_t bank = 0; bank < 16; bank++) {
            mbc_write_control(mbc, 0x4000, bank);
            assert(mbc_get_current_ram_bank(mbc) == bank % banks);
            assert(mbc_read_ram(mbc, mmu, 0xA000) == 0x30 + bank % banks);
        }
        if (codes[i] == 1) {
            mbc_write_ram(mbc, mmu, 0xA7FF, 0x5A);
            assert(mbc_read_ram(mbc, mmu, 0xA7FF) == 0x5A);
            mbc_write_ram(mbc, mmu, 0xA800, 0);
            assert(mbc_read_ram(mbc, mmu, 0xA800) == 0xFF);
        }
    }
    mmu->cartridge_ram_size = 0x20000;
}

static void test_mbc5_bounds(MMU *mmu) {
    MBC *mbc = &mmu->mbc;
    mbc_init(mbc, 0x1B, 8, 4);
    select_rom_bank(mbc, 511);
    mmu->cartridge_rom_size--;
    assert(mbc_read_rom(mbc, mmu, 0x7FFF) == 0xFF);
    assert(mbc_read_rom(mbc, mmu, 0x4000) == 0xFF);
    assert(mbc_read_rom(mbc, mmu, 0x4001) == 1);
    mmu->cartridge_rom_size = 0x4000;
    assert(mbc_read_rom(mbc, mmu, 0x4001) == 0xFF);
    mmu->cartridge_rom_size = 0x800000;
    mbc_write_control(mbc, 0, 0x0A);
    mbc_write_control(mbc, 0x4000, 15);
    mmu->cartridge_ram_size--;
    mmu->cartridge_ram[0x1FFFF] = 0x5A;
    mbc_write_ram(mbc, mmu, 0xBFFF, 0);
    assert(mmu->cartridge_ram[0x1FFFF] == 0x5A);
    assert(mbc_read_ram(mbc, mmu, 0xBFFF) == 0xFF);
    mmu->cartridge_ram_size = 0x20000;
}

static void test_legacy_rom_banks(MMU *mmu) {
    MBC *mbc = &mmu->mbc;
    const uint8_t types[] = {0, 1, 2, 3, 5, 6, 0x0F, 0x10, 0x11, 0x12, 0x13};
    for (unsigned i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        mbc_init(mbc, types[i], 6, 3);
        check_rom_bank(mmu, 1);
        mbc_write_control(mbc, 0x2100, 0);
        check_rom_bank(mmu, 1);
        mbc_write_control(mbc, 0x2100, 0xFF);
        uint16_t bank = mbc->type == MBC_NONE ? 1 : mbc->type == MBC2 ? 15 :
                        mbc->type == MBC3 || mbc->type == MBC3_RAM_BAT ? 127 : 31;
        check_rom_bank(mmu, bank);
    }
    mbc_init(mbc, 0x03, 6, 3);
    mbc_write_control(mbc, 0x2000, 1);
    mbc_write_control(mbc, 0x4000, 2);
    mbc_write_control(mbc, 0x6000, 1);
    assert(mbc_get_current_rom_bank(mbc) == 65);
    assert(mbc_get_current_ram_bank(mbc) == 2);
    assert(mbc_read_rom(mbc, mmu, 0) == 64);
    assert(mbc_read_rom(mbc, mmu, 0x4000) == 65);
}

static void test_legacy_ram(MMU *mmu) {
    MBC *mbc = &mmu->mbc;
    mbc_init(mbc, 0x06, 3, 0);
    assert(mbc->ram_size == 512 && mbc->ram_enable);
    mbc_write_ram(mbc, mmu, 0xA123, 0xAB);
    assert(mbc_read_ram(mbc, mmu, 0xA323) == 0xFB);
    mbc_init(mbc, 0x13, 6, 3);
    mbc_write_control(mbc, 0, 0x0A);
    for (uint8_t bank = 0; bank < 4; bank++) {
        mbc_write_control(mbc, 0x4000, bank);
        mbc_write_ram(mbc, mmu, 0xA000, 0x40 + bank);
    }
    for (uint8_t bank = 0; bank < 4; bank++) {
        mbc_write_control(mbc, 0x4000, bank);
        assert(mbc_read_ram(mbc, mmu, 0xA000) == 0x40 + bank);
    }
    mbc_write_control(mbc, 0x4000, MBC3_RTC_SECONDS);
    mbc_write_ram(mbc, mmu, 0xA000, 17);
    mbc_write_control(mbc, 0x6000, 0);
    mbc_write_control(mbc, 0x6000, 1);
    mbc_update_rtc(mbc);
    assert(mbc_read_ram(mbc, mmu, 0xA000) == 17 && mbc->rtc.seconds == 18);
}

int main(void) {
    MMU mmu = {0};
    mmu.cartridge_rom_size = 0x800000;
    mmu.cartridge_ram_size = 0x20000;
    mmu.cartridge_rom = calloc(1, mmu.cartridge_rom_size);
    mmu.cartridge_ram = calloc(1, mmu.cartridge_ram_size);
    assert(mmu.cartridge_rom && mmu.cartridge_ram);
    for (uint16_t bank = 0; bank < 512; bank++) {
        uint32_t offset = (uint32_t)bank * 0x4000;
        mmu.cartridge_rom[offset] = bank & 0xFF;
        mmu.cartridge_rom[offset + 1] = bank >> 8;
        mmu.cartridge_rom[offset + 0x3FFF] = (bank & 0xFF) ^ 0xA5;
    }
    test_mbc5_rom(&mmu);
    test_mbc5_sizes(&mmu);
    test_mbc5_ram_gating(&mmu);
    test_mbc5_ram_banks(&mmu);
    test_mbc5_small_ram(&mmu);
    test_mbc5_bounds(&mmu);
    test_legacy_rom_banks(&mmu);
    test_legacy_ram(&mmu);
    free(mmu.cartridge_ram);
    free(mmu.cartridge_rom);
    puts("MBC tests passed: MBC5 variants, 512 ROM banks, RAM banks and legacy controllers.");
    return 0;
}
