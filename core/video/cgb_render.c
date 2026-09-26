#include "cgb_render.h"

#include "ppu.h"
#include "../memory/mmu.h"

static uint16_t palette_color(const uint8_t *palette, uint8_t attributes, uint8_t color) {
    unsigned offset = (attributes & 7) * 8 + color * 2;
    return (palette[offset] | ((uint16_t)palette[offset + 1] << 8)) & 0x7FFF;
}

static void render_tiles(PPU *ppu, uint8_t lcdc, uint8_t *background) {
    MMU *mmu = ppu->mmu;
    uint8_t scx = mmu_read(mmu, SCX);
    uint8_t scy = mmu_read(mmu, SCY);
    int window_x = (int)mmu_read(mmu, WX) - 7;
    bool window = (lcdc & 0x20) && ppu->current_scanline >= mmu_read(mmu, WY) &&
                  window_x < LCD_WIDTH;

    for (int x = 0; x < LCD_WIDTH;) {
        bool in_window = window && x >= window_x;
        uint8_t tile_x = in_window ? x - window_x : x + scx;
        uint8_t tile_y = in_window ? ppu->window_line_counter : ppu->current_scanline + scy;
        uint16_t map = (lcdc & (in_window ? 0x40 : 0x08)) ? 0x9C00 : 0x9800;
        uint16_t entry = map + (tile_y >> 3) * 32 + (tile_x >> 3);
        uint8_t tile = mmu_vram_read(mmu, 0, entry);
        uint8_t attributes = mmu_vram_read(mmu, 1, entry);
        int row = tile_y & 7;
        if (attributes & 0x40) {
            row = 7 - row;
        }
        uint16_t address = (lcdc & 0x10) ? 0x8000 + tile * 16 : 0x9000 + (int8_t)tile * 16;
        address += row * 2;
        unsigned bank = (attributes >> 3) & 1;
        uint8_t low = mmu_vram_read(mmu, bank, address);
        uint8_t high = mmu_vram_read(mmu, bank, address + 1);
        int column = tile_x & 7;
        int end = x + 8 - column;
        if (end > LCD_WIDTH) {
            end = LCD_WIDTH;
        }
        if (window && !in_window && end > window_x) {
            end = window_x;
        }
        for (; x < end; x++, column++) {
            int bit = (attributes & 0x20) ? column : 7 - column;
            uint8_t color = ((low >> bit) & 1) | (((high >> bit) & 1) << 1);
            background[x] = color | (attributes & 0x80);
            ppu->color_framebuffer[ppu->current_scanline][x] =
                palette_color(mmu->bg_palette, attributes, color);
        }
    }
    if (window) {
        ppu->window_was_visible = true;
        ppu->window_line_counter++;
    }
}

static void render_sprites(PPU *ppu, uint8_t lcdc, const uint8_t *background) {
    if (!(lcdc & 2)) {
        return;
    }
    bool occupied[LCD_WIDTH] = {false};
    int height = (lcdc & 4) ? 16 : 8;
    MMU *mmu = ppu->mmu;

    for (int i = 0; i < ppu->num_scanline_sprites; i++) {
        const sprite_t *sprite = &ppu->scanline_sprites[i];
        int row = ppu->current_scanline - ((int)sprite->y - 16);
        if (row < 0 || row >= height) {
            continue;
        }
        if (sprite->attributes & 0x40) {
            row = height - 1 - row;
        }
        uint8_t tile = height == 16 ? sprite->tile & 0xFE : sprite->tile;
        uint16_t address = 0x8000 + tile * 16 + row * 2;
        unsigned bank = (sprite->attributes >> 3) & 1;
        uint8_t low = mmu_vram_read(mmu, bank, address);
        uint8_t high = mmu_vram_read(mmu, bank, address + 1);

        for (int column = 0; column < 8; column++) {
            int x = (int)sprite->x - 8 + column;
            if (x < 0 || x >= LCD_WIDTH || occupied[x]) {
                continue;
            }
            int bit = (sprite->attributes & 0x20) ? column : 7 - column;
            uint8_t color = ((low >> bit) & 1) | (((high >> bit) & 1) << 1);
            if (!color) {
                continue;
            }
            occupied[x] = true;
            if ((lcdc & 1) && (background[x] & 3) &&
                ((background[x] | sprite->attributes) & 0x80)) {
                continue;
            }
            ppu->color_framebuffer[ppu->current_scanline][x] =
                palette_color(mmu->obj_palette, sprite->attributes, color);
        }
    }
}

void cgb_render_scanline(PPU *ppu) {
    uint8_t background[LCD_WIDTH];
    uint8_t lcdc = mmu_read(ppu->mmu, LCDC);
    render_tiles(ppu, lcdc, background);
    render_sprites(ppu, lcdc, background);
}
