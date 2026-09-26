#ifndef RUNNER_OPTIONS_H
#define RUNNER_OPTIONS_H

#include <stdbool.h>
#include <stdint.h>

#include "../core/memory/rom.h"

struct CoreMachine;

typedef struct RunnerOptions {
    uint64_t limit;
    const char *rom, *boot, *trace, *expect, *frame, *video;
    enum rom_model model;
} RunnerOptions;

bool runner_options(RunnerOptions *options, int argc, char **argv);
bool runner_load(struct CoreMachine *m, const RunnerOptions *options);

#endif
