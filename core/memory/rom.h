#ifndef ROM_HEADER
#define ROM_HEADER

#include <stdbool.h>
#include <stdint.h>

struct MMU;

typedef enum rom_model {
    ROM_MODEL_DMG,
    ROM_MODEL_CGB,
    ROM_MODEL_AUTO,
} rom_model;

bool rom_select_model(struct MMU *mmu, rom_model model);
void log_header(struct MMU *mmu);
void load_boot_rom(struct MMU *mmu, const char *filepath);
void load_rom(struct MMU *mmu, const char *filepath);

#endif
