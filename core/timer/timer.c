#include "timer.h"

#include "../cpu/cpu.h"

/* helpers */
/* map TAC bits 1:0 to DIV bit numbers */
static inline uint8_t selected_div_bit(const Timer *t) {
    /* 00 -> 9, 01 -> 3, 10 -> 5, 11 -> 7 */
    static const uint8_t div_bit_map[4] = {9, 3, 5, 7};
    return div_bit_map[t->tac & 0x03];
}

static inline void request_interrupt(Timer *t) {
    /* request timer interrupt */
    t->cpu->ifr |= 0x04; /* set bit 2 of IF register */
}

void timer_init(Timer *timer, struct CPU *cpu, struct MMU *mmu) {
    timer->cpu = cpu;
    timer->mmu = mmu;

    timer_reset(timer);
}

void timer_reset(Timer *timer) {
    timer->div            = 0;
    timer->tima           = 0;
    timer->tma            = 0;
    timer->tac            = 0xF8;

    timer->prev_div_bit   = 0;
    timer->overflow_phase = 0xFF; /* idle */
}

/* called from advance_cycles in opcodes.h
cycles = 4, 8, 12, 16... */
void timer_step(Timer *timer, uint8_t cycles) {
    const uint8_t div_bit = selected_div_bit(timer);
    const uint16_t period = 2u << div_bit;

    while (cycles) {
        if (timer->overflow_phase == 0xFF) {
            if (!(timer->tac & 0x04)) {
                timer->div += cycles;
                return;
            }

            uint16_t until_edge = period - (timer->div & (period - 1));
            uint8_t next_bit = ((timer->div + 1) >> div_bit) & 1;
            if (timer->prev_div_bit == 1 && !next_bit) {
                until_edge = 1;
            } else if (until_edge == 1 && timer->prev_div_bit != 1) {
                until_edge += period;
            }

            if (cycles < until_edge) {
                timer->div += cycles;
                timer->prev_div_bit = (timer->div >> div_bit) & 1;
                return;
            }

            timer->div += until_edge - 1;
            cycles -= until_edge - 1;
            timer->prev_div_bit = 1;
        }

        cycles--;
        /* 1. increment DIV, our system counter */
        timer->div++;

        /* 2. falling edge detector (only if TAC is enabled -> bit 2) */
        if (timer->tac & 0x04) {
            uint8_t bit = (timer->div >> div_bit) &
                          0x01;  // get the selected bit (mux selector)
            uint8_t falling_edge = (timer->prev_div_bit == 1) && bit == 0;  // falling edge detector
            timer->prev_div_bit  = bit;  // update the previous bit

            if (falling_edge && timer->overflow_phase == 0xFF) {
                if (timer->tima == 0xFF) {
                    timer->tima           = 0x00;
                    timer->overflow_phase = 0x00;  // overflow phase
                } else {
                    timer->tima++;
                }
            }
        }

        /* 3. handle overflow */
        if (timer->overflow_phase != 0xFF) {
            timer->overflow_phase++; /* increment overflow phase */
            switch (timer->overflow_phase) {
                case 4: timer->tima = timer->tma; break; /* after 4 t-cycles have passed */
                case 5:
                    request_interrupt(timer);
                    timer->overflow_phase = 0xFF;
                    break; /* request interrupt w/ delay of 1 cycle and reset overflow phase */
            }
        }
    }
}

void timer_write_div(Timer *t) {
    if (t->tac & 0x04) {
        uint8_t div_bit = (t->div >> selected_div_bit(t)) & 0x01;
        if (div_bit && t->overflow_phase == 0xFF) {
            /* if the timer is enabled and we have a falling edge */
            if (t->tima == 0xFF) {
                t->tima           = 0x00; /* reset tima */
                t->overflow_phase = 0x00; /* start overflow phase */
            } else {
                t->tima++; /* increment tima */
            }
        }
    }
    /* reset DIV register */
    t->div          = 0; /* reset DIV register to 0x00 */
    t->prev_div_bit = 0; /* reset previous DIV bit to 0 */
}

void timer_write_tima(Timer *t, uint8_t value) {
    if (t->overflow_phase == 0xFF) {
        /* only write to tima if we are not in overflow phase */
        t->tima = value;
    } else if (t->overflow_phase < 4) {
        // writes to tima during overflow ABORT the overflow phase
        t->overflow_phase = 0xFF;  /* reset overflow phase */
        t->tima           = value; /* write the new value */
    } else if (t->overflow_phase == 4) {
        // ignore writes if tma is being loaded!
        return;
    } else {
        // if we are in overflow phase 5, we can finally write to tima
        t->tima = value;
    }
}

void timer_write_tma(Timer *t, uint8_t value) {
    t->tma = value; /* write the new value to tma */
    if (t->overflow_phase == 4) {
        t->tima = value; /* if we are in overflow phase 4, write tma to tima */
    }
}

void timer_write_tac(Timer *t, uint8_t value) {
    uint8_t prev_enable = t->tac & 0x04;                           // previous enable state
    uint8_t new_enable  = value & 0x04;                            // new enable state
    uint8_t prev_bit    = (t->div >> selected_div_bit(t)) & 0x01;  // previous selected DIV bit

    t->tac              = value | 0xF8;  // keep bits 3-7 unchanged, only update bits 0-2

    uint8_t new_bit     = (t->div >> selected_div_bit(t)) & 0x01;  // new selected DIV bit

    /* dmg glitch: going from enabled to disabled (NOT VICE-VERSA)
       or changing frequency causes a falling edge on the selected DIV bit */
    if ((prev_enable && !new_enable && prev_bit) ||             // enabled -> disabled
        (prev_enable && new_enable && prev_bit && !new_bit)) {  // changing frequency
        // falling edge caused by tac write
        if (t->overflow_phase == 0xFF) {
            if (t->tima == 0xFF) {
                t->tima           = 0x00; /* reset tima */
                t->overflow_phase = 0x00; /* start overflow phase */
            } else {
                t->tima++; /* increment tima */
            }
        }
    }

    t->prev_div_bit = new_bit;  // update previous DIV bit
}
