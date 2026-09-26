#include <assert.h>
#include <stdio.h>

#include "rom.h"
#include "mmu.h"
#include "../cpu/cpu.h"

static void check_model(uint8_t flag, rom_model model, bool accepted, bool cgb) {
    MMU mmu = {0};
    CPU cpu;
    mmu.rom[0x143] = flag;
    assert(rom_select_model(&mmu, model) == accepted);
    if (accepted) {
        assert(mmu.cgb_mode == cgb);
        cpu_init(&cpu, &mmu, NULL, NULL, NULL);
        assert(cpu.a == (cgb ? 0x11 : 0x01));
        assert(cpu.sp == 0xFFFE);
    }
}

int main(void) {
    check_model(0x00, ROM_MODEL_AUTO, true, false);
    check_model(0x80, ROM_MODEL_AUTO, true, true);
    check_model(0xC0, ROM_MODEL_AUTO, true, true);
    check_model(0x80, ROM_MODEL_DMG, true, false);
    check_model(0xC0, ROM_MODEL_DMG, false, false);
    check_model(0x00, ROM_MODEL_CGB, true, true);
    check_model(0x81, ROM_MODEL_AUTO, true, false);
    puts("Cartridge model selection and post-boot register checks passed.");
    return 0;
}
