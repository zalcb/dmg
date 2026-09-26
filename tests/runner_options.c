#include "runner_options.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core_machine.h"

static bool runner_cycle_limit(const char *value, uint64_t *limit) {
    char *end;
    errno = 0;
    *limit = strtoull(value, &end, 10);
    return !errno && !*end && *value && *value != '-' && *limit &&
           *limit <= UINT64_MAX - 16384;
}

static bool runner_model(const char *value, enum rom_model *model) {
    const char *names[] = {"dmg", "cgb", "auto"};
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (!strcmp(value, names[i])) {
            *model = (enum rom_model)i;
            return true;
        }
    }
    return false;
}

bool runner_options(RunnerOptions *options, int argc, char **argv) {
    *options = (RunnerOptions){.limit = 4194304, .model = ROM_MODEL_AUTO};
    const char *names[] = {"--rom", "--boot", "--trace", "--expect", "--frame", "--video"};
    const char **fields[] = {&options->rom, &options->boot, &options->trace,
                            &options->expect, &options->frame, &options->video};
    for (int i = 1; i < argc; ++i) {
        const char *name = argv[i];
        if (++i == argc) return false;
        const char *value = argv[i];
        if (!strcmp(name, "--cycles")) {
            if (!runner_cycle_limit(value, &options->limit)) return false;
        } else if (!strcmp(name, "--model")) {
            if (!runner_model(value, &options->model)) return false;
        } else {
            unsigned field = 0;
            while (field < sizeof(names) / sizeof(names[0]) && strcmp(name, names[field])) ++field;
            if (field == sizeof(names) / sizeof(names[0])) return false;
            *fields[field] = value;
        }
    }
    return options->rom || (!options->boot && !options->expect);
}

bool runner_load(CoreMachine *m, const RunnerOptions *options) {
    if (options->rom) load_rom(&m->mmu, options->rom);
    if (!rom_select_model(&m->mmu, options->model)) {
        fprintf(stderr, "CGB-only ROM cannot run with --model dmg\n");
        return false;
    }
    if (options->boot && m->mmu.cgb_mode) {
        fprintf(stderr, "CGB boot ROMs are not supported; omit --boot\n");
        return false;
    }
    cpu_init(&m->cpu, &m->mmu, &m->timer, &m->ppu, &m->apu);
    m->cpu.pc = 0x100;
    if (options->boot) {
        load_boot_rom(&m->mmu, options->boot);
        m->cpu.pc = 0;
    }
    if (!options->rom) core_synthetic(m);
    return true;
}
