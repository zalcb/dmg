#include "../cpu/cpu.h"
#include "timer.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t comparisons;
static uint32_t random_state = 0x6d2b79f5;

static uint8_t reference_bit(const Timer *timer) {
    static const uint8_t bits[] = {9, 3, 5, 7};
    return (timer->div >> bits[timer->tac & 3]) & 1;
}

static void reference_step(Timer *timer, uint8_t cycles) {
    while (cycles--) {
        timer->div++;
        if (timer->tac & 4) {
            uint8_t bit = reference_bit(timer);
            uint8_t falling_edge = timer->prev_div_bit == 1 && bit == 0;
            timer->prev_div_bit = bit;
            if (falling_edge && timer->overflow_phase == 0xff) {
                if (timer->tima == 0xff) {
                    timer->tima = 0;
                    timer->overflow_phase = 0;
                } else {
                    timer->tima++;
                }
            }
        }
        if (timer->overflow_phase != 0xff) {
            timer->overflow_phase++;
            switch (timer->overflow_phase) {
                case 4: timer->tima = timer->tma; break;
                case 5:
                    timer->cpu->ifr |= 4;
                    timer->overflow_phase = 0xff;
                    break;
            }
        }
    }
}

static void reference_write_div(Timer *timer) {
    if ((timer->tac & 4) && reference_bit(timer) && timer->overflow_phase == 0xff) {
        if (timer->tima == 0xff) {
            timer->tima = 0;
            timer->overflow_phase = 0;
        } else {
            timer->tima++;
        }
    }
    timer->div = 0;
    timer->prev_div_bit = 0;
}

static void reference_write_tima(Timer *timer, uint8_t value) {
    if (timer->overflow_phase == 0xff) {
        timer->tima = value;
    } else if (timer->overflow_phase < 4) {
        timer->overflow_phase = 0xff;
        timer->tima = value;
    } else if (timer->overflow_phase != 4) {
        timer->tima = value;
    }
}

static void reference_write_tma(Timer *timer, uint8_t value) {
    timer->tma = value;
    if (timer->overflow_phase == 4) {
        timer->tima = value;
    }
}

static void reference_write_tac(Timer *timer, uint8_t value) {
    uint8_t previous_enable = timer->tac & 4;
    uint8_t next_enable = value & 4;
    uint8_t previous_bit = reference_bit(timer);
    timer->tac = value | 0xf8;
    uint8_t next_bit = reference_bit(timer);
    if ((previous_enable && !next_enable && previous_bit) ||
        (previous_enable && next_enable && previous_bit && !next_bit)) {
        if (timer->overflow_phase == 0xff) {
            if (timer->tima == 0xff) {
                timer->tima = 0;
                timer->overflow_phase = 0;
            } else {
                timer->tima++;
            }
        }
    }
    timer->prev_div_bit = next_bit;
}

static uint32_t random_u32(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static void require(int condition, const char *description) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", description);
        exit(EXIT_FAILURE);
    }
}

static void compare(const Timer *actual, const Timer *expected, const char *where) {
    comparisons++;
    if (actual->div != expected->div || actual->tima != expected->tima ||
        actual->tma != expected->tma || actual->tac != expected->tac ||
        actual->prev_div_bit != expected->prev_div_bit ||
        actual->overflow_phase != expected->overflow_phase ||
        actual->cpu->ifr != expected->cpu->ifr || actual->mmu != expected->mmu) {
        fprintf(stderr, "FAIL: %s, comparison=%" PRIu64 ", seed=%08" PRIx32 "\n"
                "         DIV  TIMA TMA TAC PREV PHASE IF\n"
                "actual   %04x %04x %02x  %02x  %u    %02x    %02x\n"
                "expected %04x %04x %02x  %02x  %u    %02x    %02x\n",
                where, comparisons, random_state,
                actual->div, actual->tima, actual->tma, actual->tac,
                actual->prev_div_bit, actual->overflow_phase, actual->cpu->ifr,
                expected->div, expected->tima, expected->tma, expected->tac,
                expected->prev_div_bit, expected->overflow_phase, expected->cpu->ifr);
        exit(EXIT_FAILURE);
    }
}

static void check_step(const Timer *initial, uint8_t cycles) {
    CPU actual_cpu = {.ifr = initial->cpu->ifr};
    CPU expected_cpu = {.ifr = initial->cpu->ifr};
    Timer actual = *initial;
    Timer expected = *initial;
    actual.cpu = &actual_cpu;
    expected.cpu = &expected_cpu;
    timer_step(&actual, cycles);
    reference_step(&expected, cycles);
    compare(&actual, &expected, "step");
    require(actual.cpu == &actual_cpu, "step preserves CPU pointer");
}

static void regressions(void) {
    CPU cpu = {.ifr = 0xa1};
    Timer timer;
    timer_init(&timer, &cpu, NULL);
    require(timer.div == 0 && timer.tima == 0 && timer.tma == 0 &&
            timer.tac == 0xf8 && timer.prev_div_bit == 0 &&
            timer.overflow_phase == 0xff, "initial state");
    timer_write_tac(&timer, 5);
    timer_write_tima(&timer, 0xff);
    timer_write_tma(&timer, 0x42);
    timer_step(&timer, 16);
    require(timer.tima == 0 && timer.overflow_phase == 1 && cpu.ifr == 0xa1,
            "falling-edge overflow advances immediately to phase one");
    timer_step(&timer, 0);
    require(timer.overflow_phase == 1, "zero cycles leaves pending overflow alone");
    timer_step(&timer, 2);
    require(timer.tima == 0 && timer.overflow_phase == 3, "reload delay");
    timer_step(&timer, 1);
    require(timer.tima == 0x42 && timer.overflow_phase == 4 && cpu.ifr == 0xa1,
            "reload occurs before interrupt");
    timer_write_tima(&timer, 0x99);
    require(timer.tima == 0x42, "TIMA write ignored during reload");
    timer_write_tma(&timer, 0x67);
    require(timer.tima == 0x67, "TMA write also changes TIMA during reload");
    timer_write_tac(&timer, 0);
    timer_step(&timer, 1);
    require(timer.overflow_phase == 0xff && cpu.ifr == 0xa5,
            "disabled timer still requests pending interrupt, preserving IF bits");

    for (unsigned phase = 0; phase < 4; phase++) {
        timer_reset(&timer);
        timer_write_tac(&timer, 5);
        timer_write_tima(&timer, 0xff);
        timer_step(&timer, 8);
        timer_write_div(&timer);
        require(timer.tima == 0 && timer.overflow_phase == 0,
                "DIV write overflow begins at phase zero");
        timer_step(&timer, phase);
        timer_write_tima(&timer, 0x91);
        timer_step(&timer, 5 - phase);
        require(timer.tima == 0x91 && timer.overflow_phase == 0xff,
                "TIMA writes abort phases zero through three");
    }

    timer_reset(&timer);
    timer_write_tac(&timer, 5);
    timer_step(&timer, 8);
    timer_write_tac(&timer, 0);
    require(timer.tima == 1 && timer.prev_div_bit == 0,
            "disabling high selected bit creates a falling edge");
    timer_write_tac(&timer, 5);
    timer_write_tac(&timer, 6);
    require(timer.tima == 2, "changing selected high bit to low creates a falling edge");
    timer_write_tac(&timer, 5);
    timer_write_tac(&timer, 1);
    timer_step(&timer, 255);
    require(timer.prev_div_bit == 1, "disabled timer retains previous bit");
    timer.div = 0xffff;
    timer_step(&timer, 1);
    require(timer.div == 0 && timer.prev_div_bit == 1, "disabled DIV wraps at 16 bits");
}

static void boundary_matrix(void) {
    static const uint16_t counts[] = {0, 1, 0xfe, 0xff, 0x100, 0xffff};
    static const uint8_t phases[] = {0xff, 0, 1, 2, 3, 4};
    static const uint16_t dividers[] = {
        0, 1, 7, 8, 14, 15, 16, 30, 31, 32, 62, 63, 64,
        126, 127, 128, 254, 255, 256, 510, 511, 512,
        1022, 1023, 1024, 0xff00, 0xfff7, 0xfff8, 0xfffe, 0xffff
    };
    CPU cpu = {.ifr = 0xb1};
    Timer timer;
    timer_init(&timer, &cpu, NULL);
    for (unsigned tac = 0; tac < 8; tac++) {
        timer.tac = 0xf8 | tac;
        for (size_t d = 0; d < sizeof(dividers) / sizeof(dividers[0]); d++) {
            timer.div = dividers[d];
            for (size_t n = 0; n < sizeof(counts) / sizeof(counts[0]); n++) {
                timer.tima = counts[n];
                for (size_t p = 0; p < sizeof(phases) / sizeof(phases[0]); p++) {
                    timer.overflow_phase = phases[p];
                    timer.tma = p & 1 ? 0xff : 0;
                    for (unsigned previous = 0; previous < 2; previous++) {
                        timer.prev_div_bit = previous;
                        for (unsigned cycles = 0; cycles <= UINT8_MAX; cycles++) {
                            check_step(&timer, cycles);
                        }
                    }
                }
            }
        }
    }
    static const uint8_t lengths[] = {0, 1, 4, 16, 255};
    for (unsigned tac = 0; tac < 8; tac++) {
        timer.tac = tac | 0xf8;
        timer.tima = 0xfe;
        timer.tma = 0xff;
        timer.overflow_phase = 0xff;
        for (unsigned div = 0; div <= UINT16_MAX; div++) {
            timer.div = div;
            timer.prev_div_bit = reference_bit(&timer);
            for (size_t n = 0; n < sizeof(lengths) / sizeof(lengths[0]); n++) {
                check_step(&timer, lengths[n]);
            }
        }
    }
}

static void randomized_sequences(void) {
    CPU actual_cpu = {0};
    CPU expected_cpu = {0};
    Timer actual;
    Timer expected;
    timer_init(&actual, &actual_cpu, NULL);
    timer_init(&expected, &expected_cpu, NULL);
    for (unsigned n = 0; n < 1000000; n++) {
        uint32_t random = random_u32();
        uint8_t value = random >> 16;
        switch (random & 15) {
            case 0:
                timer_write_div(&actual);
                reference_write_div(&expected);
                break;
            case 1:
            case 2:
                timer_write_tac(&actual, value);
                reference_write_tac(&expected, value);
                break;
            case 3:
            case 4:
                value = random & 0x100 ? 0xff : value;
                timer_write_tima(&actual, value);
                reference_write_tima(&expected, value);
                break;
            case 5:
                timer_write_tma(&actual, value);
                reference_write_tma(&expected, value);
                break;
            case 6:
                actual_cpu.ifr = expected_cpu.ifr = value;
                break;
            default:
                value = random & 0x200 ? value : (random >> 8) & 7;
                timer_step(&actual, value);
                reference_step(&expected, value);
                break;
        }
        compare(&actual, &expected, "randomized register sequence");
        if ((n & 255) == 0 || actual.overflow_phase != 0xff) {
            for (unsigned cycles = 0; cycles <= UINT8_MAX; cycles++) {
                check_step(&expected, cycles);
            }
        }
    }
}

static void overflow_writes(void) {
    static const uint16_t dividers[] = {0, 8, 32, 128, 512, 0xffff};
    static const uint8_t phases[] = {0xff, 0, 1, 2, 3, 4};
    static const uint8_t lengths[] = {0, 1, 2, 3, 4, 5, 6, 255};
    CPU actual_cpu = {0};
    CPU expected_cpu = {0};
    for (unsigned tac = 0; tac < 8; tac++) {
        for (size_t d = 0; d < sizeof(dividers) / sizeof(dividers[0]); d++) {
            for (size_t p = 0; p < sizeof(phases) / sizeof(phases[0]); p++) {
                for (unsigned value = 0; value <= UINT8_MAX; value++) {
                    for (unsigned operation = 0; operation < 4; operation++) {
                        Timer actual = {
                            .cpu = &actual_cpu,
                            .div = dividers[d],
                            .tima = phases[p] == 0xff ? 0xff : 0,
                            .tma = value,
                            .tac = 0xf8 | tac,
                            .overflow_phase = phases[p]
                        };
                        actual.prev_div_bit = reference_bit(&actual);
                        Timer expected = actual;
                        expected.cpu = &expected_cpu;
                        actual_cpu.ifr = expected_cpu.ifr = 0xa1;
                        switch (operation) {
                            case 0:
                                timer_write_div(&actual);
                                reference_write_div(&expected);
                                break;
                            case 1:
                                timer_write_tima(&actual, value);
                                reference_write_tima(&expected, value);
                                break;
                            case 2:
                                timer_write_tma(&actual, value);
                                reference_write_tma(&expected, value);
                                break;
                            case 3:
                                timer_write_tac(&actual, value);
                                reference_write_tac(&expected, value);
                                break;
                        }
                        compare(&actual, &expected, "overflow register writes");
                        for (size_t n = 0; n < sizeof(lengths) / sizeof(lengths[0]); n++) {
                            check_step(&actual, lengths[n]);
                        }
                    }
                }
            }
        }
    }
}

static void benchmark(void) {
    static const uint8_t lengths[] = {4, 16, 255};
    const unsigned iterations = 2000000;
    for (unsigned tac = 0; tac <= 4; tac++) {
        for (size_t n = 0; n < sizeof(lengths) / sizeof(lengths[0]); n++) {
            CPU cpu = {0};
            Timer timer;
            timer_init(&timer, &cpu, NULL);
            timer_write_tac(&timer, tac == 0 ? 0 : tac + 3);
            timer_write_tma(&timer, 0x80);
            clock_t start = clock();
            for (unsigned i = 0; i < iterations; i++) {
                timer_step(&timer, lengths[n]);
            }
            double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
            printf("tac=%u cycles=%3u ns/call=%8.3f checksum=%u\n",
                   timer.tac & 7, lengths[n], elapsed * 1e9 / iterations,
                   timer.div + timer.tima + timer.overflow_phase + cpu.ifr);
        }
    }
    CPU cpu = {0};
    Timer timer;
    timer_init(&timer, &cpu, NULL);
    timer_write_tac(&timer, 5);
    clock_t start = clock();
    for (unsigned i = 0; i < iterations; i++) {
        if ((i & 255) == 0) {
            timer_write_tac(&timer, (i >> 8) & 7);
            timer_write_tima(&timer, 0xff);
            timer_write_tma(&timer, i >> 16);
        }
        if ((i & 127) == 0) {
            timer_write_div(&timer);
        }
        timer_step(&timer, 4 * ((i & 3) + 1));
    }
    double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
    printf("mixed-register-writes ns/call=%8.3f checksum=%u\n",
           elapsed * 1e9 / iterations,
           timer.div + timer.tima + timer.overflow_phase + cpu.ifr);
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--benchmark") == 0) {
        benchmark();
        return EXIT_SUCCESS;
    }
    require(argc == 1, "usage: timer_test [--benchmark]");
    regressions();
    boundary_matrix();
    overflow_writes();
    randomized_sequences();
    printf("Timer tests passed: %" PRIu64 " differential comparisons.\n", comparisons);
    return EXIT_SUCCESS;
}
