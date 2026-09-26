#ifndef ROM_HEADER
#define ROM_HEADER

#include <stdint.h>

struct MMU;

void log_header(struct MMU *mmu);
void load_boot_rom(struct MMU *mmu, const char *filepath);
void load_rom(struct MMU *mmu, const char *filepath);

#endif
