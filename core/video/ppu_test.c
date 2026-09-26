#include "ppu.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "../cpu/cpu.h"
#include "../memory/mmu.h"

#define CYCLES_PER_SCANLINE 456
#define CYCLES_OAM_SCAN 80
#define CYCLES_VBLANK_SCANLINE 456
#define SCANLINES_PER_FRAME 154

#define CYCLES_DRAWING_AVG 172

#define VBLANK_INTERRUPT 0
#define LCD_INTERRUPT 1

static void reference_reset(PPU *ppu) {

    memset(ppu->framebuffer, 0, sizeof(ppu->framebuffer));

    ppu->scanline_cycles  = 0;
    ppu->current_scanline = 0;
    ppu->mode             = PPU_MODE_OAM_SEARCH;
    ppu->frame_completed  = 0;

    uint8_t stat          = mmu_read(ppu->mmu, STAT);
    stat                  = (stat & 0xFC) | (ppu->mode);

    mmu_write(ppu->mmu, LY, 0);
    mmu_write(ppu->mmu, STAT, stat);

    ppu->window_line_counter = 0;
    ppu->window_was_visible  = false;
}

static inline void request_interrupt(PPU *ppu, int interrupt_type) {
    if (interrupt_type == VBLANK_INTERRUPT) {
        ppu->cpu->ifr |= 0x01;
    } else if (interrupt_type == LCD_INTERRUPT) {
        ppu->cpu->ifr |= 0x02;
    }
}

static void check_lyc_match(PPU *ppu) {
    uint8_t stat = mmu_read(ppu->mmu, STAT);
    uint8_t lyc  = mmu_read(ppu->mmu, LYC);

    if (ppu->current_scanline == lyc) {
        stat |= 0x04;
        if (stat & 0x40) {

            request_interrupt(ppu, LCD_INTERRUPT);
        }
    } else {
        stat &= ~0x04;
    }
    mmu_write(ppu->mmu, STAT, stat);
}

static void change_mode(PPU *ppu, ppu_mode new_mode) {
    ppu->mode    = new_mode;
    uint8_t stat = mmu_read(ppu->mmu, STAT);
    stat &= 0xFC;
    stat |= new_mode;

    int interrupt_requested = 0;
    if (new_mode == PPU_MODE_OAM_SEARCH && (stat & 0x20)) {
        interrupt_requested = 1;
    } else if (new_mode == PPU_MODE_VBLANK && (stat & 0x10)) {
        interrupt_requested = 1;
    } else if (new_mode == PPU_MODE_HBLANK && (stat & 0x08)) {
        interrupt_requested = 1;
    }

    if (interrupt_requested) {
        request_interrupt(ppu, LCD_INTERRUPT);
    }

    mmu_write(ppu->mmu, STAT, stat);
}

static void render_background_in_scanline(PPU *ppu) {
    uint8_t lcdc = mmu_read(ppu->mmu, LCDC);
    if (!(lcdc & 0x01)) {
        for (int x = 0; x < LCD_WIDTH; x++) {
            ppu->framebuffer[ppu->current_scanline][x] = COLOR_WHITE;
        }
        return;
    }

    uint8_t scx                     = mmu_read(ppu->mmu, SCX);
    uint8_t scy                     = mmu_read(ppu->mmu, SCY);
    uint8_t bgp                     = mmu_read(ppu->mmu, BGP);

    uint16_t tile_map_base_address  = (lcdc & 0x08) ? 0x9C00 : 0x9800;
    uint16_t tile_data_base_address = (lcdc & 0x10) ? 0x8000 : 0x9000;
    int signed_tile_data            = (lcdc & 0x10) ? 0 : 1;

    for (int x = 0; x < LCD_WIDTH; x++) {

        uint8_t y                 = (ppu->current_scanline + scy) & 0xFF;
        uint8_t xw                = (x + scx) & 0xFF;

        int tile_x                = (xw >> 3);
        int tile_y                = (y >> 3);
        int pixel_x               = xw % 8;
        int pixel_y               = y % 8;

        uint16_t tile_map_address = tile_map_base_address + tile_y * 32 + tile_x;
        uint8_t tile_index        = mmu_read(ppu->mmu, tile_map_address);

        uint16_t tile_data_address =
            tile_data_base_address + (signed_tile_data ? (int8_t)tile_index : tile_index) * 16;
        uint8_t low_byte  = mmu_read(ppu->mmu, tile_data_address + pixel_y * 2);
        uint8_t high_byte = mmu_read(ppu->mmu, tile_data_address + pixel_y * 2 + 1);

        uint8_t color_index =
            ((low_byte >> (7 - pixel_x)) & 0x01) | (((high_byte >> (7 - pixel_x)) & 0x01) << 1);

        ppu->framebuffer[ppu->current_scanline][x] =
            (bgp >> (color_index * 2)) & 0x03;
    }
}

static void render_window_in_scanline(PPU *ppu) {

    uint8_t lcdc = mmu_read(ppu->mmu, LCDC);
    if (!(lcdc & 0x20)) {
        return;
    }

    uint8_t wx = mmu_read(ppu->mmu, WX);
    uint8_t wy = mmu_read(ppu->mmu, WY);

    if (ppu->current_scanline < wy) {
        return;
    }

    int start_x = wx - 7;
    if (start_x >= LCD_WIDTH) {
        return;
    }

    if (!ppu->window_was_visible && ppu->current_scanline == wy) {
        ppu->window_line_counter = 0;
        ppu->window_was_visible  = true;
    }

    uint8_t window_y                = ppu->window_line_counter;

    uint8_t bgp                     = mmu_read(ppu->mmu, BGP);
    uint16_t tile_map_base_address  = (lcdc & 0x40) ? 0x9C00 : 0x9800;
    uint16_t tile_data_base_address = (lcdc & 0x10) ? 0x8000 : 0x9000;
    int signed_tile_data            = (lcdc & 0x10) ? 0 : 1;

    bool window_rendered            = false;

    for (int lcd_x = (start_x > 0) ? start_x : 0; lcd_x < LCD_WIDTH; lcd_x++) {
        window_rendered           = true;
        uint8_t window_x          = lcd_x - start_x;

        int tile_x                = (window_x >> 3);
        int tile_y                = (window_y >> 3);
        int pixel_x               = window_x % 8;
        int pixel_y               = window_y % 8;

        uint16_t tile_map_address = tile_map_base_address + tile_y * 32 + tile_x;
        uint8_t tile_index        = mmu_read(ppu->mmu, tile_map_address);

        uint16_t tile_data_address =
            tile_data_base_address + (signed_tile_data ? (int8_t)tile_index : tile_index) * 16;
        uint8_t low_byte  = mmu_read(ppu->mmu, tile_data_address + pixel_y * 2);
        uint8_t high_byte = mmu_read(ppu->mmu, tile_data_address + pixel_y * 2 + 1);

        uint8_t color_index =
            ((low_byte >> (7 - pixel_x)) & 0x01) | (((high_byte >> (7 - pixel_x)) & 0x01) << 1);

        ppu->framebuffer[ppu->current_scanline][lcd_x] =
            (bgp >> (color_index * 2)) & 0x03;
    }

    if (window_rendered) {
        ppu->window_line_counter++;
    }
}

static void scan_oam(PPU *ppu) {

    uint8_t lcdc = mmu_read(ppu->mmu, LCDC);
    if (!(lcdc & 0x02)) {
        ppu->num_scanline_sprites = 0;
        return;
    }

    int sprite_height         = (lcdc & 0x04) ? 16 : 8;

    ppu->num_scanline_sprites = 0;

    for (int i = 0; i < 40 && ppu->num_scanline_sprites < MAX_SPRITES_PER_SCANLINE; i++) {

        uint16_t oam_address = OAM_START + i * 4;
        uint8_t y            = mmu_read(ppu->mmu, oam_address);
        uint8_t x            = mmu_read(ppu->mmu, oam_address + 1);
        uint8_t tile         = mmu_read(ppu->mmu, oam_address + 2);
        uint8_t attributes   = mmu_read(ppu->mmu, oam_address + 3);

        if (y == 0 || y >= 160) {
            continue;
        }

        int sprite_top    = y - 16;
        int sprite_bottom = sprite_top + sprite_height;

        if (ppu->current_scanline >= sprite_top && ppu->current_scanline < sprite_bottom) {

            ppu->scanline_sprites[ppu->num_scanline_sprites].y          = y;
            ppu->scanline_sprites[ppu->num_scanline_sprites].x          = x;
            ppu->scanline_sprites[ppu->num_scanline_sprites].tile       = tile;
            ppu->scanline_sprites[ppu->num_scanline_sprites].attributes = attributes;
            ppu->scanline_sprites[ppu->num_scanline_sprites].oam_index  = i;
            ppu->num_scanline_sprites++;
        }
    }

    for (int i = 0; i < ppu->num_scanline_sprites - 1; i++) {
        for (int j = i + 1; j < ppu->num_scanline_sprites; j++) {
            if (ppu->scanline_sprites[i].x > ppu->scanline_sprites[j].x ||
                (ppu->scanline_sprites[i].x == ppu->scanline_sprites[j].x &&
                 ppu->scanline_sprites[i].oam_index > ppu->scanline_sprites[j].oam_index)) {

                sprite_t temp            = ppu->scanline_sprites[i];
                ppu->scanline_sprites[i] = ppu->scanline_sprites[j];
                ppu->scanline_sprites[j] = temp;
            }
        }
    }
}

static void render_sprites_in_scanline(PPU *ppu) {

    uint8_t lcdc = mmu_read(ppu->mmu, LCDC);
    if (!(lcdc & 0x02)) {
        return;
    }

    int sprite_height = (lcdc & 0x04) ? 16 : 8;

    uint8_t obp0      = mmu_read(ppu->mmu, OBP0);
    uint8_t obp1      = mmu_read(ppu->mmu, OBP1);

    for (int i = ppu->num_scanline_sprites - 1; i >= 0; i--) {
        sprite_t *sprite = &ppu->scanline_sprites[i];

        int sprite_x     = sprite->x - 8;
        int sprite_y     = sprite->y - 16;

        if (sprite_x >= LCD_WIDTH || sprite_x <= -8) {
            continue;
        }

        int sprite_row = ppu->current_scanline - sprite_y;

        if (sprite_row < 0 || sprite_row >= sprite_height) {
            continue;
        }

        if (sprite->attributes & 0x40) {
            sprite_row = sprite_height - 1 - sprite_row;
        }

        uint8_t tile_index = sprite->tile;
        if (sprite_height == 16) {
            tile_index &= 0xFE;
            if (sprite_row >= 8) {
                tile_index++;
                sprite_row -= 8;
            }
        }

        if (tile_index > 255 || sprite_row > 7)
            continue;

        uint16_t tile_data_address = 0x8000 + tile_index * 16 + sprite_row * 2;

        if (tile_data_address >= 0xA000) {
            continue;
        }

        uint8_t low_byte  = mmu_read(ppu->mmu, tile_data_address);
        uint8_t high_byte = mmu_read(ppu->mmu, tile_data_address + 1);

        for (int pixel_x = 0; pixel_x < 8; pixel_x++) {
            int screen_x = sprite_x + pixel_x;

            if (screen_x < 0 || screen_x >= LCD_WIDTH) {
                continue;
            }

            int bit_index = (sprite->attributes & 0x20) ? pixel_x : (7 - pixel_x);

            uint8_t color_index =
                ((low_byte >> bit_index) & 0x01) | (((high_byte >> bit_index) & 0x01) << 1);

            if (color_index == 0) {
                continue;
            }

            if (sprite->attributes & 0x80) {
                uint8_t bg_color = ppu->framebuffer[ppu->current_scanline][screen_x];
                if (bg_color != 0) {
                    continue;
                }
            }

            uint8_t palette = (sprite->attributes & 0x10) ? obp1 : obp0;

            ppu->framebuffer[ppu->current_scanline][screen_x] =
                (palette >> (color_index * 2)) & 0x03;
        }
    }
}

static void reference_step(PPU *ppu, int cycles) {
    uint8_t lcdc = mmu_read(ppu->mmu, LCDC);

    if (!(lcdc & 0x80)) {
        if (ppu->current_scanline != 0 || ppu->mode != PPU_MODE_VBLANK) {

            ppu->current_scanline = 0;
            ppu->scanline_cycles  = 0;
            mmu_write(ppu->mmu, LY, 0);
            ppu->mode = PPU_MODE_HBLANK;
        }
        return;
    }

    ppu->scanline_cycles += cycles;

    switch (ppu->mode) {
        case PPU_MODE_OAM_SEARCH:
            if (ppu->scanline_cycles >= CYCLES_OAM_SCAN) {
                ppu->scanline_cycles -= CYCLES_OAM_SCAN;
                scan_oam(ppu);
                change_mode(ppu, PPU_MODE_DRAWING);
            }
            break;

        case PPU_MODE_DRAWING:

            if (ppu->scanline_cycles >= CYCLES_DRAWING_AVG) {
                ppu->scanline_cycles -= CYCLES_DRAWING_AVG;

                if (ppu->current_scanline < LCD_HEIGHT) {
                    render_background_in_scanline(ppu);
                    render_window_in_scanline(ppu);
                    render_sprites_in_scanline(ppu);
                }
                change_mode(ppu, PPU_MODE_HBLANK);
            }
            break;

        case PPU_MODE_HBLANK:

            if (ppu->scanline_cycles >=
                (CYCLES_PER_SCANLINE - CYCLES_OAM_SCAN - CYCLES_DRAWING_AVG)) {
                ppu->scanline_cycles -=
                    (CYCLES_PER_SCANLINE - CYCLES_OAM_SCAN - CYCLES_DRAWING_AVG);

                ppu->current_scanline++;
                mmu_write(ppu->mmu, LY, ppu->current_scanline);
                check_lyc_match(ppu);

                if (ppu->current_scanline == LCD_HEIGHT) {

                    change_mode(ppu, PPU_MODE_VBLANK);
                    request_interrupt(ppu, VBLANK_INTERRUPT);
                    ppu->frame_completed = 1;
                } else {
                    change_mode(ppu, PPU_MODE_OAM_SEARCH);
                }
            }
            break;

        case PPU_MODE_VBLANK:

            if (ppu->scanline_cycles >= CYCLES_VBLANK_SCANLINE) {
                ppu->scanline_cycles -= CYCLES_VBLANK_SCANLINE;
                ppu->current_scanline++;
                mmu_write(ppu->mmu, LY, ppu->current_scanline);
                check_lyc_match(ppu);

                if (ppu->current_scanline >= SCANLINES_PER_FRAME) {

                    ppu->current_scanline = 0;
                    mmu_write(ppu->mmu, LY, 0);
                    check_lyc_match(ppu);
                    ppu->window_line_counter = 0;
                    ppu->window_was_visible  = false;

                    change_mode(ppu, PPU_MODE_OAM_SEARCH);
                }
            }
            break;
    }
}

typedef struct {
    PPU ppu;
    MMU mmu;
    CPU cpu;
} Fixture;

static Fixture actual;
static Fixture expected;
static uint32_t random_state = 0x5EED1234;
static unsigned comparisons;

static uint32_t random_u32(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static void random_bytes(uint8_t *bytes, size_t size) {
    for (size_t i = 0; i < size; i++) {
        bytes[i] = (uint8_t)random_u32();
    }
}

static void bind_fixture(Fixture *fixture) {
    fixture->ppu.mmu = &fixture->mmu;
    fixture->ppu.cpu = &fixture->cpu;
    fixture->mmu.ppu = &fixture->ppu;
    fixture->mmu.cpu = &fixture->cpu;
    fixture->cpu.ppu = &fixture->ppu;
    fixture->cpu.mmu = &fixture->mmu;
}

static void initialize_fixture(Fixture *fixture) {
    memset(fixture, 0, sizeof(*fixture));
    bind_fixture(fixture);
    random_bytes(fixture->mmu.vram, sizeof(fixture->mmu.vram));
    random_bytes(fixture->mmu.oam, sizeof(fixture->mmu.oam));
    random_bytes(fixture->mmu.io, sizeof(fixture->mmu.io));
    random_bytes(&fixture->ppu.framebuffer[0][0], sizeof(fixture->ppu.framebuffer));
    fixture->cpu.ifr = (uint8_t)random_u32();
    fixture->cpu.ier = (uint8_t)random_u32();
    fixture->cpu.ime = random_u32() & 1;
    fixture->cpu.cycles = random_u32();
    fixture->ppu.mode = PPU_MODE_OAM_SEARCH;
}

static void clone_expected(void) {
    memcpy(&expected, &actual, sizeof(expected));
    bind_fixture(&expected);
}

static void compare_state(const char *scenario, unsigned iteration) {
    PPU ppu;
    MMU mmu;
    CPU cpu;
    memcpy(&ppu, &expected.ppu, sizeof(ppu));
    memcpy(&mmu, &expected.mmu, sizeof(mmu));
    memcpy(&cpu, &expected.cpu, sizeof(cpu));
    ppu.mmu = actual.ppu.mmu;
    ppu.cpu = actual.ppu.cpu;
    mmu.ppu = actual.mmu.ppu;
    mmu.cpu = actual.mmu.cpu;
    cpu.ppu = actual.cpu.ppu;
    cpu.mmu = actual.cpu.mmu;
    if (memcmp(&ppu, &actual.ppu, sizeof(ppu)) ||
        memcmp(&mmu, &actual.mmu, sizeof(mmu)) ||
        memcmp(&cpu, &actual.cpu, sizeof(cpu))) {
        fprintf(stderr, "%s iteration %u seed 0x%08x: PPU=%d MMU=%d CPU=%d\n",
                scenario, iteration, random_state,
                memcmp(&ppu, &actual.ppu, sizeof(ppu)),
                memcmp(&mmu, &actual.mmu, sizeof(mmu)),
                memcmp(&cpu, &actual.cpu, sizeof(cpu)));
        for (int y = 0; y < LCD_HEIGHT; y++) {
            for (int x = 0; x < LCD_WIDTH; x++) {
                if (actual.ppu.framebuffer[y][x] != expected.ppu.framebuffer[y][x]) {
                    fprintf(stderr, "pixel (%d,%d): actual=%u expected=%u\n", x, y,
                            actual.ppu.framebuffer[y][x], expected.ppu.framebuffer[y][x]);
                    exit(EXIT_FAILURE);
                }
            }
        }
        exit(EXIT_FAILURE);
    }
    comparisons++;
}

static void step_both(int cycles, const char *scenario, unsigned iteration) {
    ppu_step(&actual.ppu, cycles);
    reference_step(&expected.ppu, cycles);
    compare_state(scenario, iteration);
}

static void set_register(uint16_t address, uint8_t value) {
    mmu_write(&actual.mmu, address, value);
    mmu_write(&expected.mmu, address, value);
}

static void populate_sprites(Fixture *fixture, unsigned variant) {
    static const uint8_t x_positions[] = {0, 1, 7, 8, 8, 9, 80, 159, 160, 167, 168, 255};
    for (unsigned i = 0; i < 40; i++) {
        fixture->mmu.oam[i * 4] = (uint8_t)(fixture->ppu.current_scanline + 16 -
                                           ((variant + i) & 15));
        fixture->mmu.oam[i * 4 + 1] = x_positions[(variant + i) % 12];
        fixture->mmu.oam[i * 4 + 2] = (uint8_t)random_u32();
        fixture->mmu.oam[i * 4 + 3] = (uint8_t)((variant + i) << 4);
    }
}

static void test_scanlines(void) {
    static const uint8_t window_x[] = {0, 1, 6, 7, 8, 15, 159, 160, 165, 166, 167, 255};
    for (unsigned i = 0; i < 8192; i++) {
        initialize_fixture(&actual);
        actual.ppu.current_scanline = random_u32() % LCD_HEIGHT;
        actual.ppu.scanline_cycles = random_u32() % 80;
        actual.ppu.window_line_counter = (uint8_t)random_u32();
        actual.ppu.window_was_visible = (i & 1) != 0;
        actual.mmu.io[LCDC - 0xFF00] = 0x80 | (i & 0x7F);
        actual.mmu.io[SCX - 0xFF00] = (uint8_t)i;
        actual.mmu.io[SCY - 0xFF00] = (uint8_t)(i >> 3);
        actual.mmu.io[WX - 0xFF00] = i < 4096 ? window_x[(i >> 7) % 12] : (uint8_t)(i >> 4);
        actual.mmu.io[WY - 0xFF00] = (uint8_t)(actual.ppu.current_scanline + (int)(i % 3) - 1);
        if (i & 2) {
            populate_sprites(&actual, i >> 2);
        }
        if (i % 17 == 0) {
            memset(actual.mmu.vram, 0, 0x1800);
        } else if (i % 19 == 0) {
            memset(actual.mmu.vram, 0xFF, 0x1800);
        }
        clone_expected();
        step_both(80, "scanline OAM", i);
        step_both(171 - actual.ppu.scanline_cycles, "before drawing boundary", i);
        step_both(1, "drawing boundary", i);
        step_both(203, "before HBLANK boundary", i);
        step_both(1, "HBLANK boundary", i);
    }
}

static void test_frames(void) {
    initialize_fixture(&actual);
    clone_expected();
    ppu_reset(&actual.ppu);
    reference_reset(&expected.ppu);
    compare_state("reset", 0);
    set_register(LCDC, 0xFF);
    set_register(WY, 0);
    set_register(WX, 0);
    for (unsigned i = 0; i < 12000; i++) {
        if (i % 29 == 0) {
            set_register(SCX, (uint8_t)random_u32());
            set_register(SCY, (uint8_t)random_u32());
            set_register(BGP, (uint8_t)random_u32());
            set_register(OBP0, (uint8_t)random_u32());
            set_register(OBP1, (uint8_t)random_u32());
            set_register(STAT, (uint8_t)random_u32());
            set_register(LYC, random_u32() % 154);
            set_register(WX, (uint8_t)random_u32());
            set_register(WY, random_u32() % 144);
            set_register(LCDC, 0x80 | (random_u32() & 0x7F));
            set_register(IF, (uint8_t)random_u32());
        }
        for (unsigned j = 0; j < 16; j++) {
            uint16_t address = 0x8000 + (random_u32() & 0x1FFF);
            uint8_t value = (uint8_t)random_u32();
            mmu_write(&actual.mmu, address, value);
            mmu_write(&expected.mmu, address, value);
        }
        unsigned oam = random_u32() % sizeof(actual.mmu.oam);
        actual.mmu.oam[oam] = expected.mmu.oam[oam] = (uint8_t)random_u32();
        step_both(random_u32() % 301, "running frames with writes", i);
    }
    for (unsigned mode = 0; mode < 4; mode++) {
        for (unsigned line = 0; line < 154; line++) {
            actual.ppu.mode = (ppu_mode)mode;
            actual.ppu.current_scanline = (uint8_t)line;
            actual.ppu.scanline_cycles = random_u32() % 456;
            clone_expected();
            set_register(LCDC, 0x7F);
            step_both(456, "LCD disabled", mode * 154 + line);
            set_register(LCDC, 0xFF);
            step_both(456, "LCD re-enabled", mode * 154 + line);
        }
    }
}

static uint64_t framebuffer_hash(const PPU *ppu) {
    uint64_t hash = 14695981039346656037ULL;
    for (int y = 0; y < LCD_HEIGHT; y++) {
        for (int x = 0; x < LCD_WIDTH; x++) {
            hash = (hash ^ ppu->framebuffer[y][x]) * 1099511628211ULL;
        }
    }
    return hash;
}

static double benchmark_run(Fixture *fixture, void (*step)(PPU *, int), unsigned frames) {
    clock_t start = clock();
    for (unsigned frame = 0; frame < frames; frame++) {
        for (unsigned line = 0; line < LCD_HEIGHT; line++) {
            fixture->mmu.io[SCX - 0xFF00] = (uint8_t)(frame + line);
            fixture->mmu.io[SCY - 0xFF00] = (uint8_t)(frame * 3);
            step(&fixture->ppu, 80);
            step(&fixture->ppu, 172);
            step(&fixture->ppu, 204);
        }
        for (unsigned line = 0; line < 10; line++) {
            step(&fixture->ppu, 456);
        }
    }
    return (double)(clock() - start) / CLOCKS_PER_SEC;
}

static void benchmark(unsigned frames) {
    static const uint8_t controls[] = {0x91, 0x81, 0xF1, 0xE1, 0xF7, 0xE7};
    static const char *names[] = {"background unsigned", "background signed", "window unsigned",
                                  "window signed", "sprites unsigned", "sprites signed"};
    for (unsigned i = 0; i < sizeof(controls); i++) {
        random_state = 0x5EED1234;
        initialize_fixture(&actual);
        actual.mmu.io[LCDC - 0xFF00] = controls[i];
        actual.mmu.io[WX - 0xFF00] = 3;
        actual.mmu.io[WY - 0xFF00] = 0;
        for (unsigned sprite = 0; sprite < 40; sprite++) {
            actual.mmu.oam[sprite * 4] = (uint8_t)(16 + sprite * 3);
            actual.mmu.oam[sprite * 4 + 1] = (uint8_t)(8 + (sprite * 7) % 160);
        }
        clone_expected();
        double baseline = benchmark_run(&expected, reference_step, frames);
        double optimized = benchmark_run(&actual, ppu_step, frames);
        compare_state(names[i], frames);
        printf("%-20s baseline %.6fs optimized %.6fs speedup %.2fx hash %016llx\n",
               names[i], baseline, optimized, baseline / optimized,
               (unsigned long long)framebuffer_hash(&actual.ppu));
    }
}

int main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "--benchmark") == 0) {
        unsigned frames = argc == 3 ? (unsigned)strtoul(argv[2], NULL, 10) : 300;
        if (frames == 0 || argc > 3) {
            fprintf(stderr, "usage: %s [--benchmark [frames]]\n", argv[0]);
            return EXIT_FAILURE;
        }
        benchmark(frames);
        return EXIT_SUCCESS;
    }
    if (argc != 1) {
        fprintf(stderr, "usage: %s [--benchmark [frames]]\n", argv[0]);
        return EXIT_FAILURE;
    }
    test_scanlines();
    test_frames();
    printf("PPU equivalence: %u full-state comparisons passed (seed 0x5eed1234)\n", comparisons);
    return EXIT_SUCCESS;
}
