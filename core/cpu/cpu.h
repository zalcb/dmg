#ifndef CPU_HEADER
#define CPU_HEADER

#include <stdint.h>
#include <stdio.h>


extern FILE *cpu_log;

struct MMU;
struct Timer;
struct PPU;
struct APU;

typedef struct CPU {
    struct MMU *mmu;     /* pointer to the MMU */
    struct Timer *timer; /* pointer to the timer */
    struct PPU *ppu;     /* pointer to the PPU */
    struct APU *apu;     /* pointer to the APU */

    // a: accumulator; f: flags
    union {
        struct {
            uint8_t f, a;
        };
        uint16_t af;
    };

    union {
        struct {
            uint8_t c, b;
        };
        uint16_t bc;
    };

    union {
        struct {
            uint8_t e, d;
        };
        uint16_t de;
    };

    union {
        struct {
            uint8_t l, h;
        };
        uint16_t hl;
    };

    // stack pointer and program counter are both 16 bits
    uint16_t sp, pc;

    // cpu cycle counter
    uint64_t cycles;

    // interrupt enable register
    int ime;       /* 0 or 1, current state (READ ONLY) */
    int ime_delay; /* two cycle ime delay  */

    /* 0xFF0F, interrupt flag register
    - bit 0: v-blank
    - bit 1: LCD
    - bit 2: timer
    - bit 3: serial
    - bit 4: joypad
    any bits set to 1 are interrupt requests
    */
    uint8_t ifr;

    /* 0xFFFF, interrupt enable register
    - bit 0: v-blank
    - bit 1: LCD
    - bit 2: timer
    - bit 3: serial
    - bit 4: joypad
    any bits set to 1 are enabled interrupts
    */
    uint8_t ier;

    /* HALT flag */
    int halt;     /* 0 or 1, current state */
    int halt_bug; /* 0 or 1, double read bug */

    /* OAM flag (interrupts not allowed) */
    int dma_flag; /* 0 or 1, current state */

    // last opcode executed (debugging)
    uint8_t last_opcode;

} CPU;

void tick(CPU *cpu, int cycles);

void cpu_init(CPU *cpu, struct MMU *mmu, struct Timer *timer, struct PPU *ppu,
              struct APU *apu);
void cpu_step(CPU *cpu);

#endif
