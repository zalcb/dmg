#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/audio/apu.h"
#include "../core/cpu/cpu.h"
#include "../core/input/joyp.h"
#include "../core/memory/mbc.h"
#include "../core/memory/mmu.h"
#include "../core/video/ppu.h"
#include "raylib.h"
#include "../core/memory/rom.h"
#include "../core/timer/timer.h"
#include "utils.h"

#ifndef DEFAULT_MODEL
#define DEFAULT_MODEL ROM_MODEL_DMG
#endif

// declare the components
MMU mmu;
CPU cpu;
Timer timer;
PPU ppu;
Joypad joypad;
APU apu;

void AudioInputCallback(void *buffer, unsigned int frames) {
    float *stream = (float *)buffer;

    apu_get_samples(&apu, stream, frames);
}

static void joypad_update(Joypad *pad) {
    uint8_t buttons = 0x0F;
    uint8_t dpad = 0x0F;
    if (IsKeyDown(KEY_Z)) buttons &= ~JOYP_A;
    if (IsKeyDown(KEY_X)) buttons &= ~JOYP_B;
    if (IsKeyDown(KEY_ENTER)) buttons &= ~JOYP_START;
    if (IsKeyDown(KEY_SPACE)) buttons &= ~JOYP_SELECT;
    if (IsKeyDown(KEY_RIGHT)) dpad &= ~JOYP_RIGHT;
    if (IsKeyDown(KEY_LEFT)) dpad &= ~JOYP_LEFT;
    if (IsKeyDown(KEY_UP)) dpad &= ~JOYP_UP;
    if (IsKeyDown(KEY_DOWN)) dpad &= ~JOYP_DOWN;
    joypad_set_state(pad, buttons, dpad);
}

typedef struct {
    const char *rom_file;
    const char *trace_file;
    rom_model model;
} Options;

static bool parse_options(int argc, char **argv, Options *options) {
    *options = (Options){.model = DEFAULT_MODEL};
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--trace") && i + 1 < argc && !options->trace_file) {
            options->trace_file = argv[++i];
        } else if (!strcmp(argv[i], "--model") && i + 1 < argc) {
            const char *model = argv[++i];
            if (!strcmp(model, "auto")) options->model = ROM_MODEL_AUTO;
            else if (!strcmp(model, "dmg")) options->model = ROM_MODEL_DMG;
            else if (!strcmp(model, "cgb")) options->model = ROM_MODEL_CGB;
            else return false;
        } else if (argv[i][0] != '-' && !options->rom_file) {
            options->rom_file = argv[i];
        } else return false;
    }
    return options->rom_file != NULL;
}

static void update_display(Color *display) {
    for (int y = 0; y < HEIGHT_PX; y++) {
        for (int x = 0; x < WIDTH_PX; x++) {
            Color color = dmg_palette[ppu.framebuffer[y][x] & 3];
            if (mmu.cgb_mode) {
                uint16_t pixel = ppu.color_framebuffer[y][x];
                unsigned r = pixel & 31, g = (pixel >> 5) & 31, b = (pixel >> 10) & 31;
                color = (Color){(r << 3) | (r >> 2), (g << 3) | (g >> 2),
                                (b << 3) | (b >> 2), 255};
            }
            display[y * WIDTH_PX + x] = color;
        }
    }
}

int main(int argc, char *argv[]) {
    Options options;
    if (!parse_options(argc, argv, &options)) {
        fprintf(stderr, "usage: %s [--model dmg|cgb|auto] [--trace FILE] <rom_file>\n", argv[0]);
        return 1;
    }
    if (options.trace_file && !(cpu_log = fopen(options.trace_file, "w"))) {
        perror(options.trace_file);
        return 1;
    }

    // initialize and reset components
    mmu_init(&mmu, &cpu, &timer, &ppu, &joypad, &apu);
    cpu_init(&cpu, &mmu, &timer, &ppu, &apu);
    timer_init(&timer, &cpu, &mmu);
    ppu_init(&ppu, &mmu, &cpu);
    apu_init(&apu, &cpu, &mmu);
    joypad_init(&joypad, &mmu, &cpu);

    load_rom(&mmu, options.rom_file);
    if (!rom_select_model(&mmu, options.model)) {
        apu_cleanup(&apu);
        mmu_cleanup(&mmu);
        if (cpu_log) fclose(cpu_log);
        return 1;
    }
    cpu_init(&cpu, &mmu, &timer, &ppu, &apu);
    if (mmu.cgb_mode) cpu.pc = 0x100;
    else load_boot_rom(&mmu, BOOT_ROM_PATH);

    // raylib init
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(WIDTH_PX * DISPLAY_SCALE, HEIGHT_PX * DISPLAY_SCALE,
               mmu.cgb_mode ? "dmg emulator - Game Boy Color" : "dmg emulator - Game Boy");
    SetTargetFPS(60);

    // initialize audio
    InitAudioDevice();
    SetAudioStreamBufferSizeDefault(1024);

    AudioStream stream = LoadAudioStream(48000, 32, 2);
    SetAudioStreamCallback(stream, AudioInputCallback);
    PlayAudioStream(stream);
    SetAudioStreamVolume(stream, 0.05f);  // restore some volume after improvements

    Image image       = GenImageColor(WIDTH_PX, HEIGHT_PX, BLANK);
    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);

    Color display[WIDTH_PX * HEIGHT_PX];

    uint32_t frame_counter = 0;

    while (!WindowShouldClose()) {
        // poll keyboard input
        joypad_update(&joypad);

        // run the CPU until a frame has been completed
        ppu.frame_completed = 0;

        uint64_t frame_budget = cpu.base_cycles + 70224;
        while (!ppu.frame_completed && cpu.base_cycles < frame_budget) {
            cpu_step(&cpu);  // run the CPU. this also ticks all other components
        }

        frame_counter++;
        if (frame_counter >= FRAMES_PER_RTC_TICK) {
            mbc_update_rtc(&mmu.mbc);
            frame_counter = 0;
        }

        BeginDrawing();
        ClearBackground(BLACK);

        update_display(display);

        UpdateTexture(texture, display);

        DrawTextureEx(texture, (Vector2){0, 0}, 0.0f, DISPLAY_SCALE, WHITE);

        DrawFPS(5, 5);

        EndDrawing();
    }

    UnloadAudioStream(stream);
    CloseAudioDevice();

    UnloadTexture(texture);
    CloseWindow();

    apu_cleanup(&apu);
    mmu_cleanup(&mmu);
    if (cpu_log && fclose(cpu_log)) {
        perror("trace close");
        return 1;
    }
    return 0;
}
