#include "joyp.h"

#include <stdint.h>

#include "../cpu/cpu.h"

void joypad_init(Joypad *joypad, struct MMU *mmu, struct CPU *cpu) {
    joypad->mmu = mmu;
    joypad->cpu = cpu;
    joypad_reset(joypad);
}

void joypad_reset(Joypad *joypad) {
    joypad->joyp       = 0x3F;   // reset joypad state to default (all buttons unpressed)
    joypad->joyp_ready = false;  // reset ready state
    joypad->buttons    = 0x0F;
    joypad->dpad       = 0x0F;
}

void joypad_write(Joypad *joypad, uint8_t value) {
    // write to JOYP register
    joypad->joyp       = value & 0x30;  // keep bits 4-5 (select D-PAD or buttons)
    joypad->joyp_ready = true;          // mark joypad state as ready for reading
}

uint8_t joypad_read(Joypad *joypad) {
    uint8_t result = 0xC0 | (joypad->joyp & 0x30) | 0x0F;

    if (!(joypad->joyp & 0x10)) {  // if D-PAD is selected (bit 4 = 0)
        result &= (result & 0xF0) | joypad->dpad;
    } else if (!(joypad->joyp & 0x20)) {  // if buttons are selected (bit 5 = 0)
        result &= (result & 0xF0) | joypad->buttons;
    }

    return result;  // return the joypad state
}

void joypad_set_state(Joypad *joypad, uint8_t buttons, uint8_t dpad) {
    uint8_t old_buttons = joypad->buttons;  // save old buttons state
    uint8_t old_dpad    = joypad->dpad;     // save old D-PAD state

    joypad->buttons = buttons & 0x0F;
    joypad->dpad = dpad & 0x0F;

    uint8_t button_was_pressed = old_buttons & ~joypad->buttons;
    uint8_t dpad_was_pressed   = old_dpad & ~joypad->dpad;

    // if any button or D-PAD direction was pressed, request an interrupt
    if (button_was_pressed || dpad_was_pressed) {
        joypad->cpu->ifr |= 0x10;  // set bit 4 of IF register to request JOYP interrupt
    }
}
