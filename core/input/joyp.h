#ifndef JOYP_HEADER
#define JOYP_HEADER

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define JOYP 0xFF00  // joypad I/O register address

// joypad state bits (0 = pressed, 1 = not pressed)
#define JOYP_A 0x01       // bit 0: a
#define JOYP_B 0x02       // bit 1: b/left
#define JOYP_SELECT 0x04  // bit 2: select/up
#define JOYP_START 0x08   // bit 3: start/down

#define JOYP_RIGHT 0x01  // bit 0: right
#define JOYP_LEFT 0x02   // bit 1: left
#define JOYP_UP 0x04     // bit 2: up
#define JOYP_DOWN 0x08   // bit 3: down

// forward declarations
struct MMU;
struct CPU;

typedef struct Joypad {
    struct MMU *mmu;  // pointer to the MMU
    struct CPU *cpu;  // pointer to the CPU

    uint8_t joyp;     // joypad state register
    bool joyp_ready;  // flag to indicate if joypad state is ready for reading

    // joypad state bits
    uint8_t buttons;  // buttons state (0 = pressed, 1 = not pressed)
    uint8_t dpad;     // D-PAD state (0 = pressed, 1 = not pressed)
} Joypad;

void joypad_init(Joypad *joypad, struct MMU *mmu, struct CPU *cpu);
void joypad_reset(Joypad *joypad);
uint8_t joypad_read(Joypad *joypad);
void joypad_write(Joypad *joypad, uint8_t value);
void joypad_set_state(Joypad *joypad, uint8_t buttons, uint8_t dpad);

#endif
