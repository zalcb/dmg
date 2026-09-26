#ifndef MBC_HEADER
#define MBC_HEADER

#include <stdbool.h>
#include <stdint.h>

/* MBC types based on cartridge header */
typedef enum {
    MBC_NONE = 0x00,      // ROM ONLY
    MBC1 = 0x01,          // MBC1
    MBC1_RAM = 0x02,      // MBC1+RAM
    MBC1_RAM_BAT = 0x03,  // MBC1+RAM+BATTERY
    MBC2 = 0x05,          // MBC2
    MBC3 = 0x11,          // MBC3
    MBC3_RAM_BAT = 0x13,  // MBC3+RAM+BATTERY
    MBC5 = 0x19,          // MBC5
} mbc_type_t;

/* MBC1 banking modes */
typedef enum {
    MBC1_MODE_16_8 = 0,  // 16Mbit ROM/8KByte RAM mode (default)
    MBC1_MODE_4_32 = 1,  // 4Mbit ROM/32KByte RAM mode
} mbc1_mode_t;

/* MBC3 RTC registers */
typedef enum {
    MBC3_RTC_SECONDS = 0x08,
    MBC3_RTC_MINUTES = 0x09,
    MBC3_RTC_HOURS = 0x0A,
    MBC3_RTC_DAY_LO = 0x0B,
    MBC3_RTC_DAY_HI = 0x0C,
} mbc3_rtc_reg_t;

typedef struct {
    uint8_t seconds;  // 0-59
    uint8_t minutes;  // 0-59
    uint8_t hours;    // 0-23
    uint8_t day_lo;   // 0-127 (low byte of day count)
    uint8_t day_hi;   // 0-255 (high byte of day count)

    bool latch;             // latch flag for RTC
    uint8_t latch_seconds;  // latched seconds value
    uint8_t latch_minutes;  // latched minutes value
    uint8_t latch_hours;    // latched hours value
    uint8_t latch_day_lo;   // latched low byte of day count
    uint8_t latch_day_hi;   // latched high byte of day count
} mbc3_rtc_t;

/* forward declaration */
struct MMU;

/* MBC state structure */
typedef struct MBC {
    mbc_type_t type;

    /* ROM banking */
    uint8_t rom_bank_low;   // ROM bank register (5 bits for MBC1)
    uint8_t rom_bank_high;  // upper ROM bank bits / RAM bank (2 bits for MBC1)

    /* RAM banking */
    bool ram_enable;   // RAM enable flag
    uint8_t ram_bank;  // current RAM bank

    /* MBC1 specific */
    mbc1_mode_t mbc1_mode;  // MBC1 banking mode

    /* MBC3 specific */
    uint8_t
        mbc3_mode;   // MBC3 mode (0x00-0x03 = normal RAM bank, 0x08-0x0C = RTC)
    mbc3_rtc_t rtc;  // RTC state for MBC3
    bool rtc_latch_pending;  // RTC latch pending flag
    uint32_t rtc_cycles;     // cycle counter for RTC updates

    /* ROM/RAM sizes */
    uint32_t rom_size;  // total ROM size in bytes
    uint32_t ram_size;  // total RAM size in bytes
    uint8_t rom_banks;  // number of ROM banks
    uint8_t ram_banks;  // number of RAM banks

} MBC;

/* MBC functions */
void mbc_init(MBC *mbc, uint8_t cartridge_type, uint8_t rom_size_code,
              uint8_t ram_size_code);
void mbc_reset(MBC *mbc);

/* MBC read/write handlers */
uint8_t mbc_read_rom(MBC *mbc, struct MMU *mmu, uint16_t addr);
uint8_t mbc_read_ram(MBC *mbc, struct MMU *mmu, uint16_t addr);
void mbc_write_control(MBC *mbc, uint16_t addr, uint8_t value);
void mbc_write_ram(MBC *mbc, struct MMU *mmu, uint16_t addr, uint8_t value);

/* helper functions */
uint8_t mbc_get_current_rom_bank(MBC *mbc);
uint8_t mbc_get_current_ram_bank(MBC *mbc);
void mbc_update_rtc(MBC *mbc);

/* debug functions */
void mbc_print_state(MBC *mbc);

#endif
