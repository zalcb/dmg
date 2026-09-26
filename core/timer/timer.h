#ifndef TIMER_HEADER
#define TIMER_HEADER

#include <stdint.h>
#include <stdio.h>

struct CPU;
struct MMU;

typedef struct Timer {
    struct CPU *cpu;  // pointer to the CPU
    struct MMU *mmu;  // pointer to the MMU

    uint16_t div;   // divider register (0xFF04)
    uint16_t tima;  // timer counter register (0xFF05)
    uint8_t tma;    // timer modulo register (0xFF06)
    uint8_t tac;    // timer control register (0xFF07)

    uint8_t prev_div_bit;    // previous value of the DIV register
    uint8_t overflow_phase;  // overflow phase for the timer

} Timer;

void timer_init(Timer *timer, struct CPU *cpu, struct MMU *mmu);
void timer_reset(Timer *timer);
void timer_step(Timer *timer, uint8_t cycles);

/* mmu helpers */
void timer_write_div(Timer *t);                 /* write to 0xFF04 */
void timer_write_tima(Timer *t, uint8_t value); /* write to 0xFF05 */
void timer_write_tma(Timer *t, uint8_t value);  /* write to 0xFF06 */
void timer_write_tac(Timer *t, uint8_t value);  /* write to 0xFF07 */

#endif
