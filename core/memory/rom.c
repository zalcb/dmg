#include "rom.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mmu.h"

static const char *cartridge_type_str(uint8_t t) {
    switch (t) {
        case 0x00: return "ROM ONLY";
        case 0x01: return "MBC1";
        case 0x02: return "MBC1+RAM";
        case 0x03: return "MBC1+RAM+BATTERY";
        case 0x05: return "MBC2";
        case 0x06: return "MBC2+BATTERY";
        case 0x08: return "ROM+RAM";
        case 0x09: return "ROM+RAM+BATTERY";
        case 0x0F: return "MBC3+TIMER+BATTERY";
        case 0x10: return "MBC3+TIMER+RAM+BATTERY";
        case 0x11: return "MBC3";
        case 0x13: return "MBC3+RAM+BATTERY";
        case 0x19: return "MBC5";
        default:   return "UNKNOWN";
    }
}

static const char *rom_size_str(uint8_t c) {
    static const char *sizes[] = {"32KB", "64KB", "128KB", "256KB", "512KB",
                                  "1MB",  "2MB",  "4MB",   "8MB"};
    return (c <= 8) ? sizes[c] : "UNKNOWN";
}

static const char *ram_size_str(uint8_t c) {
    static const char *sizes[] = {"0KB", "2KB", "8KB", "32KB"};
    return (c <= 3) ? sizes[c] : "UNKNOWN";
}

/* cartridge headers for the DMG start at the address 0x0100 and end at 0x014F
- 0x0134 -> 0x013E: title
- 0x0144 -> 0x0145: new licensee code
- 0x0147: cart type
- 0x0148: cart rom size
- 0x0149: cart ram size
 */
void log_header(MMU *mmu) {
    char title[17] = {0};

    for (int i = 0; i < 16; i++) {
        title[i] = mmu_read(mmu, 0x0134 + i);
    }

    uint8_t cart_type = mmu_read(mmu, 0x0147);
    uint8_t rom_size  = mmu_read(mmu, 0x0148);
    uint8_t ram_size  = mmu_read(mmu, 0x0149);

    printf("\n=== header ===\n");
    printf("title       : %.16s\n", title);
    printf("cartridge   : 0x%02X (%s)\n", cart_type, cartridge_type_str(cart_type));
    printf("ROM size    : %s\n", rom_size_str(rom_size));
    printf("RAM size    : %s\n", ram_size_str(ram_size));
    printf("region      : %s\n", mmu_read(mmu, 0x0149) ? "West" : "Japan");
    printf("CGB flag    : 0x%02X\n", mmu_read(mmu, 0x0143));
    printf("SGB flag    : 0x%02X\n", mmu_read(mmu, 0x0146));
    printf("================\n\n");
}

void load_boot_rom(MMU *mmu, const char *boot_rom_path) {
    FILE *file = fopen(boot_rom_path, "rb");
    if (!file) {
        fprintf(stderr, "Failed to open boot ROM: %s\n", boot_rom_path);
        exit(EXIT_FAILURE);
    }

    size_t bytes_read = fread(mmu->boot_rom, 1, sizeof(mmu->boot_rom), file);
    fclose(file);

    if (bytes_read != sizeof(mmu->boot_rom)) {
        fprintf(stderr, "Boot ROM size mismatch: expected %zu bytes, got %zu bytes\n",
                sizeof(mmu->boot_rom), bytes_read);
        exit(EXIT_FAILURE);
    }

    mmu->boot_rom_enabled = true; /* enable boot ROM */
    printf("Boot ROM loaded successfully: %s\n", boot_rom_path);
}

void load_rom(MMU *mmu, const char *filepath) {
    FILE *file = fopen(filepath, "rb");
    assert(file && "ROM not found?");

    /* get file size */
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    printf("Loading ROM: %s (%ld bytes)\n", filepath, file_size);

    /* clean up any existing cartridge data */
    mmu_cleanup(mmu);

    /* read cartridge header info first (load at least 32KB for header) */
    size_t initial_read_size = (file_size < 0x8000) ? file_size : 0x8000;
    size_t read              = fread(mmu->rom, 1, initial_read_size, file);
    assert(read > 0x147 && "ROM too small - missing header");

    /* extract header information */
    uint8_t cart_type     = mmu->rom[0x0147];
    uint8_t rom_size_code = mmu->rom[0x0148];
    uint8_t ram_size_code = mmu->rom[0x0149];

    /* initialize MBC */
    mbc_init(&mmu->mbc, cart_type, rom_size_code, ram_size_code);

    /* allocate and load full ROM */
    mmu->cartridge_rom_size = mmu->mbc.rom_size;
    mmu->cartridge_rom      = malloc(mmu->cartridge_rom_size);
    assert(mmu->cartridge_rom && "Failed to allocate ROM memory");

    /* reset file position and read entire ROM */
    fseek(file, 0, SEEK_SET);
    size_t total_read = fread(mmu->cartridge_rom, 1, file_size, file);

    /* pad with 0xFF if ROM is smaller than expected size */
    if (total_read < mmu->cartridge_rom_size) {
        memset(mmu->cartridge_rom + total_read, 0xFF, mmu->cartridge_rom_size - total_read);
    }

    /* copy first 32KB to legacy rom array for backwards compatibility */
    memcpy(mmu->rom, mmu->cartridge_rom, 0x8000);

    /* allocate external RAM if needed */
    if (mmu->mbc.ram_size > 0) {
        mmu->cartridge_ram_size = mmu->mbc.ram_size;
        mmu->cartridge_ram      = malloc(mmu->cartridge_ram_size);
        assert(mmu->cartridge_ram && "Failed to allocate RAM memory");
        memset(mmu->cartridge_ram, 0x00, mmu->cartridge_ram_size); /* Initialize to 0 */
    }

    fclose(file);

    printf("ROM loaded: %zu bytes (expected %u), MBC Type: %d\n", total_read,
           mmu->cartridge_rom_size, mmu->mbc.type);
    log_header(mmu);
}
