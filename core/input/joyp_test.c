#include <assert.h>
#include <stdio.h>

#include "joyp.h"
#include "../cpu/cpu.h"

int main(void) {
    for (unsigned old = 0; old < 256; old++) {
        for (unsigned next = 0; next < 256; next++) {
            CPU cpu = {0};
            Joypad pad;
            joypad_init(&pad, NULL, &cpu);
            joypad_set_state(&pad, old & 15, old >> 4);
            cpu.ifr = 0xA5;
            joypad_set_state(&pad, next & 15, next >> 4);
            assert(pad.buttons == (next & 15));
            assert(pad.dpad == (next >> 4));
            assert(cpu.ifr == (0xA5 | ((old & ~next) ? 0x10 : 0)));
            joypad_write(&pad, 0x10);
            assert(joypad_read(&pad) == (0xD0 | (next & 15)));
            joypad_write(&pad, 0x20);
            assert(joypad_read(&pad) == (0xE0 | (next >> 4)));
            joypad_write(&pad, 0x30);
            assert(joypad_read(&pad) == 0xFF);
        }
    }
    puts("Joypad tests passed: 65536 input transitions.");
    return 0;
}
