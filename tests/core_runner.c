#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <time.h>

#include "core_machine.h"
#include "runner_options.h"
#include "runner_output.h"

static double seconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

int main(int argc, char **argv) {
    RunnerOptions options;
    if (!runner_options(&options, argc, argv)) {
        fprintf(stderr, "usage: %s [--cycles N] [--trace FILE] [--model auto|dmg|cgb] "
                "[--frame FILE.ppm] [--video FILE.rgb] "
                "[--rom FILE [--boot FILE] [--expect TEXT]]\n", argv[0]);
        return 1;
    }
    CoreMachine m;
    core_init(&m);
    if (!runner_load(&m, &options)) {
        core_cleanup(&m);
        return 1;
    }
    RunnerOutput output;
    if (!runner_output_open(&output, &options)) {
        runner_output_close(&output);
        core_cleanup(&m);
        return 1;
    }
    bool ok = true;
    double start = seconds();
    while (m.cpu.cycles < options.limit) {
        cpu_step(&m.cpu);
        ++output.steps;
        if (!(ok = runner_capture(&output, &m, &options))) break;
        runner_serial(&output, &m, &options);
        if (output.matched) break;
    }
    if (cpu_log && fflush(cpu_log)) {
        perror("trace flush");
        ok = false;
    }
    double elapsed = seconds() - start;
    runner_summary(&output, &m, &options, elapsed);
    if (ok) ok = runner_save_frame(&output, options.frame);
    if (!runner_output_close(&output)) ok = false;
    core_cleanup(&m);
    if (!ok) return 1;
    if (options.expect && !output.matched) {
        fprintf(stderr, "Expected serial text not seen within cycle budget: %s\n", options.expect);
        return 2;
    }
    return 0;
}
