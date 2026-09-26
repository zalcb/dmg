#ifndef RUNNER_OUTPUT_H
#define RUNNER_OUTPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "../core/video/ppu.h"

struct CoreMachine;
struct RunnerOptions;

typedef struct RunnerOutput {
    uint64_t frames, samples, steps, frame_hash, audio_hash;
    uint8_t rgb[LCD_HEIGHT][LCD_WIDTH][3];
    char serial[4096];
    size_t serial_size;
    FILE *video;
    bool matched;
} RunnerOutput;

bool runner_output_open(RunnerOutput *output, const struct RunnerOptions *options);
bool runner_capture(RunnerOutput *output, struct CoreMachine *m,
                    const struct RunnerOptions *options);
void runner_serial(RunnerOutput *output, struct CoreMachine *m,
                   const struct RunnerOptions *options);
bool runner_output_close(RunnerOutput *output);
bool runner_save_frame(const RunnerOutput *output, const char *path);
void runner_summary(const RunnerOutput *output, struct CoreMachine *m,
                    const struct RunnerOptions *options, double elapsed);

#endif
