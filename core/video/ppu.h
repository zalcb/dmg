#ifndef PPU_HEADER
#define PPU_HEADER

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define LCD_HEIGHT 144
#define LCD_WIDTH 160

/* display colors */
#define COLOR_WHITE 0
#define COLOR_LIGHT_GRAY 1
#define COLOR_DARK_GRAY 2
#define COLOR_BLACK 3

/* I/O registers addresses */
#define LCDC 0xFF40  // LCD control
#define STAT 0xFF41  // LCD status
#define SCY 0xFF42   // scroll Y
#define SCX 0xFF43   // scroll X
#define LY 0xFF44    // current scanline
#define LYC 0xFF45   // LY compare
#define BGP 0xFF47   // background palette
#define OBP0 0xFF48  // sprite palette 0
#define OBP1 0xFF49  // sprite palette 1
#define WY 0xFF4A    // window Y position
#define WX 0xFF4B    // window X position

/* OAM */
#define OAM_START 0xFE00  // start of OAM (object attribute memory)
#define OAM_SIZE 0xA0     // size of OAM (160 bytes, 40 sprites w/ 4 bytes each)
#define MAX_SPRITES_PER_SCANLINE \
    10  // 10 sprites can be displayed per scanline, a hardware quirk

/* CPU and MMU forward declarations */
struct CPU;
struct MMU;

typedef enum {
    PPU_MODE_HBLANK = 0,
    PPU_MODE_VBLANK = 1,
    PPU_MODE_OAM_SEARCH = 2,
    PPU_MODE_DRAWING = 3,
} ppu_mode;

typedef struct {
    uint8_t y;           // y coordinate (sprite y + 16)
    uint8_t x;           // x coordinate (sprite x + 8)
    uint8_t tile;        // tile index (0-255)
    uint8_t attributes;  // attributes (palette, x flip, y flip, priority)
    uint8_t oam_index;   // index in OAM, for priority sorting
} sprite_t;

typedef struct PPU {
    struct MMU *mmu;
    struct CPU *cpu;

    /* PPU timing and states */
    int scanline_cycles;       // how many cycles in the current scanline
    uint8_t current_scanline;  // current scanline (LY register, 0-153)
    ppu_mode mode;             // current PPU mode

    /* window rendering fields */
    uint8_t window_line_counter;  // current line in the window (0-143)
    bool window_was_visible;      // flag to indicate if the window was visible

    /* OAM fields */
    sprite_t scanline_sprites[MAX_SPRITES_PER_SCANLINE];  // sprites in the
                                                          // current scanline
    int num_scanline_sprites;  // number of sprites in the current scanline

    /* framebuffer */
    uint8_t framebuffer[LCD_HEIGHT]
                       [LCD_WIDTH];  // framebuffer for the LCD
                                     // each pixel is a 0-3 shade of gray

    int frame_completed;  // flag to indicate if the frame (all scanlines) is
                          // completed

} PPU;

void ppu_init(PPU *ppu, struct MMU *mmu, struct CPU *cpu);
void ppu_reset(PPU *ppu);
void ppu_step(PPU *ppu, int cycles);

/* helper to get current framebuffer data and pass it to the main game loop */
const uint8_t (*ppu_get_framebuffer(PPU *ppu))[LCD_WIDTH];

/* function to handle DMA transfer */
void ppu_dma_transfer(PPU *ppu, uint8_t value);

#endif
