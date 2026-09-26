#ifndef CGB_HEADER
#define CGB_HEADER

#include <stdbool.h>
#include <stdint.h>

struct MMU;

bool cgb_vram_blocked(const struct MMU *mmu);
uint8_t *cgb_wram_address(struct MMU *mmu, uint16_t addr);
bool cgb_read_register(struct MMU *mmu, uint16_t addr, uint8_t *value);
bool cgb_write_register(struct MMU *mmu, uint16_t addr, uint8_t value);

#endif
