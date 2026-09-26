#include "ppu.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../cpu/cpu.h"
#include "../memory/mmu.h"

#define CYCLES_PER_SCANLINE 456
#define CYCLES_OAM_SCAN 80
#define CYCLES_VBLANK_SCANLINE 456  // each of the 10 VBLANK scanlines takes a full scanline time
#define SCANLINES_PER_FRAME 154     // 144 visible scanlines + 10 VBLANK scanlines

/* placeholder for drawing/rendering cycles, but this can be variable */
#define CYCLES_DRAWING_AVG 172  // usually between 172 and 289 dots

/* interrupt types */
#define VBLANK_INTERRUPT 0
#define LCD_INTERRUPT 1

/* function to initialize the PPU */
void ppu_init(PPU *ppu, struct MMU *mmu, struct CPU *cpu) {
    ppu->mmu = mmu;
    ppu->cpu = cpu;

    /* reset the PPU state */
    ppu_reset(ppu);
}

/* function to reset the PPU */
void ppu_reset(PPU *ppu) {
    /* reset the framebuffer */
    memset(ppu->framebuffer, 0, sizeof(ppu->framebuffer));

    /* reset the PPU state */
    ppu->scanline_cycles  = 0;
    ppu->current_scanline = 0;
    ppu->mode             = PPU_MODE_OAM_SEARCH;
    ppu->frame_completed  = 0;

    /* initialize the PPU I/O registers */
    uint8_t stat          = mmu_read(ppu->mmu, STAT);
    stat                  = (stat & 0xFC) | (ppu->mode);  // clear the mode bits

    mmu_write(ppu->mmu, LY, 0);       // LY register is initialized to 0
    mmu_write(ppu->mmu, STAT, stat);  // write the initial state to STAT

    /* window properties */
    ppu->window_line_counter = 0;
    ppu->window_was_visible  = false;
}

/* internal helper functions */
static inline void request_interrupt(PPU *ppu, int interrupt_type) {
    if (interrupt_type == VBLANK_INTERRUPT) {
        ppu->cpu->ifr |= 0x01;  // set bit 0 of IF register
    } else if (interrupt_type == LCD_INTERRUPT) {
        ppu->cpu->ifr |= 0x02;  // set bit 1 of IF register
    }
}

static void check_lyc_match(PPU *ppu) {
    uint8_t stat = mmu_read(ppu->mmu, STAT);
    uint8_t lyc  = mmu_read(ppu->mmu, LYC);

    if (ppu->current_scanline == lyc) {
        stat |= 0x04;  // set the LYC=LY flag
        if (stat & 0x40) {
            /* if the LYC=LY interrupt is enabled, request an interrupt */
            request_interrupt(ppu, LCD_INTERRUPT);
        }
    } else {
        stat &= ~0x04;  // clear the LYC=LY flag
    }
    mmu_write(ppu->mmu, STAT, stat);  // write the updated status back
}

static void change_mode(PPU *ppu, ppu_mode new_mode) {
    ppu->mode    = new_mode;
    uint8_t stat = mmu_read(ppu->mmu, STAT);
    stat &= 0xFC;      // clear the mode bits
    stat |= new_mode;  // set the new mode

    /* check for STAT interrupt triggers on mode change */
    int interrupt_requested = 0;
    if (new_mode == PPU_MODE_OAM_SEARCH && (stat & 0x20)) {
        interrupt_requested = 1;  // OAM search mode interrupt enabled
    } else if (new_mode == PPU_MODE_VBLANK && (stat & 0x10)) {
        interrupt_requested = 1;  // VBLANK mode interrupt enabled
    } else if (new_mode == PPU_MODE_HBLANK && (stat & 0x08)) {
        interrupt_requested = 1;  // HBLANK mode interrupt enabled
    }

    if (interrupt_requested) {
        request_interrupt(ppu, LCD_INTERRUPT);  // request an interrupt
    }

    mmu_write(ppu->mmu, STAT, stat);  // write the updated status back
}

static void render_background_in_scanline(PPU *ppu) {
    uint8_t lcdc = mmu_read(ppu->mmu, LCDC);
    if (!(lcdc & 0x01)) {  // bit 0: BG display enable. fill with white if disabled
        for (int x = 0; x < LCD_WIDTH; x++) {
            ppu->framebuffer[ppu->current_scanline][x] = COLOR_WHITE;  // white
        }
        return;
    }

    uint8_t scx                     = mmu_read(ppu->mmu, SCX);
    uint8_t scy                     = mmu_read(ppu->mmu, SCY);
    uint8_t bgp                     = mmu_read(ppu->mmu, BGP);

    /* get general info about the tiles */
    uint16_t tile_map_base_address  = (lcdc & 0x08) ? 0x9C00 : 0x9800;
    uint16_t tile_data_base_address = (lcdc & 0x10) ? 0x8000 : 0x9000;
    int signed_tile_data            = (lcdc & 0x10) ? 0 : 1;  // signed or unsigned tile data

    uint8_t y = (ppu->current_scanline + scy) & 0xFF;
    uint16_t tile_map_row = tile_map_base_address + (y >> 3) * 32;
    int tile_row_offset = (y & 7) * 2;
    uint8_t *pixels = ppu->framebuffer[ppu->current_scanline];

    for (int x = 0; x < LCD_WIDTH;) {
        uint8_t xw                = (x + scx) & 0xFF;
        int pixel_x = xw & 7;
        int span = 8 - pixel_x;
        if (span > LCD_WIDTH - x) {
            span = LCD_WIDTH - x;
        }
        uint8_t tile_index = mmu_read(ppu->mmu, tile_map_row + (xw >> 3));
        uint16_t tile_data_address =
            tile_data_base_address + (signed_tile_data ? (int8_t)tile_index : tile_index) * 16 +
            tile_row_offset;
        uint8_t low_byte  = mmu_read(ppu->mmu, tile_data_address);
        uint8_t high_byte = mmu_read(ppu->mmu, tile_data_address + 1);

        for (int end = x + span; x < end; x++, pixel_x++) {
            uint8_t color_index =
                ((low_byte >> (7 - pixel_x)) & 0x01) | (((high_byte >> (7 - pixel_x)) & 0x01) << 1);
            pixels[x] = (bgp >> (color_index * 2)) & 0x03;
        }
    }
}

static void render_window_in_scanline(PPU *ppu) {
    /* check if window is enabled using LCDC (bit 5) */
    uint8_t lcdc = mmu_read(ppu->mmu, LCDC);
    if (!(lcdc & 0x20)) {  // bit 5: window display enable
        return;            // if window is disabled, do nothing
    }

    /* get window position from WX and WY registers */
    uint8_t wx = mmu_read(ppu->mmu, WX);
    uint8_t wy = mmu_read(ppu->mmu, WY);

    /* check if the current scanline is within the window y position */
    if (ppu->current_scanline < wy) {
        return;  // if the current scanline is above the window, do nothing
    }

    /* calculate the window x coordinate on screen */
    int start_x = wx - 7;  // WX has a 7-pixel offset (hardware quirk)
    if (start_x >= LCD_WIDTH) {
        return;  // if the window x position is outside the screen, do nothing
    }

    /* if this is the first time the window is visible in this frame */
    if (!ppu->window_was_visible && ppu->current_scanline == wy) {
        ppu->window_line_counter = 0;
        ppu->window_was_visible  = true;
    }

    /* calculate the window y coordinate (relative to window start) */
    uint8_t window_y                = ppu->window_line_counter;

    /* get window tile map and tile data info (similar to background) */
    uint8_t bgp                     = mmu_read(ppu->mmu, BGP);
    uint16_t tile_map_base_address  = (lcdc & 0x40) ? 0x9C00 : 0x9800;  // window uses bit 6
    uint16_t tile_data_base_address = (lcdc & 0x10) ? 0x8000 : 0x9000;
    int signed_tile_data            = (lcdc & 0x10) ? 0 : 1;  // signed or unsigned tile data

    uint16_t tile_map_row = tile_map_base_address + (window_y >> 3) * 32;
    int tile_row_offset = (window_y & 7) * 2;
    uint8_t *pixels = ppu->framebuffer[ppu->current_scanline];

    for (int lcd_x = (start_x > 0) ? start_x : 0; lcd_x < LCD_WIDTH;) {
        uint8_t window_x          = lcd_x - start_x;  // relative x position in the window
        int pixel_x = window_x & 7;
        int span = 8 - pixel_x;
        if (span > LCD_WIDTH - lcd_x) {
            span = LCD_WIDTH - lcd_x;
        }
        uint8_t tile_index = mmu_read(ppu->mmu, tile_map_row + (window_x >> 3));
        uint16_t tile_data_address =
            tile_data_base_address + (signed_tile_data ? (int8_t)tile_index : tile_index) * 16 +
            tile_row_offset;
        uint8_t low_byte  = mmu_read(ppu->mmu, tile_data_address);
        uint8_t high_byte = mmu_read(ppu->mmu, tile_data_address + 1);

        for (int end = lcd_x + span; lcd_x < end; lcd_x++, pixel_x++) {
            uint8_t color_index =
                ((low_byte >> (7 - pixel_x)) & 0x01) | (((high_byte >> (7 - pixel_x)) & 0x01) << 1);
            pixels[lcd_x] = (bgp >> (color_index * 2)) & 0x03;
        }
    }

    ppu->window_line_counter++;
}

static void scan_oam(PPU *ppu) {
    /* check if sprites are enabled using LCDC (bit 1) */
    uint8_t lcdc = mmu_read(ppu->mmu, LCDC);
    if (!(lcdc & 0x02)) {               // bit 1: sprite display enable
        ppu->num_scanline_sprites = 0;  // clear the count
        return;                         // if sprites are disabled, do nothing
    }

    /* get sprite height from LCDC */
    int sprite_height         = (lcdc & 0x04) ? 16 : 8;  // bit 2: sprite size (8x8 or 8x16)

    ppu->num_scanline_sprites = 0;  // reset the sprite count for this scanline

    /* scan all 40 sprites in OAM */
    for (int i = 0; i < 40 && ppu->num_scanline_sprites < MAX_SPRITES_PER_SCANLINE; i++) {
        /* read the sprite attributes from OAM */
        uint16_t oam_address = OAM_START + i * 4;
        uint8_t y            = mmu_read(ppu->mmu, oam_address);      // sprite y coordinate
        uint8_t x            = mmu_read(ppu->mmu, oam_address + 1);  // sprite x coordinate
        uint8_t tile         = mmu_read(ppu->mmu, oam_address + 2);  // tile index
        uint8_t attributes   = mmu_read(ppu->mmu, oam_address + 3);  // attributes

        if (y == 0 || y >= 160) {
            continue;
        }

        int sprite_top    = y - 16;
        int sprite_bottom = sprite_top + sprite_height;

        if (ppu->current_scanline >= sprite_top && ppu->current_scanline < sprite_bottom) {
            /* this means the sprite is within the scanline, so let's add it to our list! */
            ppu->scanline_sprites[ppu->num_scanline_sprites].y          = y;
            ppu->scanline_sprites[ppu->num_scanline_sprites].x          = x;
            ppu->scanline_sprites[ppu->num_scanline_sprites].tile       = tile;
            ppu->scanline_sprites[ppu->num_scanline_sprites].attributes = attributes;
            ppu->scanline_sprites[ppu->num_scanline_sprites].oam_index  = i;
            ppu->num_scanline_sprites++;
        }
    }

    /* sort the sprite data by priority: first by x coords (asc), then by OAM index */
    for (int i = 0; i < ppu->num_scanline_sprites - 1; i++) {
        for (int j = i + 1; j < ppu->num_scanline_sprites; j++) {
            if (ppu->scanline_sprites[i].x > ppu->scanline_sprites[j].x ||
                (ppu->scanline_sprites[i].x == ppu->scanline_sprites[j].x &&
                 ppu->scanline_sprites[i].oam_index > ppu->scanline_sprites[j].oam_index)) {
                // swap sprites
                sprite_t temp            = ppu->scanline_sprites[i];
                ppu->scanline_sprites[i] = ppu->scanline_sprites[j];
                ppu->scanline_sprites[j] = temp;
            }
        }
    }
}

static void render_sprites_in_scanline(PPU *ppu) {
    /* check if sprites are enabled using LCDC (bit 1) */
    uint8_t lcdc = mmu_read(ppu->mmu, LCDC);
    if (!(lcdc & 0x02)) {  // bit 1: sprite display enable
        return;            // if sprites are disabled, do nothing
    }

    /* get sprite height */
    int sprite_height = (lcdc & 0x04) ? 16 : 8;  // bit 2: sprite size (8x8 or 8x16)

    /* get sprite palettes */
    uint8_t obp0      = mmu_read(ppu->mmu, OBP0);  // sprite palette 0
    uint8_t obp1      = mmu_read(ppu->mmu, OBP1);  // sprite palette 1

    /* render sprites in reverse order (priority queue is FIFO) */
    for (int i = ppu->num_scanline_sprites - 1; i >= 0; i--) {
        sprite_t *sprite = &ppu->scanline_sprites[i];

        int sprite_x     = sprite->x - 8;   // sprite x coordinate (8-pixel offset)
        int sprite_y     = sprite->y - 16;  // sprite y coordinate (16-pixel offset)

        if (sprite_x >= LCD_WIDTH || sprite_x <= -8) {
            continue;  // if sprite is outside the screen, skip it
        }

        int sprite_row = ppu->current_scanline - sprite_y;  // row in the sprite to render

        if (sprite_row < 0 || sprite_row >= sprite_height) {
            continue;  // skip invalid sprite rows
        }

        if (sprite->attributes & 0x40) {                  // bit 6: y flip
            sprite_row = sprite_height - 1 - sprite_row;  // flip the row if y flip is set
        }

        /* get the tile data address */
        uint8_t tile_index = sprite->tile;
        if (sprite_height == 16) {
            tile_index &= 0xFE;  // for 8x16 sprites, use even tile index
            if (sprite_row >= 8) {
                tile_index++;     // if sprite row is in the second half, use the next tile
                sprite_row -= 8;  // adjust the row for the second tile
            }
        }

        if (tile_index > 255 || sprite_row > 7)
            continue;

        uint16_t tile_data_address = 0x8000 + tile_index * 16 + sprite_row * 2;

        if (tile_data_address >= 0xA000) {
            continue;  // skip invalid tile access
        }

        uint8_t low_byte  = mmu_read(ppu->mmu, tile_data_address);
        uint8_t high_byte = mmu_read(ppu->mmu, tile_data_address + 1);

        /* render each pixel in the sprite row */
        for (int pixel_x = 0; pixel_x < 8; pixel_x++) {
            int screen_x = sprite_x + pixel_x;  // screen x coordinate

            if (screen_x < 0 || screen_x >= LCD_WIDTH) {
                continue;  // skip pixels outside the screen
            }

            int bit_index = (sprite->attributes & 0x20) ? pixel_x : (7 - pixel_x);

            uint8_t color_index =
                ((low_byte >> bit_index) & 0x01) | (((high_byte >> bit_index) & 0x01) << 1);

            if (color_index == 0) {
                continue;  // skip transparent pixels
            }

            /* check sprite vs background prio */
            if (sprite->attributes & 0x80) {  // bit 7: sprite priority
                uint8_t bg_color = ppu->framebuffer[ppu->current_scanline][screen_x];
                if (bg_color != 0) {
                    continue;  // if background pixel is not white (0), skip this sprite pixel
                }
            }

            uint8_t palette = (sprite->attributes & 0x10) ? obp1 : obp0;  // bit 4: sprite palette

            /* set the color in the framebuffer */
            ppu->framebuffer[ppu->current_scanline][screen_x] =
                (palette >> (color_index * 2)) & 0x03;  // get the color from the palette
        }
    }
}

/* function to update the PPU state */
void ppu_step(PPU *ppu, int cycles) {
    uint8_t lcdc = mmu_read(ppu->mmu, LCDC);

    // if LCD is disabled
    if (!(lcdc & 0x80)) {
        if (ppu->current_scanline != 0 || ppu->mode != PPU_MODE_VBLANK) {
            // when LCD is disabled, LY is reset to 0 and PPU enters VBLANK-like state
            ppu->current_scanline = 0;
            ppu->scanline_cycles  = 0;
            mmu_write(ppu->mmu, LY, 0);
            ppu->mode = PPU_MODE_HBLANK;
        }
        return;  // if LCD is off, PPU is mostly idle
    }

    // if LCD is enabled
    ppu->scanline_cycles += cycles;

    switch (ppu->mode) {
        case PPU_MODE_OAM_SEARCH:  // mode 2
            if (ppu->scanline_cycles >= CYCLES_OAM_SCAN) {
                ppu->scanline_cycles -= CYCLES_OAM_SCAN;
                scan_oam(ppu);
                change_mode(ppu, PPU_MODE_DRAWING);
            }
            break;

        case PPU_MODE_DRAWING:  // mode 3
            // drawing cycles are actually variable, and should be calculated based on sprite data
            // TODO: change PPU to use variable drawing cycles instead of an average number
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

        case PPU_MODE_HBLANK:  // mode 0
            // uses remaining cycles
            if (ppu->scanline_cycles >=
                (CYCLES_PER_SCANLINE - CYCLES_OAM_SCAN - CYCLES_DRAWING_AVG)) {
                ppu->scanline_cycles -=
                    (CYCLES_PER_SCANLINE - CYCLES_OAM_SCAN - CYCLES_DRAWING_AVG);

                ppu->current_scanline++;
                mmu_write(ppu->mmu, LY, ppu->current_scanline);
                check_lyc_match(ppu);  // check if LYC matches LY

                if (ppu->current_scanline == LCD_HEIGHT) {
                    // if we reach the end of the visible scanlines, enter VBLANK
                    change_mode(ppu, PPU_MODE_VBLANK);
                    request_interrupt(ppu, VBLANK_INTERRUPT);  // request VBLANK interrupt
                    ppu->frame_completed = 1;                  // mark the scanline as completed
                } else {
                    change_mode(ppu, PPU_MODE_OAM_SEARCH);  // go to OAM search mode
                }
            }
            break;

        case PPU_MODE_VBLANK:  // mode 1
            // VBLANK lasts for 10 scanlines, each taking a full scanline time
            if (ppu->scanline_cycles >= CYCLES_VBLANK_SCANLINE) {
                ppu->scanline_cycles -= CYCLES_VBLANK_SCANLINE;
                ppu->current_scanline++;
                mmu_write(ppu->mmu, LY, ppu->current_scanline);
                check_lyc_match(ppu);  // check if LYC matches LY

                if (ppu->current_scanline >= SCANLINES_PER_FRAME) {
                    // if we reach the end of the VBLANK period, reset to the first scanline
                    ppu->current_scanline = 0;
                    mmu_write(ppu->mmu, LY, 0);
                    check_lyc_match(ppu);
                    ppu->window_line_counter = 0;      // reset window line counter
                    ppu->window_was_visible  = false;  // reset window visibility

                    change_mode(ppu, PPU_MODE_OAM_SEARCH);  // go to OAM search mode
                }
            }
            break;
    }
}

/* function to get the current framebuffer data */
const uint8_t (*ppu_get_framebuffer(PPU *ppu))[LCD_WIDTH] {
    return (const uint8_t (*)[LCD_WIDTH])ppu->framebuffer;
}

/* dma transfer function (used in mmu!) */
void ppu_dma_transfer(PPU *ppu, uint8_t value) {
    ppu->cpu->dma_flag      = 1;  // set the DMA flag to indicate DMA transfer is in progress
    uint16_t source_address = value << 8;  // DMA transfer address (0xFF46)
    for (int i = 0; i < 0xA0; i++) {       // transfer 160 bytes (0xA0)
        uint8_t data = mmu_read(ppu->mmu, source_address + i);
        mmu_write(ppu->mmu, OAM_START + i, data);  // write to OAM
        tick(ppu->cpu, 4);                         // each byte transfer takes 4 cycles
    }
    ppu->cpu->dma_flag = 0;  // clear the DMA flag after transfer is complete
}
