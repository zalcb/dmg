#include <assert.h>
#include <stdio.h>

#include "apu.h"

int main(void) {
    APU apu = {0};
    apu_init(&apu, NULL, NULL);
    assert(apu_read(&apu, NR52) == 0x70);
    apu_write(&apu, NR52, 0x80);
    for (unsigned value = 0; value < 256; value++) {
        apu_write(&apu, NR10, value);
        assert(apu_read(&apu, NR10) == (value | 0x80));
        apu_write(&apu, NR11, value);
        assert(apu_read(&apu, NR11) == (value | 0x3F));
        apu_write(&apu, NR12, value);
        assert(apu_read(&apu, NR12) == value);
        apu_write(&apu, NR22, value);
        assert(apu_read(&apu, NR22) == value);
        apu_write(&apu, NR30, value);
        assert(apu_read(&apu, NR30) == (value | 0x7F));
        apu_write(&apu, NR32, value);
        assert(apu_read(&apu, NR32) == (value | 0x9F));
        apu_write(&apu, NR42, value);
        assert(apu_read(&apu, NR42) == value);
        apu_write(&apu, NR43, value);
        assert(apu_read(&apu, NR43) == value);
        apu_write(&apu, NR50, value);
        assert(apu_read(&apu, NR50) == (value & 0x77));
        apu_write(&apu, NR51, value);
        assert(apu_read(&apu, NR51) == value);
        for (unsigned address = 0xFF30; address <= 0xFF3F; address++) {
            apu_write(&apu, address, value);
            assert(apu_read(&apu, address) == value);
        }
    }
    const uint16_t dac[] = {NR12, NR22, NR30, NR42};
    const uint16_t trigger[] = {NR14, NR24, NR34, NR44};
    for (unsigned channel = 0; channel < 4; channel++) {
        apu_write(&apu, dac[channel], 0);
        apu_write(&apu, trigger[channel], 0x80);
        assert(!(apu_read(&apu, NR52) & (1u << channel)));
        apu_write(&apu, dac[channel], 0x80);
        apu_write(&apu, trigger[channel], 0x80);
        assert(apu_read(&apu, NR52) & (1u << channel));
        apu_write(&apu, dac[channel], 0);
        assert(!(apu_read(&apu, NR52) & (1u << channel)));
    }
    assert(apu_read(&apu, NR13) == 0xFF);
    assert(apu_read(&apu, NR23) == 0xFF);
    assert(apu_read(&apu, NR31) == 0xFF);
    assert(apu_read(&apu, NR33) == 0xFF);
    assert(apu_read(&apu, NR41) == 0xFF);
    apu_cleanup(&apu);
    assert(apu.audio_buffer == NULL);
    puts("APU register, wave RAM and DAC trigger checks passed.");
    return 0;
}
