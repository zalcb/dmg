#include <assert.h>
#include <math.h>

#include "core_machine.h"

static void test_cpu(void) {
    CoreMachine m;
    core_init(&m);
    const uint8_t program[] = {
        0x3E, 0x0F, 0xC6, 0x01, 0xD6, 0x10, 0x21, 0x00, 0xC0,
        0x36, 0x81, 0xCB, 0x06, 0xCD, 0x20, 0x01
    };
    memcpy(m.mmu.rom + 0x100, program, sizeof(program));
    m.mmu.rom[0x120] = 0xC9;
    cpu_step(&m.cpu);
    assert(m.cpu.a == 15 && m.cpu.pc == 0x102 && m.cpu.cycles == 8);
    cpu_step(&m.cpu);
    assert(m.cpu.a == 16 && m.cpu.f == 0x20 && m.cpu.cycles == 16);
    cpu_step(&m.cpu);
    assert(m.cpu.a == 0 && m.cpu.f == 0xC0);
    cpu_step(&m.cpu);
    cpu_step(&m.cpu);
    assert(mmu_read(&m.mmu, 0xC000) == 0x81);
    cpu_step(&m.cpu);
    assert(mmu_read(&m.mmu, 0xC000) == 3 && m.cpu.f == 0x10);
    cpu_step(&m.cpu);
    assert(m.cpu.pc == 0x120 && m.cpu.sp == 0xFFFC);
    assert(mmu_read16(&m.mmu, m.cpu.sp) == 0x110);
    cpu_step(&m.cpu);
    assert(m.cpu.pc == 0x110 && m.cpu.sp == 0xFFFE && m.cpu.cycles == 104);
    m.cpu.ime = 1;
    m.cpu.halt = 1;
    m.cpu.ier = m.cpu.ifr = 1;
    cpu_step(&m.cpu);
    assert(m.cpu.pc == 0x40 && !m.cpu.ime && !(m.cpu.ifr & 1));
    assert(mmu_read16(&m.mmu, m.cpu.sp) == 0x110);
    core_cleanup(&m);
}

static void test_mmu(void) {
    CoreMachine m;
    core_init(&m);
    mmu_write(&m.mmu, 0xC123, 0x42);
    assert(mmu_read(&m.mmu, 0xE123) == 0x42);
    mmu_write(&m.mmu, 0xE124, 0x73);
    assert(mmu_read(&m.mmu, 0xC124) == 0x73);
    mmu_write16(&m.mmu, 0xFF80, 0x1234);
    assert(mmu_read(&m.mmu, 0xFF80) == 0x34 && mmu_read16(&m.mmu, 0xFF80) == 0x1234);
    mmu_write(&m.mmu, 0xFEA0, 0);
    assert(mmu_read(&m.mmu, 0xFEA0) == 0xFF);
    mmu_write(&m.mmu, IE, 0x15);
    mmu_write(&m.mmu, IF, 0x04);
    assert(m.cpu.ier == 0x15 && mmu_read(&m.mmu, IF) == 0xE4);
    m.mmu.boot_rom_enabled = true;
    m.mmu.boot_rom[0] = 0xAB;
    m.mmu.rom[0] = 0xCD;
    assert(mmu_read(&m.mmu, 0) == 0xAB);
    mmu_write(&m.mmu, BOOT, 1);
    assert(mmu_read(&m.mmu, 0) == 0xCD);
    core_cleanup(&m);
}

static void test_apu(void) {
    CoreMachine m;
    core_init(&m);
    core_synthetic(&m);
    assert(m.apu.sound_enabled && m.apu.ch1.enabled && m.apu.ch2.enabled);
    assert(m.apu.ch3.enabled && m.apu.ch4.enabled);
    assert(m.apu.ch1.frequency == 0x340 && m.apu.channel_panning == 0xFF);
    for (int i = 0; i < 1024; ++i) apu_step(&m.apu, 4);
    float audio[64];
    apu_get_samples(&m.apu, audio, 32);
    bool nonzero = false;
    for (unsigned i = 0; i < sizeof(audio) / sizeof(audio[0]); ++i) {
        assert(isfinite(audio[i]));
        nonzero |= audio[i] != 0;
    }
    assert(nonzero && m.apu.cycles == 4096);
    mmu_write(&m.mmu, NR12, 0);
    assert(!m.apu.ch1.dac_enabled && !m.apu.ch1.enabled);
    core_cleanup(&m);
}

int main(void) {
    assert(cpu_log == NULL);
    test_cpu();
    test_mmu();
    test_apu();
    puts("core tests passed");
    return 0;
}
