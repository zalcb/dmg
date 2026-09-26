#include "ppu.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../cpu/cpu.h"
#include "../memory/mmu.h"

static PPU ppu;
static MMU mmu;
static CPU cpu;

static void set_color(uint8_t *palette, unsigned number, unsigned index, uint16_t color) {
    palette[number * 8 + index * 2] = color;
    palette[number * 8 + index * 2 + 1] = color >> 8;
}

static void setup(void) {
    memset(&ppu, 0, sizeof(ppu));
    memset(&mmu, 0, sizeof(mmu));
    memset(&cpu, 0, sizeof(cpu));
    mmu.cpu = &cpu;
    mmu.ppu = &ppu;
    cpu.mmu = &mmu;
    cpu.ppu = &ppu;
    mmu.cgb_mode = true;
    mmu.io[LCDC - 0xFF00] = 0x93;
    ppu_init(&ppu, &mmu, &cpu);
    memset(ppu.framebuffer, 0xA5, sizeof(ppu.framebuffer));
    for (unsigned palette = 0; palette < 8; palette++) {
        for (unsigned color = 0; color < 4; color++) {
            set_color(mmu.bg_palette, palette, color, 0x1000 + palette * 16 + color);
            set_color(mmu.obj_palette, palette, color, 0x2000 + palette * 16 + color);
        }
    }
}

static void tile_row(unsigned bank, unsigned tile, unsigned row, uint8_t low, uint8_t high) {
    uint8_t *vram = bank ? mmu.vram_bank1 : mmu.vram;
    vram[tile * 16 + row * 2] = low;
    vram[tile * 16 + row * 2 + 1] = high;
}

static void sprite(unsigned index, uint8_t x, uint8_t y, uint8_t tile, uint8_t attributes) {
    mmu.oam[index * 4] = y;
    mmu.oam[index * 4 + 1] = x;
    mmu.oam[index * 4 + 2] = tile;
    mmu.oam[index * 4 + 3] = attributes;
}

static void draw(unsigned line) {
    ppu.current_scanline = line;
    ppu.mode = PPU_MODE_OAM_SEARCH;
    ppu.scanline_cycles = 0;
    ppu_step(&ppu, 80);
    assert(ppu.mode == PPU_MODE_DRAWING);
    ppu_step(&ppu, 172);
    assert(ppu.mode == PPU_MODE_HBLANK);
    for (unsigned x = 0; x < LCD_WIDTH; x++) {
        assert(ppu.framebuffer[line][x] == 0xA5);
    }
}

static void test_background_attributes(void) {
    for (unsigned palette = 0; palette < 8; palette++) {
        for (unsigned bank = 0; bank < 2; bank++) {
            for (unsigned flips = 0; flips < 4; flips++) {
                setup();
                mmu_write(&mmu, VBK, bank ^ 1);
                mmu.vram[0x1800] = 3;
                mmu.vram_bank1[0x1800] = palette | (bank << 3) | (flips << 5) | 0x10;
                unsigned row = (flips & 2) ? 7 : 0;
                tile_row(bank, 3, row, 0xAA, 0xCC);
                draw(0);
                for (unsigned x = 0; x < 8; x++) {
                    unsigned bit = (flips & 1) ? x : 7 - x;
                    unsigned color = ((0xAA >> bit) & 1) | (((0xCC >> bit) & 1) << 1);
                    assert(ppu.color_framebuffer[0][x] == 0x1000 + palette * 16 + color);
                }
            }
        }
    }
    setup();
    mmu.io[LCDC - 0xFF00] = 0x89;
    mmu.io[SCX - 0xFF00] = 255;
    mmu.io[SCY - 0xFF00] = 255;
    mmu.vram[0x1FFF] = 0x80;
    mmu.vram[0x1FE0] = 0x7F;
    tile_row(0, 0x80, 7, 0xFF, 0);
    tile_row(0, 0x17F, 7, 0, 0xFF);
    draw(0);
    assert(ppu.color_framebuffer[0][0] == 0x1001);
    assert(ppu.color_framebuffer[0][1] == 0x1002);
    tile_row(0, 0x100, 0, 0xFF, 0xFF);
    draw(1);
    assert(ppu.color_framebuffer[1][0] == 0x1003);
    set_color(mmu.bg_palette, 0, 3, 0xFFFF);
    draw(1);
    assert(ppu_get_color_framebuffer(&ppu)[1][0] == 0x7FFF);
}

static void test_window(void) {
    setup();
    mmu.io[LCDC - 0xFF00] = 0xF0;
    mmu.io[WY - 0xFF00] = 1;
    mmu.io[WX - 0xFF00] = 10;
    mmu.vram[0x1C00] = 1;
    mmu.vram_bank1[0x1C00] = 0x0B;
    tile_row(1, 1, 0, 0x80, 0x40);
    tile_row(1, 1, 1, 0xFF, 0xFF);
    draw(0);
    assert(ppu.window_line_counter == 0);
    draw(1);
    assert(ppu.color_framebuffer[1][2] == 0x1000);
    assert(ppu.color_framebuffer[1][3] == 0x1031);
    assert(ppu.color_framebuffer[1][4] == 0x1032);
    assert(ppu.window_line_counter == 1);
    mmu.io[WX - 0xFF00] = 167;
    draw(2);
    assert(ppu.window_line_counter == 1);
    mmu.io[WX - 0xFF00] = 166;
    draw(3);
    assert(ppu.color_framebuffer[3][158] == 0x1000);
    assert(ppu.color_framebuffer[3][159] == 0x1033);
    assert(ppu.window_line_counter == 2);
    mmu.io[LCDC - 0xFF00] &= ~0x20;
    draw(4);
    assert(ppu.window_line_counter == 2);
    mmu.io[LCDC - 0xFF00] |= 0x20;
    mmu.io[WX - 0xFF00] = 0;
    tile_row(1, 1, 2, 1, 0);
    draw(5);
    assert(ppu.color_framebuffer[5][0] == 0x1031);
    assert(ppu.window_line_counter == 3);
    ppu.current_scanline = 153;
    ppu.mode = PPU_MODE_VBLANK;
    ppu_step(&ppu, 456);
    assert(ppu.window_line_counter == 0);
    assert(!ppu.window_was_visible);
}

static void test_priority_matrix(void) {
    for (unsigned master = 0; master < 2; master++) {
        for (unsigned bg_priority = 0; bg_priority < 2; bg_priority++) {
            for (unsigned obj_priority = 0; obj_priority < 2; obj_priority++) {
                for (unsigned bg_color = 0; bg_color < 4; bg_color++) {
                    setup();
                    mmu.io[LCDC - 0xFF00] = 0x92 | master;
                    mmu.vram_bank1[0x1800] = bg_priority << 7;
                    tile_row(0, 0, 0, (bg_color & 1) ? 0xFF : 0,
                             (bg_color & 2) ? 0xFF : 0);
                    tile_row(0, 1, 0, 0xFF, 0);
                    sprite(0, 8, 16, 1, obj_priority << 7);
                    set_color(mmu.bg_palette, 0, bg_color, bg_color ? 0 : 0x7FFF);
                    draw(0);
                    bool hidden = master && bg_color && (bg_priority || obj_priority);
                    assert(ppu.color_framebuffer[0][0] == (hidden ? 0 : 0x2001));
                }
            }
        }
    }
    setup();
    mmu.io[LCDC - 0xFF00] |= 0x60;
    mmu.io[WX - 0xFF00] = 7;
    mmu.vram[0x1C00] = 1;
    mmu.vram_bank1[0x1C00] = 0x80;
    tile_row(0, 1, 0, 0xFF, 0);
    tile_row(0, 2, 0, 0, 0xFF);
    sprite(0, 8, 16, 2, 0);
    draw(0);
    assert(ppu.color_framebuffer[0][0] == 0x1001);
}

static void test_sprite_attributes(void) {
    for (unsigned palette = 0; palette < 8; palette++) {
        for (unsigned bank = 0; bank < 2; bank++) {
            for (unsigned flips = 0; flips < 4; flips++) {
                for (unsigned tall = 0; tall < 2; tall++) {
                    setup();
                    mmu.io[LCDC - 0xFF00] |= tall << 2;
                    unsigned row = (flips & 2) ? (tall ? 15 : 7) : 0;
                    unsigned tile = tall ? 2 + row / 8 : 3;
                    tile_row(bank, tile, row % 8, 0xAA, 0xCC);
                    sprite(0, 8, 16, 3, palette | (bank << 3) | (flips << 5) | 0x10);
                    draw(0);
                    for (unsigned x = 0; x < 8; x++) {
                        unsigned bit = (flips & 1) ? x : 7 - x;
                        unsigned color = ((0xAA >> bit) & 1) | (((0xCC >> bit) & 1) << 1);
                        assert(ppu.color_framebuffer[0][x] ==
                               (color ? 0x2000 + palette * 16 + color : 0x1000));
                    }
                }
            }
        }
    }
}

static void test_sprite_order(void) {
    for (unsigned opri = 0; opri < 2; opri++) {
        setup();
        mmu_write(&mmu, OPRI, opri);
        tile_row(0, 0, 0, 0xFF, 0);
        tile_row(0, 1, 0, 0xFF, 0);
        tile_row(0, 2, 0, 0, 0xFF);
        sprite(0, 12, 16, 1, 0);
        sprite(1, 8, 16, 2, 0);
        draw(0);
        assert(ppu.color_framebuffer[0][4] == (opri ? 0x2002 : 0x2001));
        mmu.oam[(opri ? 1 : 0) * 4 + 3] = 0x80;
        draw(0);
        assert(ppu.color_framebuffer[0][4] == 0x1001);
        tile_row(0, opri ? 2 : 1, 0, 0, 0);
        draw(0);
        assert(ppu.color_framebuffer[0][4] == (opri ? 0x2001 : 0x2002));
        sprite(0, 8, 16, 1, 0);
        sprite(1, 8, 16, 2, 0);
        tile_row(0, 1, 0, 0xFF, 0);
        tile_row(0, 2, 0, 0, 0xFF);
        draw(0);
        assert(ppu.color_framebuffer[0][0] == 0x2001);
    }
    setup();
    tile_row(0, 1, 0, 0xFF, 0);
    for (unsigned i = 0; i < 10; i++) {
        sprite(i, 0, 16, 1, 0);
    }
    sprite(10, 8, 16, 1, 0);
    draw(0);
    assert(ppu.num_scanline_sprites == 10);
    assert(ppu.color_framebuffer[0][0] == 0x1000);
    sprite(0, 1, 16, 1, 0);
    sprite(1, 167, 16, 1, 0);
    draw(0);
    assert(ppu.color_framebuffer[0][0] == 0x2001);
    assert(ppu.color_framebuffer[0][1] == 0x1000);
    assert(ppu.color_framebuffer[0][159] == 0x2001);
}

static void test_hblank_and_reset(void) {
    setup();
    memset(mmu.wram, 0xFF, 48);
    mmu_write(&mmu, HDMA1, 0xC0);
    mmu_write(&mmu, HDMA2, 0);
    mmu_write(&mmu, HDMA3, 0);
    mmu_write(&mmu, HDMA4, 0);
    mmu_write(&mmu, HDMA5, 0x81);
    ppu_step(&ppu, 80);
    ppu_step(&ppu, 171);
    assert(mmu.vram[0] == 0);
    ppu_step(&ppu, 1);
    assert(ppu.color_framebuffer[0][0] == 0x1000);
    assert(mmu.vram[0] == 0xFF);
    assert(mmu.vram[16] == 0);
    ppu_step(&ppu, 203);
    assert(mmu.vram[16] == 0);
    ppu_step(&ppu, 1);
    ppu_step(&ppu, 80);
    ppu_step(&ppu, 172);
    assert(ppu.color_framebuffer[1][0] == 0x1003);
    assert(mmu.vram[16] == 0xFF);
    ppu.current_scanline = 144;
    ppu.mode = PPU_MODE_VBLANK;
    ppu.scanline_cycles = 0;
    mmu_write(&mmu, HDMA5, 0x80);
    assert(mmu.hdma_blocks == 1);
    ppu_step(&ppu, 456);
    assert(mmu.vram[32] == 0);
    assert(mmu.hdma_blocks == 1);
    mmu.io[LCDC - 0xFF00] = 0;
    ppu_step(&ppu, 456);
    assert(mmu.vram[32] == 0);
    assert(mmu.hdma_blocks == 1);
    memset(ppu.color_framebuffer, 0xFF, sizeof(ppu.color_framebuffer));
    ppu_reset(&ppu);
    for (unsigned y = 0; y < LCD_HEIGHT; y++) {
        for (unsigned x = 0; x < LCD_WIDTH; x++) {
            assert(ppu.color_framebuffer[y][x] == 0);
            assert(ppu.framebuffer[y][x] == 0);
        }
    }
}

int main(void) {
    test_background_attributes();
    test_window();
    test_priority_matrix();
    test_sprite_attributes();
    test_sprite_order();
    test_hblank_and_reset();
    puts("CGB video: tile attributes, RGB555, window, sprite priority, HBlank and reset passed");
    return 0;
}
