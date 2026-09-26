#ifndef MMU_HEADER
#define MMU_HEADER

#include <stdbool.h>
#include <stdint.h>

#include "mbc.h"

/* I/O registers addresses */
#define JOYP 0xFF00  // joypad input
#define DIV 0xFF04   // DIV register
#define TIMA 0xFF05  // TIMA register
#define TMA 0xFF06   // TMA register
#define TAC 0xFF07   // TAC register
#define IF 0xFF0F    // IF register
#define NR10 0xFF10  // NR10 register (CH1)
#define NR11 0xFF11  // NR11 register (CH1)
#define NR12 0xFF12  // NR12 register (CH1)
#define NR13 0xFF13  // NR13 register (CH1)
#define NR14 0xFF14  // NR14 register (CH1)
#define NR21 0xFF16  // NR21 register (CH2)
#define NR22 0xFF17  // NR22 register (CH2)
#define NR23 0xFF18  // NR23 register (CH2)
#define NR24 0xFF19  // NR24 register (CH2)
#define NR30 0xFF1A  // NR30 register (CH3)
#define NR31 0xFF1B  // NR31 register (CH3)
#define NR32 0xFF1C  // NR32 register (CH3)
#define NR33 0xFF1D  // NR33 register (CH3)
#define NR34 0xFF1E  // NR34 register (CH3)
#define NR41 0xFF20  // NR41 register (CH4)
#define NR42 0xFF21  // NR42 register (CH4)
#define NR43 0xFF22  // NR43 register (CH4)
#define NR44 0xFF23  // NR44 register (CH4)
#define NR50 0xFF24  // NR50 (sound volume control)
#define NR51 0xFF25  // NR51 (sound panning)
#define NR52 0xFF26  // NR52 (sound on/off)
#define LCDC 0xFF40  // LCD control
#define STAT 0xFF41  // LCD status
#define SCY 0xFF42   // scroll Y
#define SCX 0xFF43   // scroll X
#define LY 0xFF44    // current scanline
#define LYC 0xFF45   // LY compare
#define DMA 0xFF46   // DMA transfer register
#define BGP 0xFF47   // background palette
#define BOOT 0xFF50  // boot ROM enable (write 1 to disable boot ROM)
#define IE 0xFFFF    // IE register

struct CPU;
struct Timer;
struct PPU;
struct Joypad;
struct APU;

typedef struct MMU {
    struct CPU *cpu;        // pointer to the CPU
    struct Timer *timer;    // pointer to the timer
    struct PPU *ppu;        // pointer to the PPU
    struct Joypad *joypad;  // pointer to the joypad
    struct APU *apu;        // pointer to the APU

    /* cartridge data */
    uint8_t *cartridge_rom;       // dynamically allocated ROM data
    uint32_t cartridge_rom_size;  // actual ROM size in bytes
    uint8_t *cartridge_ram;       // dynamically allocated external RAM
    uint32_t cartridge_ram_size;  // actual RAM size in bytes
    MBC mbc;                      // memory bank controller

    /* boot rom */
    uint8_t boot_rom[0x0100];  // 0000h - 00FFh (boot ROM, 256 bytes)
    bool boot_rom_enabled;     // boot ROM enabled (true if boot ROM is used)

    /* legacy ROM for compatibility (now points to cartridge_rom) */
    uint8_t
        rom[0x8000];   // 0000h - 7FFFh (first 32KB for backwards compatibility)
    uint8_t rom_bank;  // current rom bank (for MBC1 and MBC2)
    bool ram_enable;   // ram enabled (for MBC1 and MBC2)

    /* video ram */
    uint8_t vram[0x2000];  // 8000h - 9FFFh

    /* external (cartdrige) ram */
    uint8_t eram[0x2000];  // A000h - BFFFh

    /* work ram */
    uint8_t wram[0x2000];  // C000h - DFFFh

    // echo ram is prohibited (according to nintendo)

    /* oam (object attribute memory) */
    uint8_t oam[0x00A0];  // FE00h - FE9Fh

    /* i/o registers */
    uint8_t io[0x0080];  // FF00h - FF7Fh

    /* high ram */
    uint8_t hram[0x007F];  // FF80h - FFFFh

} MMU;

// initialize and reset the MMU
void mmu_init(MMU *mmu, struct CPU *cpu, struct Timer *timer, struct PPU *ppu,
              struct Joypad *joypad, struct APU *apu);
void mmu_reset(MMU *mmu);

// free the memory allocated for the banking controller
void mmu_cleanup(MMU *mmu);

// read a 8bit value from the memory bus
uint8_t mmu_read(MMU *mmu, uint16_t addr);

// read a 16bit value from the memory bus
uint16_t mmu_read16(MMU *mmu, uint16_t addr);

// write a 8bit value to the memory bus
void mmu_write(MMU *mmu, uint16_t addr, uint8_t value);

// write a 16bit value to the memory bus
void mmu_write16(MMU *mmu, uint16_t addr, uint16_t value);

#endif
