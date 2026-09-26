#include <stdio.h>

#include "apu.h"
#include "cpu.h"
#include "joyp.h"
#include "mbc.h"
#include "mmu.h"
#include "ppu.h"
#include "raylib.h"
#include "rom.h"
#include "timer.h"
#include "utils.h"

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

int main(int argc, char *argv[]) {
    const char *rom_file = NULL;
    const char *trace_file = NULL;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--trace") && i + 1 < argc && !trace_file) {
            trace_file = argv[++i];
        } else if (argv[i][0] != '-' && !rom_file) {
            rom_file = argv[i];
        } else {
            fprintf(stderr, "usage: %s [--trace FILE] <rom_file>\n", argv[0]);
            return 1;
        }
    }
    if (!rom_file) {
        fprintf(stderr, "usage: %s [--trace FILE] <rom_file>\n", argv[0]);
        return 1;
    }
    if (trace_file && !(cpu_log = fopen(trace_file, "w"))) {
        perror(trace_file);
        return 1;
    }

    // initialize and reset components
    mmu_init(&mmu, &cpu, &timer, &ppu, &joypad, &apu);
    cpu_init(&cpu, &mmu, &timer, &ppu, &apu);
    timer_init(&timer, &cpu, &mmu);
    ppu_init(&ppu, &mmu, &cpu);
    apu_init(&apu, &cpu, &mmu);
    joypad_init(&joypad, &mmu, &cpu);

    load_boot_rom(&mmu, BOOT_ROM_PATH);
    load_rom(&mmu, rom_file);

    // raylib init
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(WIDTH_PX * DISPLAY_SCALE, HEIGHT_PX * DISPLAY_SCALE, "dmg emulator");
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

        while (!ppu.frame_completed) {
            cpu_step(&cpu);  // run the CPU. this also ticks all other components
        }

        frame_counter++;
        if (frame_counter >= FRAMES_PER_RTC_TICK) {
            mbc_update_rtc(&mmu.mbc);
        }

        BeginDrawing();
        ClearBackground(BLACK);

        const uint8_t (*ppu_framebuffer)[LCD_WIDTH] = ppu_get_framebuffer(&ppu);

        for (int y = 0; y < HEIGHT_PX; y++) {
            for (int x = 0; x < WIDTH_PX; x++) {
                display[y * WIDTH_PX + x] = dmg_palette[ppu_framebuffer[y][x] & 0x03];
            }
        }

        UpdateTexture(texture, display);

        DrawTextureEx(texture, (Vector2){0, 0}, 0.0f, DISPLAY_SCALE, WHITE);

        DrawFPS(5, 5);

        EndDrawing();
    }

    UnloadAudioStream(stream);
    CloseAudioDevice();

    UnloadTexture(texture);
    CloseWindow();

    free(apu.audio_buffer);
    mmu_cleanup(&mmu);
    if (cpu_log && fclose(cpu_log)) {
        perror("trace close");
        return 1;
    }
    return 0;
}
