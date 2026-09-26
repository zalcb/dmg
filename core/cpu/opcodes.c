
#include <stdbool.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#include "cpu.h"
#include "../memory/mmu.h"

// flags mask
#define FLAG_Z                                               \
    0x80 /* zero flag: if the result of an operation is zero \
            the mask 0x80 is 1000 0000 in binary */
#define FLAG_N                                                     \
    0x40 /* subtract flag: if the last operation was a subtraction \
            the mask 0x40 is 0100 0000 in binary */
#define FLAG_H                                                    \
    0x20 /* half carry flag: indicates carry for the lower 4 bits \
    the mask 0x20 is 0010 0000 in binary */
#define FLAG_C                                                              \
    0x10 /* carry flag: when the result of an 8-bit addition is higher than \
    $FF the mask 0x10 is 0001 0000 in binary */

/* macros to avoid rewriting everything after mmu refactor. maybe one day i'll
 * replace everything */
#undef mem_read
#undef mem_read16
#undef mem_write
#undef mem_write16

#define mem_read(addr) mmu_read((cpu)->mmu, (addr))
#define mem_read16(addr) mmu_read16((cpu)->mmu, (addr))
#define mem_write(addr, v) mmu_write((cpu)->mmu, (addr), (v))
#define mem_write16(addr, v) mmu_write16((cpu)->mmu, (addr), (v))

// decode and execute the opcode (switch statement)
#include "opcodes.h"

// helper to advance the program counter
static inline void advance_pc(CPU *cpu, uint8_t n) { cpu->pc += n; }

// helper to get the high byte of a 16-bit value
static inline uint8_t high_byte(uint16_t val) { return (val >> 8) & 0xFF; }

// helper to get the low byte of a 16-bit value
static inline uint8_t low_byte(uint16_t val) { return val & 0xFF; }

/* helper to get a flag */
static inline int get_flag(CPU *cpu, uint8_t flag) { return (cpu->f & flag) != 0; }

/* helper to set a flag */
static inline void set_flag(CPU *cpu, uint8_t flag, int enable) {
    if (enable) {
        cpu->f |= flag; /* if enable = 1, turn the flag bit to 1 */
    } else {
        cpu->f &= ~flag; /* if enable = 0, turn the flag bit to 0 */
    }
}

static inline void add_i8_to_u16(uint16_t sp, int8_t off, uint16_t *out, CPU *cpu) {
    uint16_t res = sp + off;
    uint16_t tmp = sp ^ off ^ res;      /* XOR catches carries/borrows */
    set_flag(cpu, FLAG_H, tmp & 0x10);  /* carry from bit 3 */
    set_flag(cpu, FLAG_C, tmp & 0x100); /* carry from bit 7 */
    *out = res;
}

/*  -----  MACROS ---------------------------------------------------------- */

/* other helpers -------------------------------- */
#define ADV_PC(cpu, n) advance_pc((cpu), (n))
#define ADV_CYCLES(cpu, n) tick((cpu), (n))

static void op_stop(CPU *cpu) {
    cpu->pc++;
    tick(cpu, 4);
    if (cpu->mmu->cgb_mode && (mmu_read(cpu->mmu, KEY1) & 1)) {
        cpu->mmu->double_speed = !cpu->mmu->double_speed;
        cpu->speed_cycle_remainder = 0;
        mmu_write(cpu->mmu, KEY1, 0);
        mmu_write(cpu->mmu, DIV, 0);
    } else {
        cpu->halt = 1;
    }
}

static inline void call_u16(CPU *cpu) {
    /* 1. fetch the target address (little‑endian) */
    uint16_t target = mem_read16(cpu->pc);

    /* 2. compute address to return to and skip operand */
    uint16_t ret_addr = cpu->pc + 2;
    advance_pc(cpu, 2);

    /* 3. push ret_addr (high byte first, then low byte) */
    cpu->sp--;
    mem_write(cpu->sp, high_byte(ret_addr)); /* high */
    cpu->sp--;
    mem_write(cpu->sp, low_byte(ret_addr)); /* low  */

    /* 4. jump */
    cpu->pc = target;

    /* 5. timing */
    ADV_CYCLES(cpu, 24);
}

static inline void ret(CPU *cpu) {
    /* 1. pop the return address (low byte first, then high byte) */
    uint8_t low = mem_read(cpu->sp++);
    uint8_t high = mem_read(cpu->sp++);
    cpu->pc = (high << 8) | low;
}

#define CALL_U16(cpu) call_u16(cpu)
#define RET(cpu) ret(cpu)

/*  x8/alu  ---------------------------------------------------------------- */
#define DEF_INC_R8(OP, REG)                                                   \
    static void op_##OP##_i_##REG(CPU *cpu) {                                 \
        /* 1. r8 <- r8 + 1 */                                                 \
        uint8_t old = (cpu)->REG;                                             \
        (cpu)->REG += 1;                                                      \
        /* 2. set flags */                                                    \
        set_flag(cpu, FLAG_Z, (cpu)->REG == 0);                               \
        set_flag(cpu, FLAG_N, 0);                                             \
        set_flag(cpu, FLAG_H,                                                 \
                 (old & 0x0F) == 0x0F); /* was the low nibble of the original \
                                           value 1111? carry will occur */    \
        ADV_CYCLES(cpu, 4);                                                   \
    }

#define DEF_DEC_R8(OP, REG)                                                   \
    static void op_##OP##_d_##REG(CPU *cpu) {                                 \
        /* 1. r8 <- r8 - 1 */                                                 \
        uint8_t old = (cpu)->REG;                                             \
        (cpu)->REG -= 1;                                                      \
        /* 2. set flags */                                                    \
        set_flag(cpu, FLAG_Z, (cpu)->REG == 0);                               \
        set_flag(cpu, FLAG_N, 1);                                             \
        set_flag(cpu, FLAG_H,                                                 \
                 (old & 0x0F) == 0x00); /* was the low nibble of the original \
                              value 0000? borrow will occur */                \
        ADV_CYCLES(cpu, 4);                                                   \
    }

#define DEF_INC_HLPTR(OP)                                                    \
    static void op_##OP##_i_hlptr(CPU *cpu) {                                \
        /* 1. (hl) <- (hl) + 1 */                                            \
        uint8_t old_val = mem_read((cpu)->hl);                               \
        uint8_t new_val = old_val + 1;                                       \
        mem_write((cpu)->hl, new_val);                                       \
        /* 2. set flags */                                                   \
        set_flag(cpu, FLAG_Z, new_val == 0);                                 \
        set_flag(cpu, FLAG_N, 0);                                            \
        set_flag(                                                            \
            cpu, FLAG_H,                                                     \
            (old_val & 0x0F) == 0x0F); /* was the low nibble of the original \
                                      value 1111? carry will occur */        \
        ADV_CYCLES(cpu, 12);                                                 \
    }

#define DEF_DEC_HLPTR(OP)                                                \
    static void op_##OP##_d_hlptr(CPU *cpu) {                            \
        /* 1. (hl) <- (hl) - 1 */                                        \
        uint8_t old_val = mem_read((cpu)->hl);                           \
        uint8_t new_val = old_val - 1;                                   \
        mem_write((cpu)->hl, new_val);                                   \
        /* 2. set flags */                                               \
        set_flag(cpu, FLAG_Z, new_val == 0);                             \
        set_flag(cpu, FLAG_N, 1);                                        \
        set_flag(cpu, FLAG_H,                                            \
                 (old_val & 0x0F) == 0x00); /* was the low nibble of the \
                              original value 0000? borrow will occur */  \
        ADV_CYCLES(cpu, 12);                                             \
    }

#define DEF_ADD_A_R8(OP, R8)                                                  \
    static void op_##OP##_add_a_##R8(CPU *cpu) {                              \
        /* 1. a <- a + r8 */                                                  \
        uint16_t result = (cpu)->a + (cpu)->R8;                               \
        /* 2. set flags */                                                    \
        set_flag(cpu, FLAG_Z, (result & 0xFF) == 0);                          \
        set_flag(cpu, FLAG_N, 0);                                             \
        set_flag(cpu, FLAG_H, ((cpu)->a & 0x0F) + ((cpu)->R8 & 0x0F) > 0x0F); \
        set_flag(cpu, FLAG_C, result > 0xFF);                                 \
        (cpu)->a =                                                            \
            result &                                                          \
            0x00FF; /* make sure we only store ONE byte, the lower one */     \
        ADV_CYCLES(cpu, 4);                                                   \
    }

#define DEF_ADD_A_HLPTR(OP)                                                   \
    static void op_##OP##_add_a_hlptr(CPU *cpu) {                             \
        /* 1. a <- a + (hl) */                                                \
        uint8_t byte_read = mem_read((cpu)->hl);                              \
        uint16_t result = (cpu)->a + byte_read;                               \
        /* 2. set flags */                                                    \
        set_flag(cpu, FLAG_Z, (result & 0xFF) == 0);                          \
        set_flag(cpu, FLAG_N, 0);                                             \
        set_flag(cpu, FLAG_H, ((cpu)->a & 0x0F) + (byte_read & 0x0F) > 0x0F); \
        set_flag(cpu, FLAG_C, result > 0xFF);                                 \
        (cpu)->a =                                                            \
            result &                                                          \
            0x00FF; /* make sure we only store ONE byte, the lower one */     \
        ADV_CYCLES(cpu, 8);                                                   \
    }

#define DEF_ADD_A_U8(OP)                                                      \
    static void op_##OP##_add_a_u8(CPU *cpu) {                                \
        /* 1. a <- a + u8 */                                                  \
        uint8_t byte_read = mem_read(cpu->pc);                                \
        uint16_t result = (cpu)->a + byte_read;                               \
        /* 2. set flags */                                                    \
        set_flag(cpu, FLAG_Z, (result & 0xFF) == 0);                          \
        set_flag(cpu, FLAG_N, 0);                                             \
        set_flag(cpu, FLAG_H, ((cpu)->a & 0x0F) + (byte_read & 0x0F) > 0x0F); \
        set_flag(cpu, FLAG_C, result > 0xFF);                                 \
        (cpu)->a =                                                            \
            result &                                                          \
            0x00FF; /* make sure we only store ONE byte, the lower one */     \
        ADV_PC(cpu, 1);                                                       \
        ADV_CYCLES(cpu, 8);                                                   \
    }

#define DEF_ADC_A_R8(OP, R8)                                                 \
    static void op_##OP##_adc_a_##R8(CPU *cpu) {                             \
        /* 1. a <- a + r8 + carry */                                         \
        uint16_t result = (cpu)->a + (cpu)->R8 + get_flag(cpu, FLAG_C);      \
        /* 2. set flags */                                                   \
        set_flag(cpu, FLAG_Z, (result & 0xFF) == 0);                         \
        set_flag(cpu, FLAG_N, 0);                                            \
        set_flag(                                                            \
            cpu, FLAG_H,                                                     \
            ((cpu)->a & 0x0F) + ((cpu)->R8 & 0x0F) + get_flag(cpu, FLAG_C) > \
                0x0F);                                                       \
        set_flag(cpu, FLAG_C, result > 0xFF);                                \
        (cpu)->a =                                                           \
            result &                                                         \
            0x00FF; /* make sure we only store ONE byte, the lower one */    \
        ADV_CYCLES(cpu, 4);                                                  \
    }

#define DEF_ADC_A_HLPTR(OP)                                                  \
    static void op_##OP##_adc_a_hlptr(CPU *cpu) {                            \
        /* 1. a <- a + (hl) + carry */                                       \
        uint8_t byte_read = mem_read((cpu)->hl);                             \
        uint16_t result = (cpu)->a + byte_read + get_flag(cpu, FLAG_C);      \
        /* 2. set flags */                                                   \
        set_flag(cpu, FLAG_Z, (result & 0xFF) == 0);                         \
        set_flag(cpu, FLAG_N, 0);                                            \
        set_flag(                                                            \
            cpu, FLAG_H,                                                     \
            ((cpu)->a & 0x0F) + (byte_read & 0x0F) + get_flag(cpu, FLAG_C) > \
                0x0F);                                                       \
        set_flag(cpu, FLAG_C, result > 0xFF);                                \
        (cpu)->a =                                                           \
            result &                                                         \
            0x00FF; /* make sure we only store ONE byte, the lower one */    \
        ADV_CYCLES(cpu, 8);                                                  \
    }

#define DEF_ADC_A_U8(OP)                                                     \
    static void op_##OP##_adc_a_u8(CPU *cpu) {                               \
        /* 1. a <- a + u8 + carry */                                         \
        uint8_t byte_read = mem_read(cpu->pc);                               \
        uint16_t result = (cpu)->a + byte_read + get_flag(cpu, FLAG_C);      \
        /* 2. set flags */                                                   \
        set_flag(cpu, FLAG_Z, (result & 0xFF) == 0);                         \
        set_flag(cpu, FLAG_N, 0);                                            \
        set_flag(                                                            \
            cpu, FLAG_H,                                                     \
            ((cpu)->a & 0x0F) + (byte_read & 0x0F) + get_flag(cpu, FLAG_C) > \
                0x0F);                                                       \
        set_flag(cpu, FLAG_C, result > 0xFF);                                \
        (cpu)->a =                                                           \
            result &                                                         \
            0x00FF; /* make sure we only store ONE byte, the lower one */    \
        ADV_PC(cpu, 1);                                                      \
        ADV_CYCLES(cpu, 8);                                                  \
    }

#define DEF_SUB_A_R8(OP, R8)                                              \
    static void op_##OP##_sub_a_##R8(CPU *cpu) {                          \
        /* 1. a <- a - r8 */                                              \
        uint16_t result = (cpu)->a - (cpu)->R8;                           \
        /* 2. set flags */                                                \
        set_flag(cpu, FLAG_Z, (result & 0xFF) == 0);                      \
        set_flag(cpu, FLAG_N, 1);                                         \
        set_flag(cpu, FLAG_H, ((cpu)->a & 0x0F) < ((cpu)->R8 & 0x0F));    \
        set_flag(cpu, FLAG_C, result > 0xFF);                             \
        (cpu)->a =                                                        \
            result &                                                      \
            0x00FF; /* make sure we only store ONE byte, the lower one */ \
        ADV_CYCLES(cpu, 4);                                               \
    }

#define DEF_SUB_A_HLPTR(OP)                                               \
    static void op_##OP##_sub_a_hlptr(CPU *cpu) {                         \
        /* 1. a <- a - (hl) */                                            \
        uint8_t byte_read = mem_read((cpu)->hl);                          \
        uint16_t result = (cpu)->a - byte_read;                           \
        /* 2. set flags */                                                \
        set_flag(cpu, FLAG_Z, (result & 0xFF) == 0);                      \
        set_flag(cpu, FLAG_N, 1);                                         \
        set_flag(cpu, FLAG_H, ((cpu)->a & 0x0F) < (byte_read & 0x0F));    \
        set_flag(cpu, FLAG_C, result > 0xFF);                             \
        (cpu)->a =                                                        \
            result &                                                      \
            0x00FF; /* make sure we only store ONE byte, the lower one */ \
        ADV_CYCLES(cpu, 8);                                               \
    }

#define DEF_SUB_A_U8(OP)                                                  \
    static void op_##OP##_sub_a_u8(CPU *cpu) {                            \
        /* 1. a <- a - u8 */                                              \
        uint8_t byte_read = mem_read(cpu->pc);                            \
        uint16_t result = (cpu)->a - byte_read;                           \
        /* 2. set flags */                                                \
        set_flag(cpu, FLAG_Z, (result & 0xFF) == 0);                      \
        set_flag(cpu, FLAG_N, 1);                                         \
        set_flag(cpu, FLAG_H, ((cpu)->a & 0x0F) < (byte_read & 0x0F));    \
        set_flag(cpu, FLAG_C, result > 0xFF);                             \
        (cpu)->a =                                                        \
            result &                                                      \
            0x00FF; /* make sure we only store ONE byte, the lower one */ \
        ADV_PC(cpu, 1);                                                   \
        ADV_CYCLES(cpu, 8);                                               \
    }

#define DEF_SBC_A_R8(OP, R8)                                                 \
    static void op_##OP##_sbc_a_##R8(CPU *cpu) {                             \
        /* 1. a <- a - r8 - carry */                                         \
        uint16_t result = (cpu)->a - (cpu)->R8 - get_flag(cpu, FLAG_C);      \
        /* 2. set flags */                                                   \
        set_flag(cpu, FLAG_Z, (result & 0xFF) == 0);                         \
        set_flag(cpu, FLAG_N, 1);                                            \
        set_flag(                                                            \
            cpu, FLAG_H,                                                     \
            ((cpu)->a & 0x0F) < ((cpu)->R8 & 0x0F) + get_flag(cpu, FLAG_C)); \
        set_flag(cpu, FLAG_C, result > 0xFF);                                \
        (cpu)->a =                                                           \
            result &                                                         \
            0x00FF; /* make sure we only store ONE byte, the lower one */    \
        ADV_CYCLES(cpu, 4);                                                  \
    }

#define DEF_SBC_A_HLPTR(OP)                                                  \
    static void op_##OP##_sbc_a_hlptr(CPU *cpu) {                            \
        /* 1. a <- a - (hl) - carry */                                       \
        uint8_t byte_read = mem_read((cpu)->hl);                             \
        uint16_t result = (cpu)->a - byte_read - get_flag(cpu, FLAG_C);      \
        /* 2. set flags */                                                   \
        set_flag(cpu, FLAG_Z, (result & 0xFF) == 0);                         \
        set_flag(cpu, FLAG_N, 1);                                            \
        set_flag(                                                            \
            cpu, FLAG_H,                                                     \
            ((cpu)->a & 0x0F) < (byte_read & 0x0F) + get_flag(cpu, FLAG_C)); \
        set_flag(cpu, FLAG_C, result > 0xFF);                                \
        (cpu)->a =                                                           \
            result &                                                         \
            0x00FF; /* make sure we only store ONE byte, the lower one */    \
        ADV_CYCLES(cpu, 8);                                                  \
    }

#define DEF_SBC_A_U8(OP)                                                     \
    static void op_##OP##_sbc_a_u8(CPU *cpu) {                               \
        /* 1. a <- a - u8 - carry */                                         \
        uint8_t byte_read = mem_read(cpu->pc);                               \
        uint16_t result = (cpu)->a - byte_read - get_flag(cpu, FLAG_C);      \
        /* 2. set flags */                                                   \
        set_flag(cpu, FLAG_Z, (result & 0xFF) == 0);                         \
        set_flag(cpu, FLAG_N, 1);                                            \
        set_flag(                                                            \
            cpu, FLAG_H,                                                     \
            ((cpu)->a & 0x0F) < (byte_read & 0x0F) + get_flag(cpu, FLAG_C)); \
        set_flag(cpu, FLAG_C, result > 0xFF);                                \
        (cpu)->a =                                                           \
            result &                                                         \
            0x00FF; /* make sure we only store ONE byte, the lower one */    \
        ADV_PC(cpu, 1);                                                      \
        ADV_CYCLES(cpu, 8);                                                  \
    }

#define DEF_AND_A_R8(OP, R8)                     \
    static void op_##OP##_and_a_##R8(CPU *cpu) { \
        /* 1. a <- a & r8 */                     \
        (cpu)->a &= (cpu)->R8;                   \
        /* 2. set flags */                       \
        set_flag(cpu, FLAG_Z, (cpu)->a == 0);    \
        set_flag(cpu, FLAG_N, 0);                \
        set_flag(cpu, FLAG_H, 1);                \
        set_flag(cpu, FLAG_C, 0);                \
        ADV_CYCLES(cpu, 4);                      \
    }

#define DEF_AND_A_HLPTR(OP)                       \
    static void op_##OP##_and_a_hlptr(CPU *cpu) { \
        /* 1. a <- a & (hl) */                    \
        (cpu)->a &= mem_read((cpu)->hl);          \
        /* 2. set flags */                        \
        set_flag(cpu, FLAG_Z, (cpu)->a == 0);     \
        set_flag(cpu, FLAG_N, 0);                 \
        set_flag(cpu, FLAG_H, 1);                 \
        set_flag(cpu, FLAG_C, 0);                 \
        ADV_CYCLES(cpu, 8);                       \
    }

#define DEF_AND_A_U8(OP)                       \
    static void op_##OP##_and_a_u8(CPU *cpu) { \
        /* 1. a <- a & u8 */                   \
        (cpu)->a &= mem_read(cpu->pc);         \
        /* 2. set flags */                     \
        set_flag(cpu, FLAG_Z, (cpu)->a == 0);  \
        set_flag(cpu, FLAG_N, 0);              \
        set_flag(cpu, FLAG_H, 1);              \
        set_flag(cpu, FLAG_C, 0);              \
        ADV_PC(cpu, 1);                        \
        ADV_CYCLES(cpu, 8);                    \
    }

#define DEF_XOR_A_R8(OP, R8)                     \
    static void op_##OP##_xor_a_##R8(CPU *cpu) { \
        /* 1. a <- a ^ r8 */                     \
        (cpu)->a ^= (cpu)->R8;                   \
        /* 2. set flags */                       \
        set_flag(cpu, FLAG_Z, (cpu)->a == 0);    \
        set_flag(cpu, FLAG_N, 0);                \
        set_flag(cpu, FLAG_H, 0);                \
        set_flag(cpu, FLAG_C, 0);                \
        ADV_CYCLES(cpu, 4);                      \
    }

#define DEF_XOR_A_HLPTR(OP)                       \
    static void op_##OP##_xor_a_hlptr(CPU *cpu) { \
        /* 1. a <- a ^ (hl) */                    \
        (cpu)->a ^= mem_read((cpu)->hl);          \
        /* 2. set flags */                        \
        set_flag(cpu, FLAG_Z, (cpu)->a == 0);     \
        set_flag(cpu, FLAG_N, 0);                 \
        set_flag(cpu, FLAG_H, 0);                 \
        set_flag(cpu, FLAG_C, 0);                 \
        ADV_CYCLES(cpu, 8);                       \
    }

#define DEF_XOR_A_U8(OP)                       \
    static void op_##OP##_xor_a_u8(CPU *cpu) { \
        /* 1. a <- a ^ u8 */                   \
        (cpu)->a ^= mem_read(cpu->pc);         \
        /* 2. set flags */                     \
        set_flag(cpu, FLAG_Z, (cpu)->a == 0);  \
        set_flag(cpu, FLAG_N, 0);              \
        set_flag(cpu, FLAG_H, 0);              \
        set_flag(cpu, FLAG_C, 0);              \
        ADV_PC(cpu, 1);                        \
        ADV_CYCLES(cpu, 8);                    \
    }

#define DEF_OR_A_R8(OP, R8)                     \
    static void op_##OP##_or_a_##R8(CPU *cpu) { \
        /* 1. a <- a | r8 */                    \
        (cpu)->a |= (cpu)->R8;                  \
        /* 2. set flags */                      \
        set_flag(cpu, FLAG_Z, (cpu)->a == 0);   \
        set_flag(cpu, FLAG_N, 0);               \
        set_flag(cpu, FLAG_H, 0);               \
        set_flag(cpu, FLAG_C, 0);               \
        ADV_CYCLES(cpu, 4);                     \
    }

#define DEF_OR_A_HLPTR(OP)                       \
    static void op_##OP##_or_a_hlptr(CPU *cpu) { \
        /* 1. a <- a | (hl) */                   \
        (cpu)->a |= mem_read((cpu)->hl);         \
        /* 2. set flags */                       \
        set_flag(cpu, FLAG_Z, (cpu)->a == 0);    \
        set_flag(cpu, FLAG_N, 0);                \
        set_flag(cpu, FLAG_H, 0);                \
        set_flag(cpu, FLAG_C, 0);                \
        ADV_CYCLES(cpu, 8);                      \
    }

#define DEF_OR_A_U8(OP)                       \
    static void op_##OP##_or_a_u8(CPU *cpu) { \
        /* 1. a <- a | (pc) */                \
        (cpu)->a |= mem_read((cpu)->pc);      \
        /* 2. set flags */                    \
        set_flag(cpu, FLAG_Z, (cpu)->a == 0); \
        set_flag(cpu, FLAG_N, 0);             \
        set_flag(cpu, FLAG_H, 0);             \
        set_flag(cpu, FLAG_C, 0);             \
        ADV_PC(cpu, 1);                       \
        ADV_CYCLES(cpu, 8);                   \
    }

#define DEF_CP_A_R8(OP, R8)                                            \
    static void op_##OP##_cp_a_##R8(CPU *cpu) {                        \
        /* 1. a - r8, but discard the result */                        \
        uint16_t result = (cpu)->a - (cpu)->R8;                        \
        /* 2. set flags */                                             \
        set_flag(cpu, FLAG_Z, (result & 0xFF) == 0);                   \
        set_flag(cpu, FLAG_N, 1);                                      \
        set_flag(cpu, FLAG_H, ((cpu)->a & 0x0F) < ((cpu)->R8 & 0x0F)); \
        set_flag(cpu, FLAG_C, result > 0xFF);                          \
        ADV_CYCLES(cpu, 4);                                            \
    }

#define DEF_CP_A_HLPTR(OP)                                             \
    static void op_##OP##_cp_a_hlptr(CPU *cpu) {                       \
        /* 1. a - (hl), but discard the result */                      \
        uint8_t byte_read = mem_read((cpu)->hl);                       \
        uint16_t result = (cpu)->a - byte_read;                        \
        /* 2. set flags */                                             \
        set_flag(cpu, FLAG_Z, (result & 0xFF) == 0);                   \
        set_flag(cpu, FLAG_N, 1);                                      \
        set_flag(cpu, FLAG_H, ((cpu)->a & 0x0F) < (byte_read & 0x0F)); \
        set_flag(cpu, FLAG_C, result > 0xFF);                          \
        ADV_CYCLES(cpu, 8);                                            \
    }

#define DEF_CP_A_U8(OP)                                                \
    static void op_##OP##_cp_a_u8(CPU *cpu) {                          \
        /* 1. a - u8, but discard the result */                        \
        uint8_t byte_read = mem_read(cpu->pc);                         \
        uint16_t result = (cpu)->a - byte_read;                        \
        /* 2. set flags */                                             \
        set_flag(cpu, FLAG_Z, (result & 0xFF) == 0);                   \
        set_flag(cpu, FLAG_N, 1);                                      \
        set_flag(cpu, FLAG_H, ((cpu)->a & 0x0F) < (byte_read & 0x0F)); \
        set_flag(cpu, FLAG_C, result > 0xFF);                          \
        ADV_PC(cpu, 1);                                                \
        ADV_CYCLES(cpu, 8);                                            \
    }

#define DEF_DAA(OP)                                                       \
    static void op_##OP##_daa(CPU *cpu) {                                 \
        /* daa adjusts the accumulator after an ADD / ADC or SUB / SBC */ \
        uint8_t A = cpu->a;                                               \
        bool flag_n = get_flag(cpu, FLAG_N);                              \
        bool flag_h = get_flag(cpu, FLAG_H);                              \
        bool flag_c = get_flag(cpu, FLAG_C);                              \
        uint8_t adj = 0;                                                  \
        if (flag_n) { /* —— previous op was SUB / SBC —— */               \
            if (flag_h) adj |= 0x06;                                      \
            if (flag_c) adj |= 0x60;                                      \
            A -= adj;                                                     \
        } else { /* —— previous op was ADD / ADC —— */                    \
            if (flag_h || (A & 0x0F) > 0x09) adj |= 0x06;                 \
            if (flag_c || A > 0x99) adj |= 0x60, flag_c = true;           \
            A += adj;                                                     \
            set_flag(cpu, FLAG_C, flag_c);                                \
        }                                                                 \
        cpu->a = A;                                                       \
        set_flag(cpu, FLAG_Z, A == 0); /* Z reflects adjusted A */        \
        set_flag(cpu, FLAG_H, 0);      /* H always cleared */             \
        ADV_CYCLES(cpu, 4);                                               \
    }

#define DEF_CPL(OP)                       \
    static void op_##OP##_cpl(CPU *cpu) { \
        /* 1. a <- ~a */                  \
        (cpu)->a = ~(cpu)->a;             \
        /* 2. set flags */                \
        set_flag(cpu, FLAG_N, 1);         \
        set_flag(cpu, FLAG_H, 1);         \
        ADV_CYCLES(cpu, 4);               \
    }

#define DEF_SCF(OP)                       \
    static void op_##OP##_scf(CPU *cpu) { \
        /* 1. set carry flag */           \
        set_flag(cpu, FLAG_C, 1);         \
        set_flag(cpu, FLAG_N, 0);         \
        set_flag(cpu, FLAG_H, 0);         \
        ADV_CYCLES(cpu, 4);               \
    }

#define DEF_CCF(OP)                                    \
    static void op_##OP##_ccf(CPU *cpu) {              \
        /* 1. flip carry flag */                       \
        set_flag(cpu, FLAG_C, !get_flag(cpu, FLAG_C)); \
        set_flag(cpu, FLAG_N, 0);                      \
        set_flag(cpu, FLAG_H, 0);                      \
        ADV_CYCLES(cpu, 4);                            \
    }

/* x16/alu  ----------------------------------- */
#define DEF_INC_R16(OP, REG)                  \
    static void op_##OP##_i_##REG(CPU *cpu) { \
        /* r16 <- r16 + 1 */                  \
        (cpu)->REG += 1;                      \
        ADV_CYCLES(cpu, 8);                   \
    }

#define DEF_DEC_R16(OP, REG)                  \
    static void op_##OP##_d_##REG(CPU *cpu) { \
        /* r16 <- r16 - 1 */                  \
        (cpu)->REG -= 1;                      \
        ADV_CYCLES(cpu, 8);                   \
    }

#define DEF_ADD_HL_R16(OP, REG)                                          \
    static void op_##OP##_add_hl_##REG(CPU *cpu) {                       \
        /* 1. hl <- hl + r16 */                                          \
        uint32_t result = (cpu)->hl + (cpu)->REG;                        \
        /* 2. set flags */                                               \
        set_flag(cpu, FLAG_N, 0);                                        \
        set_flag(cpu, FLAG_H,                                            \
                 ((cpu)->hl & 0x0FFF) + ((cpu)->REG & 0x0FFF) > 0x0FFF); \
        set_flag(cpu, FLAG_C, result > 0xFFFF);                          \
        (cpu)->hl = result & 0xFFFF;                                     \
        ADV_CYCLES(cpu, 8);                                              \
    }

#define DEF_ADD_SP_I8(OP)                             \
    static void op_##OP##_add_sp_i8(CPU *cpu) {       \
        /* 1. sp <- sp + i8 */                        \
        int8_t offset = (int8_t)mem_read(cpu->pc);    \
        uint16_t result;                              \
        set_flag(cpu, FLAG_Z, 0);                     \
        set_flag(cpu, FLAG_N, 0);                     \
        add_i8_to_u16(cpu->sp, offset, &result, cpu); \
        cpu->sp = result;                             \
        ADV_PC(cpu, 1);                               \
        ADV_CYCLES(cpu, 16);                          \
    }

#define DEF_LD_HL_SP_I8(OP)                           \
    static void op_##OP##_ld_hl_sp_i8(CPU *cpu) {     \
        /* 1. hl <- sp + i8 */                        \
        int8_t offset = (int8_t)mem_read(cpu->pc);    \
        uint16_t result;                              \
        set_flag(cpu, FLAG_Z, 0);                     \
        set_flag(cpu, FLAG_N, 0);                     \
        add_i8_to_u16(cpu->sp, offset, &result, cpu); \
        cpu->hl = result;                             \
        ADV_PC(cpu, 1);                               \
        ADV_CYCLES(cpu, 12);                          \
    }

#define DEF_LD_U16PTR_SP(OP)                       \
    static void op_##OP##_ld_u16ptr_sp(CPU *cpu) { \
        /* 1. (u16) <- sp */                       \
        uint16_t addr = mem_read16(cpu->pc);       \
        mem_write(addr, low_byte(cpu->sp));        \
        mem_write(addr + 1, high_byte(cpu->sp));   \
        ADV_PC(cpu, 2);                            \
        ADV_CYCLES(cpu, 20);                       \
    }

/*  x8/lsm  ---------------------------------------------- */
#define DEF_LD_R8_U8(OP, REG)                       \
    static void op_##OP##_ld_##REG##_u8(CPU *cpu) { \
        /* r8 <- u8 */                              \
        (cpu)->REG = mem_read((cpu)->pc);           \
        ADV_PC(cpu, 1);                             \
        ADV_CYCLES(cpu, 8);                         \
    }

#define DEF_LD_R8_R8(OP, DST, SRC)                     \
    static void op_##OP##_ld_##DST##_##SRC(CPU *cpu) { \
        /* r8 <- r8 */                                 \
        (cpu)->DST = (cpu)->SRC;                       \
        ADV_CYCLES(cpu, 4);                            \
    }

#define DEF_LD_R16PTR_R8(OP, R16, R8)                    \
    static void op_##OP##_ld_##R16##ptr_##R8(CPU *cpu) { \
        /* (r16) <- r8 */                                \
        mem_write((cpu)->R16, (cpu)->R8);                \
        ADV_CYCLES(cpu, 8);                              \
    }

#define DEF_LD_R8_R16PTR_INC(OP, R8, R16)                    \
    static void op_##OP##_ld_##R8##_##R16##ptr_i(CPU *cpu) { \
        /* 1. r8 <- (r16++) */                               \
        (cpu)->R8 = mem_read((cpu)->R16++);                  \
        ADV_CYCLES(cpu, 8);                                  \
    }

#define DEF_LD_R8_R16PTR_DEC(OP, R8, R16)                    \
    static void op_##OP##_ld_##R8##_##R16##ptr_d(CPU *cpu) { \
        /* 1. r8 <- (r16--) */                               \
        (cpu)->R8 = mem_read((cpu)->R16--);                  \
        ADV_CYCLES(cpu, 8);                                  \
    }

#define DEF_LD_R8_U16PTR(OP, R8)                       \
    static void op_##OP##_ld_##R8##_u16ptr(CPU *cpu) { \
        /* r8 <- (u16) */                              \
        uint16_t addr = mem_read16((cpu)->pc);         \
        (cpu)->R8 = mem_read(addr);                    \
        ADV_PC(cpu, 2);                                \
        ADV_CYCLES(cpu, 16);                           \
    }

#define DEF_LD_U16PTR_R8(OP, SRC)                     \
    static void op_##OP##_ld_u16ptr_##SRC(CPU *cpu) { \
        /* (u16) <- r8 */                             \
        uint16_t addr = mem_read16((cpu)->pc);        \
        mem_write(addr, (cpu)->SRC);                  \
        ADV_PC(cpu, 2);                               \
        ADV_CYCLES(cpu, 16);                          \
    }

#define DEF_LD_A_FF00U8PTR(OP)                             \
    static void op_##OP##_ld_a_ff00u8ptr(CPU *cpu) {       \
        /* 1. a <- (FF00 + u8) */                          \
        (cpu)->a = mem_read(0xFF00 + mem_read((cpu)->pc)); \
        ADV_PC(cpu, 1);                                    \
        ADV_CYCLES(cpu, 12);                               \
    }

#define DEF_LD_FF00U8PTR_A(OP)                        \
    static void op_##OP##_ld_ff00u8ptr_a(CPU *cpu) {  \
        /* (FF00 + u8) <- a */                        \
        uint16_t addr = 0xFF00 + mem_read((cpu)->pc); \
        mem_write(addr, (cpu)->a);                    \
        ADV_PC(cpu, 1);                               \
        ADV_CYCLES(cpu, 12);                          \
    }

#define DEF_LD_R8_R16PTR(OP, R8, R16)                      \
    static void op_##OP##_ld_##R8##_##R16##ptr(CPU *cpu) { \
        /* 1. r8 <- (r16) */                               \
        (cpu)->R8 = mem_read((cpu)->R16);                  \
        ADV_CYCLES(cpu, 8);                                \
    }

#define DEF_LD_R16PTR_U8(OP, R16)                      \
    static void op_##OP##_ld_##R16##ptr_u8(CPU *cpu) { \
        /* (r16) <- u8 */                              \
        mem_write((cpu)->R16, mem_read((cpu)->pc));    \
        ADV_PC(cpu, 1);                                \
        ADV_CYCLES(cpu, 12);                           \
    }

#define DEF_LD_R16PTR_INC_R8(OP, R16, R8)                  \
    static void op_##OP##_ld_##R16##ptr_i_##R8(CPU *cpu) { \
        /* (r16++) <- r8 */                                \
        mem_write((cpu)->R16++, (cpu)->R8);                \
        ADV_CYCLES(cpu, 8);                                \
    }

#define DEF_LD_R16PTR_DEC_R8(OP, R16, R8)                  \
    static void op_##OP##_ld_##R16##ptr_d_##R8(CPU *cpu) { \
        /* (r16--) <- r8 */                                \
        mem_write((cpu)->R16--, (cpu)->R8);                \
        ADV_CYCLES(cpu, 8);                                \
    }

#define DEF_LD_A_FF00CPTR(OP)                       \
    static void op_##OP##_ld_a_ff00cptr(CPU *cpu) { \
        /* a <- (FF00 + c) */                       \
        (cpu)->a = mem_read(0xFF00 + (cpu)->c);     \
        ADV_CYCLES(cpu, 8);                         \
    }

#define DEF_LD_FF00CPTR_A(OP)                       \
    static void op_##OP##_ld_ff00cptr_a(CPU *cpu) { \
        /* (FF00 + c) <- a */                       \
        mem_write(0xFF00 + (cpu)->c, (cpu)->a);     \
        ADV_CYCLES(cpu, 8);                         \
    }

/*  x16/lsm  -------------------------------------- */
#define DEF_LD_R16_U16(OP, R16)                      \
    static void op_##OP##_ld_##R16##_u16(CPU *cpu) { \
        /* r16 <- u16 */                             \
        uint16_t v = mem_read16((cpu)->pc);          \
        (cpu)->R16 = v;                              \
        ADV_PC(cpu, 2);                              \
        ADV_CYCLES(cpu, 12);                         \
    }

#define DEF_PUSH_R16(OP, R16)                        \
    static void op_##OP##_push_##R16(CPU *cpu) {     \
        /* 1. get the high and low bytes of r16 */   \
        uint8_t high = high_byte(cpu->R16);          \
        uint8_t low = low_byte(cpu->R16);            \
        /* 2. first, write the high, then the low */ \
        cpu->sp--;                                   \
        mem_write(cpu->sp, high);                    \
        cpu->sp--;                                   \
        mem_write(cpu->sp, low);                     \
        ADV_CYCLES(cpu, 16);                         \
    }

#define DEF_POP_R16(OP, R16)                    \
    static void op_##OP##_pop_##R16(CPU *cpu) { \
        /* 1. pop the low byte first */         \
        uint8_t low = mem_read(cpu->sp++);      \
        uint8_t high = mem_read(cpu->sp++);     \
        /* 2. combine the two bytes into r16 */ \
        cpu->R16 = (high << 8) | low;           \
        ADV_CYCLES(cpu, 12);                    \
    }

#define DEF_POP_AF(OP)                                        \
    static void op_##OP##_pop_af(CPU *cpu) {                  \
        /* 1. pop the low byte first */                       \
        uint8_t low = mem_read(cpu->sp++);                    \
        uint8_t high = mem_read(cpu->sp++);                   \
        /* 2. combine the two bytes into af */                \
        cpu->a = high;                                        \
        cpu->f = low & 0xF0; /* only keep the upper nibble */ \
        ADV_CYCLES(cpu, 12);                                  \
    }

#define DEF_LD_SP_HL(OP)                       \
    static void op_##OP##_ld_sp_hl(CPU *cpu) { \
        /* 1. sp <- hl */                      \
        cpu->sp = cpu->hl;                     \
        ADV_CYCLES(cpu, 8);                    \
    }

/*  control/branch  --------------------------------------------------- */
#define DEF_JR_U8(OP)                             \
    static void op_##OP##_jr_u8(CPU *cpu) {       \
        /* unconditional jump */                  \
        int8_t off = (int8_t)mem_read((cpu)->pc); \
        ADV_PC(cpu, 1);                           \
        (cpu)->pc += off;                         \
        ADV_CYCLES(cpu, 12);                      \
    }

#define DEF_JR_Z_U8(OP)                           \
    static void op_##OP##_jr_z_u8(CPU *cpu) {     \
        /* jump w/ offset if FLAG_Z = 1 */        \
        int8_t off = (int8_t)mem_read((cpu)->pc); \
        ADV_PC(cpu, 1);                           \
        if (get_flag(cpu, FLAG_Z)) {              \
            (cpu)->pc += off;                     \
            ADV_CYCLES(cpu, 12);                  \
        } else {                                  \
            ADV_CYCLES(cpu, 8);                   \
        }                                         \
    }

#define DEF_JR_NZ_U8(OP)                          \
    static void op_##OP##_jr_nz_u8(CPU *cpu) {    \
        /* jump w/ offset if FLAG_Z = 0 */        \
        int8_t off = (int8_t)mem_read((cpu)->pc); \
        ADV_PC(cpu, 1);                           \
        if (!get_flag(cpu, FLAG_Z)) {             \
            (cpu)->pc += off;                     \
            ADV_CYCLES(cpu, 12);                  \
        } else {                                  \
            ADV_CYCLES(cpu, 8);                   \
        }                                         \
    }

#define DEF_JR_C_U8(OP)                           \
    static void op_##OP##_jr_c_u8(CPU *cpu) {     \
        /* jump w/ offset if FLAG_C = 1 */        \
        int8_t off = (int8_t)mem_read((cpu)->pc); \
        ADV_PC(cpu, 1);                           \
        if (get_flag(cpu, FLAG_C)) {              \
            (cpu)->pc += off;                     \
            ADV_CYCLES(cpu, 12);                  \
        } else {                                  \
            ADV_CYCLES(cpu, 8);                   \
        }                                         \
    }

#define DEF_JR_NC_U8(OP)                          \
    static void op_##OP##_jr_nc_u8(CPU *cpu) {    \
        /* jump w/ offset if FLAG_C = 0 */        \
        int8_t off = (int8_t)mem_read((cpu)->pc); \
        ADV_PC(cpu, 1);                           \
        if (!get_flag(cpu, FLAG_C)) {             \
            (cpu)->pc += off;                     \
            ADV_CYCLES(cpu, 12);                  \
        } else {                                  \
            ADV_CYCLES(cpu, 8);                   \
        }                                         \
    }

#define DEF_JP(OP)                             \
    static void op_##OP##_jp(CPU *cpu) {       \
        /* unconditional jump */               \
        uint16_t addr = mem_read16((cpu)->pc); \
        (cpu)->pc = addr;                      \
        ADV_CYCLES(cpu, 16);                   \
    }

#define DEF_JP_HL(OP)                       \
    static void op_##OP##_jp_hl(CPU *cpu) { \
        /* jump to hl */                    \
        (cpu)->pc = (cpu)->hl;              \
        ADV_CYCLES(cpu, 4);                 \
    }

#define DEF_JP_NZ_U16(OP)                       \
    static void op_##OP##_jp_nz_u16(CPU *cpu) { \
        /* jump if FLAG_Z = 0 */                \
        uint16_t addr = mem_read16((cpu)->pc);  \
        ADV_PC(cpu, 2);                         \
        if (!get_flag(cpu, FLAG_Z)) {           \
            (cpu)->pc = addr;                   \
            ADV_CYCLES(cpu, 16);                \
        } else {                                \
            ADV_CYCLES(cpu, 12);                \
        }                                       \
    }

#define DEF_JP_NC_U16(OP)                       \
    static void op_##OP##_jp_nc_u16(CPU *cpu) { \
        /* jump if FLAG_C = 0 */                \
        uint16_t addr = mem_read16((cpu)->pc);  \
        ADV_PC(cpu, 2);                         \
        if (!get_flag(cpu, FLAG_C)) {           \
            (cpu)->pc = addr;                   \
            ADV_CYCLES(cpu, 16);                \
        } else {                                \
            ADV_CYCLES(cpu, 12);                \
        }                                       \
    }

#define DEF_JP_Z_U16(OP)                       \
    static void op_##OP##_jp_z_u16(CPU *cpu) { \
        /* jump if FLAG_Z = 1 */               \
        uint16_t addr = mem_read16((cpu)->pc); \
        ADV_PC(cpu, 2);                        \
        if (get_flag(cpu, FLAG_Z)) {           \
            (cpu)->pc = addr;                  \
            ADV_CYCLES(cpu, 16);               \
        } else {                               \
            ADV_CYCLES(cpu, 12);               \
        }                                      \
    }

#define DEF_JP_C_U16(OP)                       \
    static void op_##OP##_jp_c_u16(CPU *cpu) { \
        /* jump if FLAG_C = 1 */               \
        uint16_t addr = mem_read16((cpu)->pc); \
        ADV_PC(cpu, 2);                        \
        if (get_flag(cpu, FLAG_C)) {           \
            (cpu)->pc = addr;                  \
            ADV_CYCLES(cpu, 16);               \
        } else {                               \
            ADV_CYCLES(cpu, 12);               \
        }                                      \
    }

#define DEF_CALL_U16(OP)                       \
    static void op_##OP##_call_u16(CPU *cpu) { \
        /* use the CALL helper */              \
        CALL_U16(cpu);                         \
    }

#define DEF_CALL_NZ_U16(OP)                              \
    static void op_##OP##_call_nz_u16(CPU *cpu) {        \
        /* call if FLAG_Z = 0 */                         \
        if (!get_flag(cpu, FLAG_Z)) {                    \
            CALL_U16(cpu); /* push the return address */ \
        } else {                                         \
            ADV_PC(cpu, 2);                              \
            ADV_CYCLES(cpu, 12);                         \
        }                                                \
    }

#define DEF_CALL_NC_U16(OP)                           \
    static void op_##OP##_call_nc_u16(CPU *cpu) {     \
        /* call if FLAG_C = 0 */                      \
        if (!get_flag(cpu, FLAG_C)) {                 \
            CALL_U16(cpu); /* push the return addr */ \
        } else {                                      \
            ADV_PC(cpu, 2);                           \
            ADV_CYCLES(cpu, 12);                      \
        }                                             \
    }

#define DEF_CALL_Z_U16(OP)                            \
    static void op_##OP##_call_z_u16(CPU *cpu) {      \
        /* call if FLAG_Z = 1 */                      \
        if (get_flag(cpu, FLAG_Z)) {                  \
            CALL_U16(cpu); /* push the return addr */ \
        } else {                                      \
            ADV_PC(cpu, 2);                           \
            ADV_CYCLES(cpu, 12);                      \
        }                                             \
    }

#define DEF_CALL_C_U16(OP)                            \
    static void op_##OP##_call_c_u16(CPU *cpu) {      \
        /* call if FLAG_C = 1 */                      \
        if (get_flag(cpu, FLAG_C)) {                  \
            CALL_U16(cpu); /* push the return addr */ \
        } else {                                      \
            ADV_PC(cpu, 2);                           \
            ADV_CYCLES(cpu, 12);                      \
        }                                             \
    }

#define DEF_RET(OP)                                                      \
    static void op_##OP##_ret(CPU *cpu) {                                \
        /* 1. pop the return address (low byte first, then high byte) */ \
        RET(cpu);                                                        \
        ADV_CYCLES(cpu, 16);                                             \
    }

#define DEF_RET_NC(OP)                       \
    static void op_##OP##_ret_nc(CPU *cpu) { \
        /* return if FLAG_C = 0 */           \
        if (!get_flag(cpu, FLAG_C)) {        \
            /* use the RET helper */         \
            RET(cpu);                        \
            ADV_CYCLES(cpu, 20);             \
        } else {                             \
            ADV_CYCLES(cpu, 8);              \
        }                                    \
    }

#define DEF_RET_NZ(OP)                       \
    static void op_##OP##_ret_nz(CPU *cpu) { \
        /* return if FLAG_Z = 0 */           \
        if (!get_flag(cpu, FLAG_Z)) {        \
            /* use the RET helper */         \
            RET(cpu);                        \
            ADV_CYCLES(cpu, 20);             \
        } else {                             \
            ADV_CYCLES(cpu, 8);              \
        }                                    \
    }

#define DEF_RET_C(OP)                       \
    static void op_##OP##_ret_c(CPU *cpu) { \
        /* return if FLAG_C = 1 */          \
        if (get_flag(cpu, FLAG_C)) {        \
            /* use the RET helper */        \
            RET(cpu);                       \
            ADV_CYCLES(cpu, 20);            \
        } else {                            \
            ADV_CYCLES(cpu, 8);             \
        }                                   \
    }

#define DEF_RET_Z(OP)                       \
    static void op_##OP##_ret_z(CPU *cpu) { \
        /* return if FLAG_Z = 1 */          \
        if (get_flag(cpu, FLAG_Z)) {        \
            /* use the RET helper */        \
            RET(cpu);                       \
            ADV_CYCLES(cpu, 20);            \
        } else {                            \
            ADV_CYCLES(cpu, 8);             \
        }                                   \
    }

#define DEF_RETI(OP)                       \
    static void op_##OP##_reti(CPU *cpu) { \
        /* 1. pop the return address */    \
        RET(cpu);                          \
        /* 2. enable interrupts */         \
        cpu->ime = 1;                      \
        ADV_CYCLES(cpu, 16);               \
    }

#define DEF_RST(OP, ADDR)                        \
    static void op_##OP##_rst_##ADDR(CPU *cpu) { \
        /* 1. push the return address */         \
        cpu->sp--;                               \
        mem_write(cpu->sp, high_byte(cpu->pc));  \
        cpu->sp--;                               \
        mem_write(cpu->sp, low_byte(cpu->pc));   \
        /* 2. jump to the reset address */       \
        cpu->pc = ADDR;                          \
        ADV_CYCLES(cpu, 16);                     \
    }

/*  control/misc --------------------------- */
#define DEF_IMPLIED(OP, NAME, BODY, CYC)     \
    static void op_##OP##_##NAME(CPU *cpu) { \
        BODY;                                \
        ADV_CYCLES(cpu, CYC);                \
    }

#define DEF_HALT(OP)                                              \
    static void op_##OP##_halt(CPU *cpu) {                        \
        /* 1. halt the CPU */                                     \
        uint8_t flagged_and_enabled = cpu->ifr & cpu->ier & 0x1F; \
        if (cpu->ime == 0 && flagged_and_enabled != 0) {          \
            cpu->halt_bug = 1;                                    \
        } else {                                                  \
            cpu->halt = 1;                                        \
        }                                                         \
    }

/* x8/rsb (cb-prefixed) -------------------- */
#define DEF_RLC_R8(OP, R8)                               \
    static void op_##OP##_rlc_##R8(CPU *cpu) {           \
        /* 1. r8 <- r8 << 1 and set carry to high bit */ \
        uint8_t bit_7 = (cpu)->R8 & 0x80;                \
        (cpu)->R8 <<= 1;                                 \
        (cpu)->R8 |= bit_7 >> 7;                         \
        /* 2. set flags */                               \
        set_flag(cpu, FLAG_Z, (cpu)->R8 == 0);           \
        set_flag(cpu, FLAG_N, 0);                        \
        set_flag(cpu, FLAG_H, 0);                        \
        set_flag(cpu, FLAG_C, bit_7 ? 1 : 0);            \
        ADV_CYCLES(cpu, 8);                              \
    }

#define DEF_RLC_HLPTR(OP)                                     \
    static void op_##OP##_rlc_hlptr(CPU *cpu) {               \
        /* 1. (hl) <- (hl) << 1  and set carry to high bit */ \
        uint8_t byte_read = mem_read((cpu)->hl);              \
        uint8_t bit_7 = byte_read & 0x80;                     \
        byte_read <<= 1;                                      \
        byte_read |= bit_7 >> 7;                              \
        mem_write((cpu)->hl, byte_read);                      \
        /* 2. set flags */                                    \
        set_flag(cpu, FLAG_Z, byte_read == 0);                \
        set_flag(cpu, FLAG_N, 0);                             \
        set_flag(cpu, FLAG_H, 0);                             \
        set_flag(cpu, FLAG_C, bit_7 ? 1 : 0);                 \
        ADV_CYCLES(cpu, 16);                                  \
    }

#define DEF_RRC_R8(OP, R8)                              \
    static void op_##OP##_rrc_##R8(CPU *cpu) {          \
        /* 1. r8 <- r8 >> 1 and set carry to low bit */ \
        uint8_t bit_0 = (cpu)->R8 & 0x01;               \
        (cpu)->R8 >>= 1;                                \
        (cpu)->R8 |= (bit_0 << 7);                      \
        /* 2. set flags */                              \
        set_flag(cpu, FLAG_Z, (cpu)->R8 == 0);          \
        set_flag(cpu, FLAG_N, 0);                       \
        set_flag(cpu, FLAG_H, 0);                       \
        set_flag(cpu, FLAG_C, bit_0 ? 1 : 0);           \
        ADV_CYCLES(cpu, 8);                             \
    }

#define DEF_RRC_HLPTR(OP)                               \
    static void op_##OP##_rrc_hlptr(CPU *cpu) {         \
        /* 1. r8 <- r8 >> 1 and set carry to low bit */ \
        uint8_t byte_read = mem_read((cpu)->hl);        \
        uint8_t bit_0 = byte_read & 0x01;               \
        byte_read >>= 1;                                \
        byte_read |= (bit_0 << 7);                      \
        mem_write((cpu)->hl, byte_read);                \
        /* 2. set flags */                              \
        set_flag(cpu, FLAG_Z, byte_read == 0);          \
        set_flag(cpu, FLAG_N, 0);                       \
        set_flag(cpu, FLAG_H, 0);                       \
        set_flag(cpu, FLAG_C, bit_0 ? 1 : 0);           \
        ADV_CYCLES(cpu, 16);                            \
    }

#define DEF_RL_R8(OP, R8)                             \
    static void op_##OP##_rl_##R8(CPU *cpu) {         \
        /* 1. r8 <- r8 << 1 through the carry flag */ \
        uint8_t bit_7 = (cpu)->R8 & 0x80;             \
        (cpu)->R8 <<= 1;                              \
        (cpu)->R8 |= get_flag(cpu, FLAG_C);           \
        /* 2. set flags */                            \
        set_flag(cpu, FLAG_Z, (cpu)->R8 == 0);        \
        set_flag(cpu, FLAG_N, 0);                     \
        set_flag(cpu, FLAG_H, 0);                     \
        set_flag(cpu, FLAG_C, bit_7 ? 1 : 0);         \
        ADV_CYCLES(cpu, 8);                           \
    }

#define DEF_RL_HLPTR(OP)                                  \
    static void op_##OP##_rl_hlptr(CPU *cpu) {            \
        /* 1. (hl) <- (hl) << 1 through the carry flag */ \
        uint8_t byte_read = mem_read((cpu)->hl);          \
        uint8_t bit_7 = byte_read & 0x80;                 \
        byte_read <<= 1;                                  \
        byte_read |= get_flag(cpu, FLAG_C);               \
        mem_write((cpu)->hl, byte_read);                  \
        /* 2. set flags */                                \
        set_flag(cpu, FLAG_Z, byte_read == 0);            \
        set_flag(cpu, FLAG_N, 0);                         \
        set_flag(cpu, FLAG_H, 0);                         \
        set_flag(cpu, FLAG_C, bit_7 ? 1 : 0);             \
        ADV_CYCLES(cpu, 16);                              \
    }

#define DEF_RR_R8(OP, R8)                             \
    static void op_##OP##_rr_##R8(CPU *cpu) {         \
        /* 1. r8 <- r8 >> 1 through the carry flag */ \
        uint8_t bit_0 = (cpu)->R8 & 0x01;             \
        (cpu)->R8 >>= 1;                              \
        (cpu)->R8 |= (get_flag(cpu, FLAG_C) << 7);    \
        /* 2. set flags */                            \
        set_flag(cpu, FLAG_Z, (cpu)->R8 == 0);        \
        set_flag(cpu, FLAG_N, 0);                     \
        set_flag(cpu, FLAG_H, 0);                     \
        set_flag(cpu, FLAG_C, bit_0 ? 1 : 0);         \
        ADV_CYCLES(cpu, 8);                           \
    }

#define DEF_RR_HLPTR(OP)                                  \
    static void op_##OP##_rr_hlptr(CPU *cpu) {            \
        /* 1. (hl) <- (hl) >> 1 through the carry flag */ \
        uint8_t byte_read = mem_read((cpu)->hl);          \
        uint8_t bit_0 = byte_read & 0x01;                 \
        byte_read >>= 1;                                  \
        byte_read |= (get_flag(cpu, FLAG_C) << 7);        \
        mem_write((cpu)->hl, byte_read);                  \
        /* 2. set flags */                                \
        set_flag(cpu, FLAG_Z, byte_read == 0);            \
        set_flag(cpu, FLAG_N, 0);                         \
        set_flag(cpu, FLAG_H, 0);                         \
        set_flag(cpu, FLAG_C, bit_0 ? 1 : 0);             \
        ADV_CYCLES(cpu, 16);                              \
    }

#define DEF_SLA_R8(OP, R8)                     \
    static void op_##OP##_sla_##R8(CPU *cpu) { \
        /* 1. shift left arithmetically */     \
        /* flag_c <- bit7, bit0 <- 0 */        \
        uint8_t bit_7 = (cpu)->R8 & 0x80;      \
        (cpu)->R8 <<= 1;                       \
        (cpu)->R8 &= ~1;                       \
        /* 2. set flags */                     \
        set_flag(cpu, FLAG_Z, (cpu)->R8 == 0); \
        set_flag(cpu, FLAG_N, 0);              \
        set_flag(cpu, FLAG_H, 0);              \
        set_flag(cpu, FLAG_C, bit_7 ? 1 : 0);  \
        ADV_CYCLES(cpu, 8);                    \
    }

#define DEF_SLA_HLPTR(OP)                        \
    static void op_##OP##_sla_hlptr(CPU *cpu) {  \
        /* 1. shift left arithmetically */       \
        /* flag_c <- bit7, bit0 <- 0 */          \
        uint8_t byte_read = mem_read((cpu)->hl); \
        uint8_t bit_7 = byte_read & 0x80;        \
        byte_read <<= 1;                         \
        byte_read &= ~1;                         \
        mem_write((cpu)->hl, byte_read);         \
        /* 2. set flags */                       \
        set_flag(cpu, FLAG_Z, byte_read == 0);   \
        set_flag(cpu, FLAG_N, 0);                \
        set_flag(cpu, FLAG_H, 0);                \
        set_flag(cpu, FLAG_C, bit_7 ? 1 : 0);    \
        ADV_CYCLES(cpu, 16);                     \
    }

#define DEF_SRA_R8(OP, R8)                     \
    static void op_##OP##_sra_##R8(CPU *cpu) { \
        /* 1. shift right arithmetically */    \
        /* flag_c <- bit0, bit7 unchanged */   \
        uint8_t bit_0 = (cpu)->R8 & 0x01;      \
        uint8_t bit_7 = (cpu)->R8 & 0x80;      \
        (cpu)->R8 >>= 1;                       \
        (cpu)->R8 |= bit_7;                    \
        /* 2. set flags */                     \
        set_flag(cpu, FLAG_Z, (cpu)->R8 == 0); \
        set_flag(cpu, FLAG_N, 0);              \
        set_flag(cpu, FLAG_H, 0);              \
        set_flag(cpu, FLAG_C, bit_0 ? 1 : 0);  \
        ADV_CYCLES(cpu, 8);                    \
    }

#define DEF_SRA_HLPTR(OP)                        \
    static void op_##OP##_sra_hlptr(CPU *cpu) {  \
        /* 1. shift right arithmetically */      \
        /* flag_c <- bit0, bit7 unchanged */     \
        uint8_t byte_read = mem_read((cpu)->hl); \
        uint8_t bit_0 = byte_read & 0x01;        \
        uint8_t bit_7 = byte_read & 0x80;        \
        byte_read >>= 1;                         \
        byte_read |= bit_7;                      \
        mem_write((cpu)->hl, byte_read);         \
        /* 2. set flags */                       \
        set_flag(cpu, FLAG_Z, byte_read == 0);   \
        set_flag(cpu, FLAG_N, 0);                \
        set_flag(cpu, FLAG_H, 0);                \
        set_flag(cpu, FLAG_C, bit_0 ? 1 : 0);    \
        ADV_CYCLES(cpu, 16);                     \
    }

#define DEF_SWAP_R8(OP, R8)                              \
    static void op_##OP##_swap_##R8(CPU *cpu) {          \
        /* 1. swap the nibbles */                        \
        (cpu)->R8 = ((cpu)->R8 << 4) | ((cpu)->R8 >> 4); \
        /* 2. set flags */                               \
        set_flag(cpu, FLAG_Z, (cpu)->R8 == 0);           \
        set_flag(cpu, FLAG_N, 0);                        \
        set_flag(cpu, FLAG_H, 0);                        \
        set_flag(cpu, FLAG_C, 0);                        \
        ADV_CYCLES(cpu, 8);                              \
    }

#define DEF_SWAP_HLPTR(OP)                               \
    static void op_##OP##_swap_hlptr(CPU *cpu) {         \
        /* 1. swap the nibbles */                        \
        uint8_t byte_read = mem_read((cpu)->hl);         \
        byte_read = (byte_read << 4) | (byte_read >> 4); \
        mem_write((cpu)->hl, byte_read);                 \
        /* 2. set flags */                               \
        set_flag(cpu, FLAG_Z, byte_read == 0);           \
        set_flag(cpu, FLAG_N, 0);                        \
        set_flag(cpu, FLAG_H, 0);                        \
        set_flag(cpu, FLAG_C, 0);                        \
        ADV_CYCLES(cpu, 16);                             \
    }

#define DEF_SRL_R8(OP, R8)                     \
    static void op_##OP##_srl_##R8(CPU *cpu) { \
        /* 1. shift right locally */           \
        /* flag_c <- bit0, bit7 <- 0 */        \
        uint8_t bit_0 = (cpu)->R8 & 0x01;      \
        (cpu)->R8 >>= 1;                       \
        (cpu)->R8 &= ~0x80;                    \
        /* 2. set flags */                     \
        set_flag(cpu, FLAG_Z, (cpu)->R8 == 0); \
        set_flag(cpu, FLAG_N, 0);              \
        set_flag(cpu, FLAG_H, 0);              \
        set_flag(cpu, FLAG_C, bit_0 ? 1 : 0);  \
        ADV_CYCLES(cpu, 8);                    \
    }

#define DEF_SRL_HLPTR(OP)                        \
    static void op_##OP##_srl_hlptr(CPU *cpu) {  \
        /* 1. shift right locally */             \
        /* flag_c <- bit0, bit7 <- 0 */          \
        uint8_t byte_read = mem_read((cpu)->hl); \
        uint8_t bit_0 = byte_read & 0x01;        \
        byte_read >>= 1;                         \
        byte_read &= ~0x80;                      \
        mem_write((cpu)->hl, byte_read);         \
        /* 2. set flags */                       \
        set_flag(cpu, FLAG_Z, byte_read == 0);   \
        set_flag(cpu, FLAG_N, 0);                \
        set_flag(cpu, FLAG_H, 0);                \
        set_flag(cpu, FLAG_C, bit_0 ? 1 : 0);    \
        ADV_CYCLES(cpu, 16);                     \
    }

#define DEF_BIT_U3_R8(OP, BIT, R8)                      \
    static void op_##OP##_bit_##BIT##_##R8(CPU *cpu) {  \
        /* 1. test bit BIT in r8 */                     \
        uint8_t mask = 1 << BIT;                        \
        set_flag(cpu, FLAG_Z, ((cpu)->R8 & mask) == 0); \
        set_flag(cpu, FLAG_N, 0);                       \
        set_flag(cpu, FLAG_H, 1);                       \
        ADV_CYCLES(cpu, 8);                             \
    }

#define DEF_BIT_U3_HLPTR(OP, BIT)                       \
    static void op_##OP##_bit_##BIT##_hlptr(CPU *cpu) { \
        /* 1. test bit BIT in (hl) */                   \
        uint8_t byte_read = mem_read((cpu)->hl);        \
        uint8_t mask = 1 << BIT;                        \
        set_flag(cpu, FLAG_Z, (byte_read & mask) == 0); \
        set_flag(cpu, FLAG_N, 0);                       \
        set_flag(cpu, FLAG_H, 1);                       \
        ADV_CYCLES(cpu, 12);                            \
    }

#define DEF_RES_U3_R8(OP, BIT, R8)                     \
    static void op_##OP##_res_##BIT##_##R8(CPU *cpu) { \
        /* 1. reset bit BIT in r8 */                   \
        uint8_t mask = (uint8_t)(~(1 << BIT));         \
        (cpu)->R8 &= mask;                             \
        ADV_CYCLES(cpu, 8);                            \
    }

#define DEF_RES_U3_HLPTR(OP, BIT)                       \
    static void op_##OP##_res_##BIT##_hlptr(CPU *cpu) { \
        /* 1. reset bit BIT in (hl) */                  \
        uint8_t byte_read = mem_read((cpu)->hl);        \
        uint8_t mask = (uint8_t)(~(1 << BIT));          \
        byte_read &= mask;                              \
        mem_write((cpu)->hl, byte_read);                \
        ADV_CYCLES(cpu, 16);                            \
    }

#define DEF_SET_U3_R8(OP, BIT, R8)                     \
    static void op_##OP##_set_##BIT##_##R8(CPU *cpu) { \
        /* 1. set bit BIT in r8 */                     \
        uint8_t mask = 1 << BIT;                       \
        (cpu)->R8 |= mask;                             \
        ADV_CYCLES(cpu, 8);                            \
    }

#define DEF_SET_U3_HLPTR(OP, BIT)                       \
    static void op_##OP##_set_##BIT##_hlptr(CPU *cpu) { \
        /* 1. set bit BIT in (hl) */                    \
        uint8_t byte_read = mem_read((cpu)->hl);        \
        uint8_t mask = 1 << BIT;                        \
        byte_read |= mask;                              \
        mem_write((cpu)->hl, byte_read);                \
        ADV_CYCLES(cpu, 16);                            \
    }

#define DEF_RRA()                                     \
    static void op_rra(CPU *cpu) {                    \
        /* 1. r8 <- r8 >> 1 through the carry flag */ \
        uint8_t bit_0 = (cpu)->a & 0x01;              \
        (cpu)->a >>= 1;                               \
        (cpu)->a |= (get_flag(cpu, FLAG_C) << 7);     \
        /* 2. set flags */                            \
        set_flag(cpu, FLAG_Z, 0);                     \
        set_flag(cpu, FLAG_N, 0);                     \
        set_flag(cpu, FLAG_H, 0);                     \
        set_flag(cpu, FLAG_C, bit_0 ? 1 : 0);         \
        ADV_CYCLES(cpu, 4);                           \
    }

#define DEF_RRCA()                                      \
    static void op_rrca(CPU *cpu) {                     \
        /* 1. r8 <- r8 >> 1 and set carry to low bit */ \
        uint8_t bit_0 = (cpu)->a & 0x01;                \
        (cpu)->a >>= 1;                                 \
        (cpu)->a |= (bit_0 << 7);                       \
        /* 2. set flags */                              \
        set_flag(cpu, FLAG_Z, 0);                       \
        set_flag(cpu, FLAG_N, 0);                       \
        set_flag(cpu, FLAG_H, 0);                       \
        set_flag(cpu, FLAG_C, bit_0 ? 1 : 0);           \
        ADV_CYCLES(cpu, 4);                             \
    }

#define DEF_RLA()                                     \
    static void op_rla(CPU *cpu) {                    \
        /* 1. r8 <- r8 << 1 through the carry flag */ \
        uint8_t bit_7 = (cpu)->a & 0x80;              \
        (cpu)->a <<= 1;                               \
        (cpu)->a |= get_flag(cpu, FLAG_C);            \
        /* 2. set flags */                            \
        set_flag(cpu, FLAG_Z, 0);                     \
        set_flag(cpu, FLAG_N, 0);                     \
        set_flag(cpu, FLAG_H, 0);                     \
        set_flag(cpu, FLAG_C, bit_7 ? 1 : 0);         \
        ADV_CYCLES(cpu, 4);                           \
    }

#define DEF_RLCA()                                       \
    static void op_rlca(CPU *cpu) {                      \
        /* 1. r8 <- r8 << 1 and set carry to high bit */ \
        uint8_t bit_7 = (cpu)->a & 0x80;                 \
        (cpu)->a <<= 1;                                  \
        (cpu)->a |= bit_7 >> 7;                          \
        /* 2. set flags */                               \
        set_flag(cpu, FLAG_Z, 0);                        \
        set_flag(cpu, FLAG_N, 0);                        \
        set_flag(cpu, FLAG_H, 0);                        \
        set_flag(cpu, FLAG_C, bit_7 ? 1 : 0);            \
        ADV_CYCLES(cpu, 4);                              \
    }


/* check https://izik1.github.io/gbops/index.html */


#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* function to log CPU errors with useful information */
static void log_cpu_error(CPU *cpu, const char *format, ...) {
    va_list args;
    va_start(args, format);

    fprintf(stderr, "\n=== error ===\n");
    fprintf(stderr, "pc: 0x%04X\n", cpu->pc);
    fprintf(stderr, "opcode: 0x%02X\n", cpu->last_opcode);
    fprintf(stderr, "error: ");
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");

    // print CPU state
    fprintf(stderr, "\nCPU state:\n");
    fprintf(stderr, "A: 0x%02X  F: 0x%02X\n", cpu->a, cpu->f);
    fprintf(stderr, "B: 0x%02X  C: 0x%02X\n", cpu->b, cpu->c);
    fprintf(stderr, "D: 0x%02X  E: 0x%02X\n", cpu->d, cpu->e);
    fprintf(stderr, "H: 0x%02X  L: 0x%02X\n", cpu->h, cpu->l);
    fprintf(stderr, "sp: 0x%04X\n", cpu->sp);
    fprintf(stderr, "cycles: %" PRIu64 "\n", cpu->cycles);
    fprintf(stderr, "===================\n\n");

    va_end(args);
    exit(1);
}

/* ----  x8/alu ---- */
DEF_INC_R8(0x04, b) /* INC B */
DEF_DEC_R8(0x05, b) /* DEC B */
DEF_INC_R8(0x0c, c) /* INC C */
DEF_DEC_R8(0x0d, c) /* DEC C */
DEF_INC_R8(0x14, d) /* INC D */
DEF_DEC_R8(0x15, d) /* DEC D */
DEF_INC_R8(0x1c, e) /* INC E */
DEF_DEC_R8(0x1d, e) /* DEC E */
DEF_INC_R8(0x24, h) /* INC H */
DEF_DEC_R8(0x25, h) /* DEC H */
DEF_INC_R8(0x2c, l) /* INC L */
DEF_DEC_R8(0x2d, l) /* DEC L */
DEF_INC_HLPTR(0x34) /* INC (HL) */
DEF_DEC_HLPTR(0x35) /* DEC (HL) */
DEF_INC_R8(0x3c, a) /* INC A */
DEF_DEC_R8(0x3d, a) /* DEC A */

/* add */
DEF_ADD_A_R8(0x80, b) /* ADD A, B */
DEF_ADD_A_R8(0x81, c) /* ADD A, C */
DEF_ADD_A_R8(0x82, d) /* ADD A, D */
DEF_ADD_A_R8(0x83, e) /* ADD A, E */
DEF_ADD_A_R8(0x84, h) /* ADD A, H */
DEF_ADD_A_R8(0x85, l) /* ADD A, L */
DEF_ADD_A_HLPTR(0x86) /* ADD A, (HL) */
DEF_ADD_A_R8(0x87, a) /* ADD A, A */
DEF_ADC_A_R8(0x88, b) /* ADC A, B */
DEF_ADC_A_R8(0x89, c) /* ADC A, C */
DEF_ADC_A_R8(0x8a, d) /* ADC A, D */
DEF_ADC_A_R8(0x8b, e) /* ADC A, E */
DEF_ADC_A_R8(0x8c, h) /* ADC A, H */
DEF_ADC_A_R8(0x8d, l) /* ADC A, L */
DEF_ADC_A_HLPTR(0x8e) /* ADC A, (HL) */
DEF_ADC_A_R8(0x8f, a) /* ADC A, A */

/* sub */
DEF_SUB_A_R8(0x90, b) /* SUB A, B */
DEF_SUB_A_R8(0x91, c) /* SUB A, C */
DEF_SUB_A_R8(0x92, d) /* SUB A, D */
DEF_SUB_A_R8(0x93, e) /* SUB A, E */
DEF_SUB_A_R8(0x94, h) /* SUB A, H */
DEF_SUB_A_R8(0x95, l) /* SUB A, L */
DEF_SUB_A_HLPTR(0x96) /* SUB A, (HL) */
DEF_SUB_A_R8(0x97, a) /* SUB A, A */
DEF_SBC_A_R8(0x98, b) /* SBC A, B */
DEF_SBC_A_R8(0x99, c) /* SBC A, C */
DEF_SBC_A_R8(0x9a, d) /* SBC A, D */
DEF_SBC_A_R8(0x9b, e) /* SBC A, E */
DEF_SBC_A_R8(0x9c, h) /* SBC A, H */
DEF_SBC_A_R8(0x9d, l) /* SBC A, L */
DEF_SBC_A_HLPTR(0x9e) /* SBC A, (HL) */
DEF_SBC_A_R8(0x9f, a) /* SBC A, A */

/* and & xor */
DEF_AND_A_R8(0xa0, b) /* AND A, B */
DEF_AND_A_R8(0xa1, c) /* AND A, C */
DEF_AND_A_R8(0xa2, d) /* AND A, D */
DEF_AND_A_R8(0xa3, e) /* AND A, E */
DEF_AND_A_R8(0xa4, h) /* AND A, H */
DEF_AND_A_R8(0xa5, l) /* AND A, L */
DEF_AND_A_HLPTR(0xa6) /* AND A, (HL) */
DEF_AND_A_R8(0xa7, a) /* AND A, A */
DEF_XOR_A_R8(0xa8, b) /* XOR A, B */
DEF_XOR_A_R8(0xa9, c) /* XOR A, C */
DEF_XOR_A_R8(0xaa, d) /* XOR A, D */
DEF_XOR_A_R8(0xab, e) /* XOR A, E */
DEF_XOR_A_R8(0xac, h) /* XOR A, H */
DEF_XOR_A_R8(0xad, l) /* XOR A, L */
DEF_XOR_A_HLPTR(0xae) /* XOR A, (HL) */
DEF_XOR_A_R8(0xaf, a) /* XOR A, A */

/* or & cp */
DEF_OR_A_R8(0xb0, b) /* OR A, B */
DEF_OR_A_R8(0xb1, c) /* OR A, C */
DEF_OR_A_R8(0xb2, d) /* OR A, D */
DEF_OR_A_R8(0xb3, e) /* OR A, E */
DEF_OR_A_R8(0xb4, h) /* OR A, H */
DEF_OR_A_R8(0xb5, l) /* OR A, L */
DEF_OR_A_HLPTR(0xb6) /* OR A, (HL) */
DEF_OR_A_R8(0xb7, a) /* OR A, A */
DEF_CP_A_R8(0xb8, b) /* CP A, B */
DEF_CP_A_R8(0xb9, c) /* CP A, C */
DEF_CP_A_R8(0xba, d) /* CP A, D */
DEF_CP_A_R8(0xbb, e) /* CP A, E */
DEF_CP_A_R8(0xbc, h) /* CP A, H */
DEF_CP_A_R8(0xbd, l) /* CP A, L */
DEF_CP_A_HLPTR(0xbe) /* CP A, (HL) */
DEF_CP_A_R8(0xbf, a) /* CP A, A */

/* operations with immediate */
DEF_ADD_A_U8(0xc6) /* ADD A, u8 */
DEF_ADC_A_U8(0xce) /* ADC A, u8 */
DEF_SUB_A_U8(0xd6) /* SUB A, u8 */
DEF_SBC_A_U8(0xde) /* SBC A, u8 */
DEF_AND_A_U8(0xe6) /* AND A, u8 */
DEF_XOR_A_U8(0xee) /* XOR A, u8 */
DEF_OR_A_U8(0xf6)  /* OR A, u8 */
DEF_CP_A_U8(0xfe)  /* CP A, u8 */

DEF_DAA(0x27) /* DAA */
DEF_CPL(0x2f) /* CPL */
DEF_SCF(0x37) /* SCF */
DEF_CCF(0x3f) /* CCF */

/* ---- x16/alu ---- */
DEF_INC_R16(0x03, bc)    /* INC BC */
DEF_ADD_HL_R16(0x09, bc) /* ADD HL, BC */
DEF_INC_R16(0x13, de)    /* INC DE */
DEF_ADD_HL_R16(0x19, de) /* ADD HL, DE */
DEF_INC_R16(0x23, hl)    /* INC HL */
DEF_ADD_HL_R16(0x29, hl) /* ADD HL, HL */
DEF_INC_R16(0x33, sp)    /* INC SP */
DEF_ADD_HL_R16(0x39, sp) /* ADD HL, SP */
DEF_DEC_R16(0x0b, bc)    /* DEC BC */
DEF_DEC_R16(0x1b, de)    /* DEC DE */
DEF_DEC_R16(0x2b, hl)    /* DEC HL */
DEF_DEC_R16(0x3b, sp)    /* DEC SP */
DEF_ADD_SP_I8(0xe8)      /* ADD SP, i8 */
DEF_LD_HL_SP_I8(0xf8)    /* LD HL, SP+i8 */

/* ----  x8/lsm ---- */
DEF_LD_R16PTR_R8(0x02, bc, a)     /* LD (BC), A */
DEF_LD_R8_U8(0x06, b)             /* LD B, u8 */
DEF_LD_R8_R16PTR(0x0a, a, bc)     /* LD A, (BC) */
DEF_LD_R8_U8(0x0e, c)             /* LD C, u8 */
DEF_LD_R16PTR_R8(0x12, de, a)     /* LD (DE), A */
DEF_LD_R8_U8(0x16, d)             /* LD D, u8 */
DEF_LD_R8_R16PTR(0x1a, a, de)     /* LD A, (DE) */
DEF_LD_R8_U8(0x1e, e)             /* LD E, u8 */
DEF_LD_R16PTR_INC_R8(0x22, hl, a) /* LD (HL+), A */
DEF_LD_R8_U8(0x26, h)             /* LD H, u8 */
DEF_LD_R8_R16PTR_INC(0x2a, a, hl) /* LD A, (HL+) */
DEF_LD_R8_U8(0x2e, l)             /* LD L, u8 */
DEF_LD_R16PTR_DEC_R8(0x32, hl, a) /* LD (HL-), A */
DEF_LD_R16PTR_U8(0x36, hl)        /* LD (HL), u8 */
DEF_LD_R8_R16PTR_DEC(0x3a, a, hl) /* LD A, (HL-) */
DEF_LD_R8_U8(0x3e, a)             /* LD A, u8 */

DEF_LD_R8_R8(0x40, b, b)      /* LD B, B */
DEF_LD_R8_R8(0x41, b, c)      /* LD B, C */
DEF_LD_R8_R8(0x42, b, d)      /* LD B, D */
DEF_LD_R8_R8(0x43, b, e)      /* LD B, E */
DEF_LD_R8_R8(0x44, b, h)      /* LD B, H */
DEF_LD_R8_R8(0x45, b, l)      /* LD B, L */
DEF_LD_R8_R16PTR(0x46, b, hl) /* LD B, (HL) */
DEF_LD_R8_R8(0x47, b, a)      /* LD B, A */
DEF_LD_R8_R8(0x48, c, b)      /* LD C, B */
DEF_LD_R8_R8(0x49, c, c)      /* LD C, C */
DEF_LD_R8_R8(0x4a, c, d)      /* LD C, D */
DEF_LD_R8_R8(0x4b, c, e)      /* LD C, E */
DEF_LD_R8_R8(0x4c, c, h)      /* LD C, H */
DEF_LD_R8_R8(0x4d, c, l)      /* LD C, L */
DEF_LD_R8_R16PTR(0x4e, c, hl) /* LD C, (HL) */
DEF_LD_R8_R8(0x4f, c, a)      /* LD C, A */

DEF_LD_R8_R8(0x50, d, b)      /* LD D, B */
DEF_LD_R8_R8(0x51, d, c)      /* LD D, C */
DEF_LD_R8_R8(0x52, d, d)      /* LD D, D */
DEF_LD_R8_R8(0x53, d, e)      /* LD D, E */
DEF_LD_R8_R8(0x54, d, h)      /* LD D, H */
DEF_LD_R8_R8(0x55, d, l)      /* LD D, L */
DEF_LD_R8_R16PTR(0x56, d, hl) /* LD D, (HL) */
DEF_LD_R8_R8(0x57, d, a)      /* LD D, A */
DEF_LD_R8_R8(0x58, e, b)      /* LD E, B */
DEF_LD_R8_R8(0x59, e, c)      /* LD E, C */
DEF_LD_R8_R8(0x5a, e, d)      /* LD E, D */
DEF_LD_R8_R8(0x5b, e, e)      /* LD E, E */
DEF_LD_R8_R8(0x5c, e, h)      /* LD E, H */
DEF_LD_R8_R8(0x5d, e, l)      /* LD E, L */
DEF_LD_R8_R16PTR(0x5e, e, hl) /* LD E, (HL) */
DEF_LD_R8_R8(0x5f, e, a)      /* LD E, A */

DEF_LD_R8_R8(0x60, h, b)      /* LD H, B */
DEF_LD_R8_R8(0x61, h, c)      /* LD H, C */
DEF_LD_R8_R8(0x62, h, d)      /* LD H, D */
DEF_LD_R8_R8(0x63, h, e)      /* LD H, E */
DEF_LD_R8_R8(0x64, h, h)      /* LD H, H */
DEF_LD_R8_R8(0x65, h, l)      /* LD H, L */
DEF_LD_R8_R16PTR(0x66, h, hl) /* LD H, (HL) */
DEF_LD_R8_R8(0x67, h, a)      /* LD H, A */
DEF_LD_R8_R8(0x68, l, b)      /* LD L, B */
DEF_LD_R8_R8(0x69, l, c)      /* LD L, C */
DEF_LD_R8_R8(0x6a, l, d)      /* LD L, D */
DEF_LD_R8_R8(0x6b, l, e)      /* LD L, E */
DEF_LD_R8_R8(0x6c, l, h)      /* LD L, H */
DEF_LD_R8_R8(0x6d, l, l)      /* LD L, L */
DEF_LD_R8_R16PTR(0x6e, l, hl) /* LD L, (HL) */
DEF_LD_R8_R8(0x6f, l, a)      /* LD L, A */

DEF_LD_R16PTR_R8(0x70, hl, b) /* LD (HL), B */
DEF_LD_R16PTR_R8(0x71, hl, c) /* LD (HL), C */
DEF_LD_R16PTR_R8(0x72, hl, d) /* LD (HL), D */
DEF_LD_R16PTR_R8(0x73, hl, e) /* LD (HL), E */
DEF_LD_R16PTR_R8(0x74, hl, h) /* LD (HL), H */
DEF_LD_R16PTR_R8(0x75, hl, l) /* LD (HL), L */
DEF_LD_R16PTR_R8(0x77, hl, a) /* LD (HL), A */

DEF_LD_R8_R8(0x78, a, b)      /* LD A, B */
DEF_LD_R8_R8(0x79, a, c)      /* LD A, C */
DEF_LD_R8_R8(0x7a, a, d)      /* LD A, D */
DEF_LD_R8_R8(0x7b, a, e)      /* LD A, E */
DEF_LD_R8_R8(0x7c, a, h)      /* LD A, H */
DEF_LD_R8_R8(0x7d, a, l)      /* LD A, L */
DEF_LD_R8_R16PTR(0x7e, a, hl) /* LD A, (HL) */
DEF_LD_R8_R8(0x7f, a, a)      /* LD A, A */
DEF_LD_FF00U8PTR_A(0xe0)      /* LD (FF00 + u8), A */
DEF_LD_FF00CPTR_A(0xe2)       /* LD (FF00 + C), A */
DEF_LD_U16PTR_R8(0xea, a)     /* LD (u16), A */
DEF_LD_A_FF00U8PTR(0xf0)      /* LD A, (FF00 + u8) */
DEF_LD_A_FF00CPTR(0xf2)       /* LD A, (FF00 + C) */
DEF_LD_R8_U16PTR(0xfa, a)     /* LD A, (u16) */

/* ----  x16/lsm ---- */
DEF_LD_R16_U16(0x01, bc) /* LD BC, u16 */
DEF_LD_U16PTR_SP(0x08)   /* LD (u16), SP */
DEF_LD_R16_U16(0x11, de) /* LD DE, u16 */
DEF_LD_R16_U16(0x21, hl) /* LD HL, u16 */
DEF_LD_R16_U16(0x31, sp) /* LD SP, u16 */
DEF_POP_R16(0xc1, bc)    /* POP BC */
DEF_POP_R16(0xd1, de)    /* POP DE */
DEF_POP_R16(0xe1, hl)    /* POP HL */
DEF_POP_AF(0xf1)         /* POP AF */
DEF_PUSH_R16(0xc5, bc)   /* PUSH BC */
DEF_PUSH_R16(0xd5, de)   /* PUSH DE */
DEF_PUSH_R16(0xe5, hl)   /* PUSH HL */
DEF_PUSH_R16(0xf5, af)   /* PUSH AF */
DEF_LD_SP_HL(0xf9)       /* LD SP, HL */

/* ----  control/branch ---- */
DEF_JR_U8(0x18)       /* JR (u8) */
DEF_JR_NZ_U8(0x20)    /* JR NZ, (u8) -> jump if z = 0 */
DEF_JR_Z_U8(0x28)     /* JR Z, (u8) -> jump if z = 1 */
DEF_JR_NC_U8(0x30)    /* JR NC, (u8) -> jump if c = 0 */
DEF_JR_C_U8(0x38)     /* JR C, (u8) -> jump if c = 1 */
DEF_RET_NZ(0xc0)      /* RET NZ -> return if z = 0 */
DEF_JP_NZ_U16(0xc2)   /* JP NZ, (u16) -> jump if z = 0 */
DEF_JP(0xc3)          /* JP (u16) */
DEF_CALL_NZ_U16(0xc4) /* CALL NZ, (u16) -> call if z = 0 */
DEF_RST(0xc7, 0x0000) /* RST 0x00 */
DEF_RET_Z(0xc8)       /* RET Z -> return if z = 1 */
DEF_RET(0xc9)         /* RET */
DEF_JP_Z_U16(0xca)    /* JP Z, (u16) -> jump if z = 1 */
DEF_CALL_Z_U16(0xcc)  /* CALL Z, (u16) -> call if z = 1 */
DEF_CALL_U16(0xcd)    /* CALL (u16) */
DEF_RST(0xcf, 0x0008) /* RST 0x08 */
DEF_RET_NC(0xd0)      /* RET NC -> return if c = 0 */
DEF_JP_NC_U16(0xd2)   /* JP NC, (u16) -> jump if c = 0 */
DEF_CALL_NC_U16(0xd4) /* CALL NC, (u16) -> call if c = 0 */
DEF_RST(0xd7, 0x0010) /* RST 0x10 */
DEF_RET_C(0xd8)       /* RET C -> return if c = 1 */
DEF_RETI(0xd9)        /* RETI */
DEF_JP_C_U16(0xda)    /* JP C, (u16) -> jump if c = 1 */
DEF_CALL_C_U16(0xdc)  /* CALL C, (u16) -> call if c = 1 */
DEF_RST(0xdf, 0x0018) /* RST 0x18 */
DEF_RST(0xe7, 0x0020) /* RST 0x20 */
DEF_JP_HL(0xe9)       /* JP HL -> jump to HL */
DEF_RST(0xef, 0x0028) /* RST 0x28 */
DEF_RST(0xf7, 0x0030) /* RST 0x30 */
DEF_RST(0xff, 0x0038) /* RST 0x38 */

/* ---- x8/rsb (cb-prefixed) ---- */
DEF_RLA()
DEF_RLCA()
DEF_RRA()
DEF_RRCA()
DEF_RLC_R8(0x00, b) /* RLC B */
DEF_RLC_R8(0x01, c) /* RLC C */
DEF_RLC_R8(0x02, d) /* RLC D */
DEF_RLC_R8(0x03, e) /* RLC E */
DEF_RLC_R8(0x04, h) /* RLC H */
DEF_RLC_R8(0x05, l) /* RLC L */
DEF_RLC_HLPTR(0x06) /* RLC (HL) */
DEF_RLC_R8(0x07, a) /* RLC A */

DEF_RRC_R8(0x08, b) /* RRC B */
DEF_RRC_R8(0x09, c) /* RRC C */
DEF_RRC_R8(0x0a, d) /* RRC D */
DEF_RRC_R8(0x0b, e) /* RRC E */
DEF_RRC_R8(0x0c, h) /* RRC H */
DEF_RRC_R8(0x0d, l) /* RRC L */
DEF_RRC_HLPTR(0x0e) /* RRC (HL) */
DEF_RRC_R8(0x0f, a) /* RRC A */

DEF_RL_R8(0x10, b) /* RL B */
DEF_RL_R8(0x11, c) /* RL C */
DEF_RL_R8(0x12, d) /* RL D */
DEF_RL_R8(0x13, e) /* RL E */
DEF_RL_R8(0x14, h) /* RL H */
DEF_RL_R8(0x15, l) /* RL L */
DEF_RL_HLPTR(0x16) /* RL (HL) */
DEF_RL_R8(0x17, a) /* RL A */

DEF_RR_R8(0x18, b) /* RR B */
DEF_RR_R8(0x19, c) /* RR C */
DEF_RR_R8(0x1a, d) /* RR D */
DEF_RR_R8(0x1b, e) /* RR E */
DEF_RR_R8(0x1c, h) /* RR H */
DEF_RR_R8(0x1d, l) /* RR L */
DEF_RR_HLPTR(0x1e) /* RR (HL) */
DEF_RR_R8(0x1f, a) /* RR A */

DEF_SLA_R8(0x20, b) /* SLA B */
DEF_SLA_R8(0x21, c) /* SLA C */
DEF_SLA_R8(0x22, d) /* SLA D */
DEF_SLA_R8(0x23, e) /* SLA E */
DEF_SLA_R8(0x24, h) /* SLA H */
DEF_SLA_R8(0x25, l) /* SLA L */
DEF_SLA_HLPTR(0x26) /* SLA (HL) */
DEF_SLA_R8(0x27, a) /* SLA A */

DEF_SRA_R8(0x28, b) /* SRA B */
DEF_SRA_R8(0x29, c) /* SRA C */
DEF_SRA_R8(0x2a, d) /* SRA D */
DEF_SRA_R8(0x2b, e) /* SRA E */
DEF_SRA_R8(0x2c, h) /* SRA H */
DEF_SRA_R8(0x2d, l) /* SRA L */
DEF_SRA_HLPTR(0x2e) /* SRA (HL) */
DEF_SRA_R8(0x2f, a) /* SRA A */

DEF_SWAP_R8(0x30, b) /* SWAP B */
DEF_SWAP_R8(0x31, c) /* SWAP C */
DEF_SWAP_R8(0x32, d) /* SWAP D */
DEF_SWAP_R8(0x33, e) /* SWAP E */
DEF_SWAP_R8(0x34, h) /* SWAP H */
DEF_SWAP_R8(0x35, l) /* SWAP L */
DEF_SWAP_HLPTR(0x36) /* SWAP (HL) */
DEF_SWAP_R8(0x37, a) /* SWAP A */

DEF_SRL_R8(0x38, b) /* SRL B */
DEF_SRL_R8(0x39, c) /* SRL C */
DEF_SRL_R8(0x3a, d) /* SRL D */
DEF_SRL_R8(0x3b, e) /* SRL E */
DEF_SRL_R8(0x3c, h) /* SRL H */
DEF_SRL_R8(0x3d, l) /* SRL L */
DEF_SRL_HLPTR(0x3e) /* SRL (HL) */
DEF_SRL_R8(0x3f, a) /* SRL A */

DEF_BIT_U3_R8(0x40, 0, b) /* BIT 0, B */
DEF_BIT_U3_R8(0x41, 0, c) /* BIT 0, C */
DEF_BIT_U3_R8(0x42, 0, d) /* BIT 0, D */
DEF_BIT_U3_R8(0x43, 0, e) /* BIT 0, E */
DEF_BIT_U3_R8(0x44, 0, h) /* BIT 0, H */
DEF_BIT_U3_R8(0x45, 0, l) /* BIT 0, L */
DEF_BIT_U3_HLPTR(0x46, 0) /* BIT 0, (HL) */
DEF_BIT_U3_R8(0x47, 0, a) /* BIT 0, A */

DEF_BIT_U3_R8(0x48, 1, b) /* BIT 1, B */
DEF_BIT_U3_R8(0x49, 1, c) /* BIT 1, C */
DEF_BIT_U3_R8(0x4a, 1, d) /* BIT 1, D */
DEF_BIT_U3_R8(0x4b, 1, e) /* BIT 1, E */
DEF_BIT_U3_R8(0x4c, 1, h) /* BIT 1, H */
DEF_BIT_U3_R8(0x4d, 1, l) /* BIT 1, L */
DEF_BIT_U3_HLPTR(0x4e, 1) /* BIT 1, (HL) */
DEF_BIT_U3_R8(0x4f, 1, a) /* BIT 1, A */

DEF_BIT_U3_R8(0x50, 2, b) /* BIT 2, B */
DEF_BIT_U3_R8(0x51, 2, c) /* BIT 2, C */
DEF_BIT_U3_R8(0x52, 2, d) /* BIT 2, D */
DEF_BIT_U3_R8(0x53, 2, e) /* BIT 2, E */
DEF_BIT_U3_R8(0x54, 2, h) /* BIT 2, H */
DEF_BIT_U3_R8(0x55, 2, l) /* BIT 2, L */
DEF_BIT_U3_HLPTR(0x56, 2) /* BIT 2, (HL) */
DEF_BIT_U3_R8(0x57, 2, a) /* BIT 2, A */

DEF_BIT_U3_R8(0x58, 3, b) /* BIT 3, B */
DEF_BIT_U3_R8(0x59, 3, c) /* BIT 3, C */
DEF_BIT_U3_R8(0x5a, 3, d) /* BIT 3, D */
DEF_BIT_U3_R8(0x5b, 3, e) /* BIT 3, E */
DEF_BIT_U3_R8(0x5c, 3, h) /* BIT 3, H */
DEF_BIT_U3_R8(0x5d, 3, l) /* BIT 3, L */
DEF_BIT_U3_HLPTR(0x5e, 3) /* BIT 3, (HL) */
DEF_BIT_U3_R8(0x5f, 3, a) /* BIT 3, A */

DEF_BIT_U3_R8(0x60, 4, b) /* BIT 4, B */
DEF_BIT_U3_R8(0x61, 4, c) /* BIT 4, C */
DEF_BIT_U3_R8(0x62, 4, d) /* BIT 4, D */
DEF_BIT_U3_R8(0x63, 4, e) /* BIT 4, E */
DEF_BIT_U3_R8(0x64, 4, h) /* BIT 4, H */
DEF_BIT_U3_R8(0x65, 4, l) /* BIT 4, L */
DEF_BIT_U3_HLPTR(0x66, 4) /* BIT 4, (HL) */
DEF_BIT_U3_R8(0x67, 4, a) /* BIT 4, A */

DEF_BIT_U3_R8(0x68, 5, b) /* BIT 5, B */
DEF_BIT_U3_R8(0x69, 5, c) /* BIT 5, C */
DEF_BIT_U3_R8(0x6a, 5, d) /* BIT 5, D */
DEF_BIT_U3_R8(0x6b, 5, e) /* BIT 5, E */
DEF_BIT_U3_R8(0x6c, 5, h) /* BIT 5, H */
DEF_BIT_U3_R8(0x6d, 5, l) /* BIT 5, L */
DEF_BIT_U3_HLPTR(0x6e, 5) /* BIT 5, (HL) */
DEF_BIT_U3_R8(0x6f, 5, a) /* BIT 5, A */

DEF_BIT_U3_R8(0x70, 6, b) /* BIT 6, B */
DEF_BIT_U3_R8(0x71, 6, c) /* BIT 6, C */
DEF_BIT_U3_R8(0x72, 6, d) /* BIT 6, D */
DEF_BIT_U3_R8(0x73, 6, e) /* BIT 6, E */
DEF_BIT_U3_R8(0x74, 6, h) /* BIT 6, H */
DEF_BIT_U3_R8(0x75, 6, l) /* BIT 6, L */
DEF_BIT_U3_HLPTR(0x76, 6) /* BIT 6, (HL) */
DEF_BIT_U3_R8(0x77, 6, a) /* BIT 6, A */

DEF_BIT_U3_R8(0x78, 7, b) /* BIT 7, B */
DEF_BIT_U3_R8(0x79, 7, c) /* BIT 7, C */
DEF_BIT_U3_R8(0x7a, 7, d) /* BIT 7, D */
DEF_BIT_U3_R8(0x7b, 7, e) /* BIT 7, E */
DEF_BIT_U3_R8(0x7c, 7, h) /* BIT 7, H */
DEF_BIT_U3_R8(0x7d, 7, l) /* BIT 7, L */
DEF_BIT_U3_HLPTR(0x7e, 7) /* BIT 7, (HL) */
DEF_BIT_U3_R8(0x7f, 7, a) /* BIT 7, A */

DEF_RES_U3_R8(0x80, 0, b) /* RES 0, B */
DEF_RES_U3_R8(0x81, 0, c) /* RES 0, C */
DEF_RES_U3_R8(0x82, 0, d) /* RES 0, D */
DEF_RES_U3_R8(0x83, 0, e) /* RES 0, E */
DEF_RES_U3_R8(0x84, 0, h) /* RES 0, H */
DEF_RES_U3_R8(0x85, 0, l) /* RES 0, L */
DEF_RES_U3_HLPTR(0x86, 0) /* RES 0, (HL) */
DEF_RES_U3_R8(0x87, 0, a) /* RES 0, A */

DEF_RES_U3_R8(0x88, 1, b) /* RES 1, B */
DEF_RES_U3_R8(0x89, 1, c) /* RES 1, C */
DEF_RES_U3_R8(0x8a, 1, d) /* RES 1, D */
DEF_RES_U3_R8(0x8b, 1, e) /* RES 1, E */
DEF_RES_U3_R8(0x8c, 1, h) /* RES 1, H */
DEF_RES_U3_R8(0x8d, 1, l) /* RES 1, L */
DEF_RES_U3_HLPTR(0x8e, 1) /* RES 1, (HL) */
DEF_RES_U3_R8(0x8f, 1, a) /* RES 1, A */

DEF_RES_U3_R8(0x90, 2, b) /* RES 2, B */
DEF_RES_U3_R8(0x91, 2, c) /* RES 2, C */
DEF_RES_U3_R8(0x92, 2, d) /* RES 2, D */
DEF_RES_U3_R8(0x93, 2, e) /* RES 2, E */
DEF_RES_U3_R8(0x94, 2, h) /* RES 2, H */
DEF_RES_U3_R8(0x95, 2, l) /* RES 2, L */
DEF_RES_U3_HLPTR(0x96, 2) /* RES 2, (HL) */
DEF_RES_U3_R8(0x97, 2, a) /* RES 2, A */

DEF_RES_U3_R8(0x98, 3, b) /* RES 3, B */
DEF_RES_U3_R8(0x99, 3, c) /* RES 3, C */
DEF_RES_U3_R8(0x9a, 3, d) /* RES 3, D */
DEF_RES_U3_R8(0x9b, 3, e) /* RES 3, E */
DEF_RES_U3_R8(0x9c, 3, h) /* RES 3, H */
DEF_RES_U3_R8(0x9d, 3, l) /* RES 3, L */
DEF_RES_U3_HLPTR(0x9e, 3) /* RES 3, (HL) */
DEF_RES_U3_R8(0x9f, 3, a) /* RES 3, A */

DEF_RES_U3_R8(0xa0, 4, b) /* RES 4, B */
DEF_RES_U3_R8(0xa1, 4, c) /* RES 4, C */
DEF_RES_U3_R8(0xa2, 4, d) /* RES 4, D */
DEF_RES_U3_R8(0xa3, 4, e) /* RES 4, E */
DEF_RES_U3_R8(0xa4, 4, h) /* RES 4, H */
DEF_RES_U3_R8(0xa5, 4, l) /* RES 4, L */
DEF_RES_U3_HLPTR(0xa6, 4) /* RES 4, (HL) */
DEF_RES_U3_R8(0xa7, 4, a) /* RES 4, A */

DEF_RES_U3_R8(0xa8, 5, b) /* RES 5, B */
DEF_RES_U3_R8(0xa9, 5, c) /* RES 5, C */
DEF_RES_U3_R8(0xaa, 5, d) /* RES 5, D */
DEF_RES_U3_R8(0xab, 5, e) /* RES 5, E */
DEF_RES_U3_R8(0xac, 5, h) /* RES 5, H */
DEF_RES_U3_R8(0xad, 5, l) /* RES 5, L */
DEF_RES_U3_HLPTR(0xae, 5) /* RES 5, (HL) */
DEF_RES_U3_R8(0xaf, 5, a) /* RES 5, A */

DEF_RES_U3_R8(0xb0, 6, b) /* RES 6, B */
DEF_RES_U3_R8(0xb1, 6, c) /* RES 6, C */
DEF_RES_U3_R8(0xb2, 6, d) /* RES 6, D */
DEF_RES_U3_R8(0xb3, 6, e) /* RES 6, E */
DEF_RES_U3_R8(0xb4, 6, h) /* RES 6, H */
DEF_RES_U3_R8(0xb5, 6, l) /* RES 6, L */
DEF_RES_U3_HLPTR(0xb6, 6) /* RES 6, (HL) */
DEF_RES_U3_R8(0xb7, 6, a) /* RES 6, A */

DEF_RES_U3_R8(0xb8, 7, b) /* RES 7, B */
DEF_RES_U3_R8(0xb9, 7, c) /* RES 7, C */
DEF_RES_U3_R8(0xba, 7, d) /* RES 7, D */
DEF_RES_U3_R8(0xbb, 7, e) /* RES 7, E */
DEF_RES_U3_R8(0xbc, 7, h) /* RES 7, H */
DEF_RES_U3_R8(0xbd, 7, l) /* RES 7, L */
DEF_RES_U3_HLPTR(0xbe, 7) /* RES 7, (HL) */
DEF_RES_U3_R8(0xbf, 7, a) /* RES 7, A */

DEF_SET_U3_R8(0xc0, 0, b) /* SET 0, B */
DEF_SET_U3_R8(0xc1, 0, c) /* SET 0, C */
DEF_SET_U3_R8(0xc2, 0, d) /* SET 0, D */
DEF_SET_U3_R8(0xc3, 0, e) /* SET 0, E */
DEF_SET_U3_R8(0xc4, 0, h) /* SET 0, H */
DEF_SET_U3_R8(0xc5, 0, l) /* SET 0, L */
DEF_SET_U3_HLPTR(0xc6, 0) /* SET 0, (HL) */
DEF_SET_U3_R8(0xc7, 0, a) /* SET 0, A */

DEF_SET_U3_R8(0xc8, 1, b) /* SET 1, B */
DEF_SET_U3_R8(0xc9, 1, c) /* SET 1, C */
DEF_SET_U3_R8(0xca, 1, d) /* SET 1, D */
DEF_SET_U3_R8(0xcb, 1, e) /* SET 1, E */
DEF_SET_U3_R8(0xcc, 1, h) /* SET 1, H */
DEF_SET_U3_R8(0xcd, 1, l) /* SET 1, L */
DEF_SET_U3_HLPTR(0xce, 1) /* SET 1, (HL) */
DEF_SET_U3_R8(0xcf, 1, a) /* SET 1, A */

DEF_SET_U3_R8(0xd0, 2, b) /* SET 2, B */
DEF_SET_U3_R8(0xd1, 2, c) /* SET 2, C */
DEF_SET_U3_R8(0xd2, 2, d) /* SET 2, D */
DEF_SET_U3_R8(0xd3, 2, e) /* SET 2, E */
DEF_SET_U3_R8(0xd4, 2, h) /* SET 2, H */
DEF_SET_U3_R8(0xd5, 2, l) /* SET 2, L */
DEF_SET_U3_HLPTR(0xd6, 2) /* SET 2, (HL) */
DEF_SET_U3_R8(0xd7, 2, a) /* SET 2, A */

DEF_SET_U3_R8(0xd8, 3, b) /* SET 3, B */
DEF_SET_U3_R8(0xd9, 3, c) /* SET 3, C */
DEF_SET_U3_R8(0xda, 3, d) /* SET 3, D */
DEF_SET_U3_R8(0xdb, 3, e) /* SET 3, E */
DEF_SET_U3_R8(0xdc, 3, h) /* SET 3, H */
DEF_SET_U3_R8(0xdd, 3, l) /* SET 3, L */
DEF_SET_U3_HLPTR(0xde, 3) /* SET 3, (HL) */
DEF_SET_U3_R8(0xdf, 3, a) /* SET 3, A */

DEF_SET_U3_R8(0xe0, 4, b) /* SET 4, B */
DEF_SET_U3_R8(0xe1, 4, c) /* SET 4, C */
DEF_SET_U3_R8(0xe2, 4, d) /* SET 4, D */
DEF_SET_U3_R8(0xe3, 4, e) /* SET 4, E */
DEF_SET_U3_R8(0xe4, 4, h) /* SET 4, H */
DEF_SET_U3_R8(0xe5, 4, l) /* SET 4, L */
DEF_SET_U3_HLPTR(0xe6, 4) /* SET 4, (HL) */
DEF_SET_U3_R8(0xe7, 4, a) /* SET 4, A */

DEF_SET_U3_R8(0xe8, 5, b) /* SET 5, B */
DEF_SET_U3_R8(0xe9, 5, c) /* SET 5, C */
DEF_SET_U3_R8(0xea, 5, d) /* SET 5, D */
DEF_SET_U3_R8(0xeb, 5, e) /* SET 5, E */
DEF_SET_U3_R8(0xec, 5, h) /* SET 5, H */
DEF_SET_U3_R8(0xed, 5, l) /* SET 5, L */
DEF_SET_U3_HLPTR(0xee, 5) /* SET 5, (HL) */
DEF_SET_U3_R8(0xef, 5, a) /* SET 5, A */

DEF_SET_U3_R8(0xf0, 6, b) /* SET 6, B */
DEF_SET_U3_R8(0xf1, 6, c) /* SET 6, C */
DEF_SET_U3_R8(0xf2, 6, d) /* SET 6, D */
DEF_SET_U3_R8(0xf3, 6, e) /* SET 6, E */
DEF_SET_U3_R8(0xf4, 6, h) /* SET 6, H */
DEF_SET_U3_R8(0xf5, 6, l) /* SET 6, L */
DEF_SET_U3_HLPTR(0xf6, 6) /* SET 6, (HL) */
DEF_SET_U3_R8(0xf7, 6, a) /* SET 6, A */

DEF_SET_U3_R8(0xf8, 7, b) /* SET 7, B */
DEF_SET_U3_R8(0xf9, 7, c) /* SET 7, C */
DEF_SET_U3_R8(0xfa, 7, d) /* SET 7, D */
DEF_SET_U3_R8(0xfb, 7, e) /* SET 7, E */
DEF_SET_U3_R8(0xfc, 7, h) /* SET 7, H */
DEF_SET_U3_R8(0xfd, 7, l) /* SET 7, L */
DEF_SET_U3_HLPTR(0xfe, 7) /* SET 7, (HL) */
DEF_SET_U3_R8(0xff, 7, a) /* SET 7, A */

/* ----  control/misc ---- */
DEF_IMPLIED(0x00, nop, /* nothing */, 4)
DEF_IMPLIED(0xf3, di, cpu->ime = cpu->ime_delay = 0;, 4)
DEF_IMPLIED(0xfb, ei, cpu->ime_delay = 2;, 4)
DEF_HALT(0x76)

void decode_and_execute(CPU *cpu, uint8_t op) {
    switch (op) {
        /* ---- x8/alu ---- */
        case 0x04: op_0x04_i_b(cpu); break;
        case 0x05: op_0x05_d_b(cpu); break;
        case 0x0C: op_0x0c_i_c(cpu); break;
        case 0x0D: op_0x0d_d_c(cpu); break;
        case 0x14: op_0x14_i_d(cpu); break;
        case 0x15: op_0x15_d_d(cpu); break;
        case 0x1C: op_0x1c_i_e(cpu); break;
        case 0x1D: op_0x1d_d_e(cpu); break;
        case 0x24: op_0x24_i_h(cpu); break;
        case 0x25: op_0x25_d_h(cpu); break;
        case 0x27: op_0x27_daa(cpu); break;
        case 0x2C: op_0x2c_i_l(cpu); break;
        case 0x2D: op_0x2d_d_l(cpu); break;
        case 0x2F: op_0x2f_cpl(cpu); break;
        case 0x34: op_0x34_i_hlptr(cpu); break;
        case 0x35: op_0x35_d_hlptr(cpu); break;
        case 0x37: op_0x37_scf(cpu); break;
        case 0x3C: op_0x3c_i_a(cpu); break;
        case 0x3D: op_0x3d_d_a(cpu); break;
        case 0x3F: op_0x3f_ccf(cpu); break;
        case 0x80: op_0x80_add_a_b(cpu); break;
        case 0x81: op_0x81_add_a_c(cpu); break;
        case 0x82: op_0x82_add_a_d(cpu); break;
        case 0x83: op_0x83_add_a_e(cpu); break;
        case 0x84: op_0x84_add_a_h(cpu); break;
        case 0x85: op_0x85_add_a_l(cpu); break;
        case 0x86: op_0x86_add_a_hlptr(cpu); break;
        case 0x87: op_0x87_add_a_a(cpu); break;
        case 0x88: op_0x88_adc_a_b(cpu); break;
        case 0x89: op_0x89_adc_a_c(cpu); break;
        case 0x8A: op_0x8a_adc_a_d(cpu); break;
        case 0x8B: op_0x8b_adc_a_e(cpu); break;
        case 0x8C: op_0x8c_adc_a_h(cpu); break;
        case 0x8D: op_0x8d_adc_a_l(cpu); break;
        case 0x8E: op_0x8e_adc_a_hlptr(cpu); break;
        case 0x8F: op_0x8f_adc_a_a(cpu); break;
        case 0x90: op_0x90_sub_a_b(cpu); break;
        case 0x91: op_0x91_sub_a_c(cpu); break;
        case 0x92: op_0x92_sub_a_d(cpu); break;
        case 0x93: op_0x93_sub_a_e(cpu); break;
        case 0x94: op_0x94_sub_a_h(cpu); break;
        case 0x95: op_0x95_sub_a_l(cpu); break;
        case 0x96: op_0x96_sub_a_hlptr(cpu); break;
        case 0x97: op_0x97_sub_a_a(cpu); break;
        case 0x98: op_0x98_sbc_a_b(cpu); break;
        case 0x99: op_0x99_sbc_a_c(cpu); break;
        case 0x9A: op_0x9a_sbc_a_d(cpu); break;
        case 0x9B: op_0x9b_sbc_a_e(cpu); break;
        case 0x9C: op_0x9c_sbc_a_h(cpu); break;
        case 0x9D: op_0x9d_sbc_a_l(cpu); break;
        case 0x9E: op_0x9e_sbc_a_hlptr(cpu); break;
        case 0x9F: op_0x9f_sbc_a_a(cpu); break;
        case 0xA0: op_0xa0_and_a_b(cpu); break;
        case 0xA1: op_0xa1_and_a_c(cpu); break;
        case 0xA2: op_0xa2_and_a_d(cpu); break;
        case 0xA3: op_0xa3_and_a_e(cpu); break;
        case 0xA4: op_0xa4_and_a_h(cpu); break;
        case 0xA5: op_0xa5_and_a_l(cpu); break;
        case 0xA6: op_0xa6_and_a_hlptr(cpu); break;
        case 0xA7: op_0xa7_and_a_a(cpu); break;
        case 0xA8: op_0xa8_xor_a_b(cpu); break;
        case 0xA9: op_0xa9_xor_a_c(cpu); break;
        case 0xAA: op_0xaa_xor_a_d(cpu); break;
        case 0xAB: op_0xab_xor_a_e(cpu); break;
        case 0xAC: op_0xac_xor_a_h(cpu); break;
        case 0xAD: op_0xad_xor_a_l(cpu); break;
        case 0xAE: op_0xae_xor_a_hlptr(cpu); break;
        case 0xAF: op_0xaf_xor_a_a(cpu); break;
        case 0xB0: op_0xb0_or_a_b(cpu); break;
        case 0xB1: op_0xb1_or_a_c(cpu); break;
        case 0xB2: op_0xb2_or_a_d(cpu); break;
        case 0xB3: op_0xb3_or_a_e(cpu); break;
        case 0xB4: op_0xb4_or_a_h(cpu); break;
        case 0xB5: op_0xb5_or_a_l(cpu); break;
        case 0xB6: op_0xb6_or_a_hlptr(cpu); break;
        case 0xB7: op_0xb7_or_a_a(cpu); break;
        case 0xB8: op_0xb8_cp_a_b(cpu); break;
        case 0xB9: op_0xb9_cp_a_c(cpu); break;
        case 0xBA: op_0xba_cp_a_d(cpu); break;
        case 0xBB: op_0xbb_cp_a_e(cpu); break;
        case 0xBC: op_0xbc_cp_a_h(cpu); break;
        case 0xBD: op_0xbd_cp_a_l(cpu); break;
        case 0xBE: op_0xbe_cp_a_hlptr(cpu); break;
        case 0xBF: op_0xbf_cp_a_a(cpu); break;
        case 0xC6: op_0xc6_add_a_u8(cpu); break;
        case 0xCE: op_0xce_adc_a_u8(cpu); break;
        case 0xD6: op_0xd6_sub_a_u8(cpu); break;
        case 0xDE: op_0xde_sbc_a_u8(cpu); break;
        case 0xE6: op_0xe6_and_a_u8(cpu); break;
        case 0xEE: op_0xee_xor_a_u8(cpu); break;
        case 0xF6: op_0xf6_or_a_u8(cpu); break;
        case 0xFE: op_0xfe_cp_a_u8(cpu); break;

        /* ---- x16/alu ---- */
        case 0x03: op_0x03_i_bc(cpu); break;
        case 0x09: op_0x09_add_hl_bc(cpu); break;
        case 0x13: op_0x13_i_de(cpu); break;
        case 0x19: op_0x19_add_hl_de(cpu); break;
        case 0x23: op_0x23_i_hl(cpu); break;
        case 0x29: op_0x29_add_hl_hl(cpu); break;
        case 0x33: op_0x33_i_sp(cpu); break;
        case 0x39: op_0x39_add_hl_sp(cpu); break;
        case 0x0B: op_0x0b_d_bc(cpu); break;
        case 0x1B: op_0x1b_d_de(cpu); break;
        case 0x2B: op_0x2b_d_hl(cpu); break;
        case 0x3B: op_0x3b_d_sp(cpu); break;
        case 0xE8: op_0xe8_add_sp_i8(cpu); break;
        case 0xF8: op_0xf8_ld_hl_sp_i8(cpu); break;

        /* ---- x8/lsm ---- */
        case 0x02: op_0x02_ld_bcptr_a(cpu); break;
        case 0x06: op_0x06_ld_b_u8(cpu); break;
        case 0x0A: op_0x0a_ld_a_bcptr(cpu); break;
        case 0x0E: op_0x0e_ld_c_u8(cpu); break;
        case 0x12: op_0x12_ld_deptr_a(cpu); break;
        case 0x16: op_0x16_ld_d_u8(cpu); break;
        case 0x1A: op_0x1a_ld_a_deptr(cpu); break;
        case 0x1E: op_0x1e_ld_e_u8(cpu); break;
        case 0x22: op_0x22_ld_hlptr_i_a(cpu); break;
        case 0x26: op_0x26_ld_h_u8(cpu); break;
        case 0x2A: op_0x2a_ld_a_hlptr_i(cpu); break;
        case 0x2E: op_0x2e_ld_l_u8(cpu); break;
        case 0x32: op_0x32_ld_hlptr_d_a(cpu); break;
        case 0x36: op_0x36_ld_hlptr_u8(cpu); break;
        case 0x3A: op_0x3a_ld_a_hlptr_d(cpu); break;
        case 0x3E: op_0x3e_ld_a_u8(cpu); break;
        case 0x40: op_0x40_ld_b_b(cpu); break;
        case 0x41: op_0x41_ld_b_c(cpu); break;
        case 0x42: op_0x42_ld_b_d(cpu); break;
        case 0x43: op_0x43_ld_b_e(cpu); break;
        case 0x44: op_0x44_ld_b_h(cpu); break;
        case 0x45: op_0x45_ld_b_l(cpu); break;
        case 0x46: op_0x46_ld_b_hlptr(cpu); break;
        case 0x47: op_0x47_ld_b_a(cpu); break;
        case 0x48: op_0x48_ld_c_b(cpu); break;
        case 0x49: op_0x49_ld_c_c(cpu); break;
        case 0x4A: op_0x4a_ld_c_d(cpu); break;
        case 0x4B: op_0x4b_ld_c_e(cpu); break;
        case 0x4C: op_0x4c_ld_c_h(cpu); break;
        case 0x4D: op_0x4d_ld_c_l(cpu); break;
        case 0x4E: op_0x4e_ld_c_hlptr(cpu); break;
        case 0x4F: op_0x4f_ld_c_a(cpu); break;
        case 0x50: op_0x50_ld_d_b(cpu); break;
        case 0x51: op_0x51_ld_d_c(cpu); break;
        case 0x52: op_0x52_ld_d_d(cpu); break;
        case 0x53: op_0x53_ld_d_e(cpu); break;
        case 0x54: op_0x54_ld_d_h(cpu); break;
        case 0x55: op_0x55_ld_d_l(cpu); break;
        case 0x56: op_0x56_ld_d_hlptr(cpu); break;
        case 0x57: op_0x57_ld_d_a(cpu); break;
        case 0x58: op_0x58_ld_e_b(cpu); break;
        case 0x59: op_0x59_ld_e_c(cpu); break;
        case 0x5A: op_0x5a_ld_e_d(cpu); break;
        case 0x5B: op_0x5b_ld_e_e(cpu); break;
        case 0x5C: op_0x5c_ld_e_h(cpu); break;
        case 0x5D: op_0x5d_ld_e_l(cpu); break;
        case 0x5E: op_0x5e_ld_e_hlptr(cpu); break;
        case 0x5F: op_0x5f_ld_e_a(cpu); break;
        case 0x60: op_0x60_ld_h_b(cpu); break;
        case 0x61: op_0x61_ld_h_c(cpu); break;
        case 0x62: op_0x62_ld_h_d(cpu); break;
        case 0x63: op_0x63_ld_h_e(cpu); break;
        case 0x64: op_0x64_ld_h_h(cpu); break;
        case 0x65: op_0x65_ld_h_l(cpu); break;
        case 0x66: op_0x66_ld_h_hlptr(cpu); break;
        case 0x67: op_0x67_ld_h_a(cpu); break;
        case 0x68: op_0x68_ld_l_b(cpu); break;
        case 0x69: op_0x69_ld_l_c(cpu); break;
        case 0x6A: op_0x6a_ld_l_d(cpu); break;
        case 0x6B: op_0x6b_ld_l_e(cpu); break;
        case 0x6C: op_0x6c_ld_l_h(cpu); break;
        case 0x6D: op_0x6d_ld_l_l(cpu); break;
        case 0x6E: op_0x6e_ld_l_hlptr(cpu); break;
        case 0x6F: op_0x6f_ld_l_a(cpu); break;
        case 0x70: op_0x70_ld_hlptr_b(cpu); break;
        case 0x71: op_0x71_ld_hlptr_c(cpu); break;
        case 0x72: op_0x72_ld_hlptr_d(cpu); break;
        case 0x73: op_0x73_ld_hlptr_e(cpu); break;
        case 0x74: op_0x74_ld_hlptr_h(cpu); break;
        case 0x75: op_0x75_ld_hlptr_l(cpu); break;
        case 0x77: op_0x77_ld_hlptr_a(cpu); break;
        case 0x78: op_0x78_ld_a_b(cpu); break;
        case 0x79: op_0x79_ld_a_c(cpu); break;
        case 0x7A: op_0x7a_ld_a_d(cpu); break;
        case 0x7B: op_0x7b_ld_a_e(cpu); break;
        case 0x7C: op_0x7c_ld_a_h(cpu); break;
        case 0x7D: op_0x7d_ld_a_l(cpu); break;
        case 0x7E: op_0x7e_ld_a_hlptr(cpu); break;
        case 0x7F: op_0x7f_ld_a_a(cpu); break;
        case 0xE0: op_0xe0_ld_ff00u8ptr_a(cpu); break;
        case 0xE2: op_0xe2_ld_ff00cptr_a(cpu); break;
        case 0xEA: op_0xea_ld_u16ptr_a(cpu); break;
        case 0xF0: op_0xf0_ld_a_ff00u8ptr(cpu); break;
        case 0xF2: op_0xf2_ld_a_ff00cptr(cpu); break;
        case 0xFA: op_0xfa_ld_a_u16ptr(cpu); break;

        /* ---- x16/lsm ---- */
        case 0x01: op_0x01_ld_bc_u16(cpu); break;
        case 0x08: op_0x08_ld_u16ptr_sp(cpu); break;
        case 0x11: op_0x11_ld_de_u16(cpu); break;
        case 0x21: op_0x21_ld_hl_u16(cpu); break;
        case 0x31: op_0x31_ld_sp_u16(cpu); break;
        case 0xC1: op_0xc1_pop_bc(cpu); break;
        case 0xD1: op_0xd1_pop_de(cpu); break;
        case 0xE1: op_0xe1_pop_hl(cpu); break;
        case 0xF1: op_0xf1_pop_af(cpu); break;
        case 0xC5: op_0xc5_push_bc(cpu); break;
        case 0xD5: op_0xd5_push_de(cpu); break;
        case 0xE5: op_0xe5_push_hl(cpu); break;
        case 0xF5: op_0xf5_push_af(cpu); break;
        case 0xF9: op_0xf9_ld_sp_hl(cpu); break;

        /* ---- ctrl/br ---- */
        case 0x18: op_0x18_jr_u8(cpu); break;
        case 0x20: op_0x20_jr_nz_u8(cpu); break;
        case 0x28: op_0x28_jr_z_u8(cpu); break;
        case 0x30: op_0x30_jr_nc_u8(cpu); break;
        case 0x38: op_0x38_jr_c_u8(cpu); break;
        case 0xC0: op_0xc0_ret_nz(cpu); break;
        case 0xC2: op_0xc2_jp_nz_u16(cpu); break;
        case 0xC3: op_0xc3_jp(cpu); break;
        case 0xC4: op_0xc4_call_nz_u16(cpu); break;
        case 0xC7: op_0xc7_rst_0x0000(cpu); break;
        case 0xC8: op_0xc8_ret_z(cpu); break;
        case 0xC9: op_0xc9_ret(cpu); break;
        case 0xCA: op_0xca_jp_z_u16(cpu); break;
        case 0xCC: op_0xcc_call_z_u16(cpu); break;
        case 0xCD: op_0xcd_call_u16(cpu); break;
        case 0xCF: op_0xcf_rst_0x0008(cpu); break;
        case 0xD0: op_0xd0_ret_nc(cpu); break;
        case 0xD2: op_0xd2_jp_nc_u16(cpu); break;
        case 0xD4: op_0xd4_call_nc_u16(cpu); break;
        case 0xD7: op_0xd7_rst_0x0010(cpu); break;
        case 0xD8: op_0xd8_ret_c(cpu); break;
        case 0xD9: op_0xd9_reti(cpu); break;
        case 0xDA: op_0xda_jp_c_u16(cpu); break;
        case 0xDC: op_0xdc_call_c_u16(cpu); break;
        case 0xDF: op_0xdf_rst_0x0018(cpu); break;
        case 0xE7: op_0xe7_rst_0x0020(cpu); break;
        case 0xE9: op_0xe9_jp_hl(cpu); break;
        case 0xEF: op_0xef_rst_0x0028(cpu); break;
        case 0xF7: op_0xf7_rst_0x0030(cpu); break;
        case 0xFF: op_0xff_rst_0x0038(cpu); break;

        /* ---- ctrl/misc ---- */
        case 0x00: op_0x00_nop(cpu); break;
        case 0x10: op_stop(cpu); break;
        case 0x76: op_0x76_halt(cpu); break;
        case 0xF3: op_0xf3_di(cpu); break;
        case 0xFB: op_0xfb_ei(cpu); break;

        /* ---- x8/rsb ---- */
        case 0x07: op_rlca(cpu); break;
        case 0x17: op_rla(cpu); break;
        case 0x0F: op_rrca(cpu); break;
        case 0x1F: op_rra(cpu); break;
        case 0xCB: {
            uint8_t cb_op =
                mem_read(cpu->pc++); /* fetch the CB‐opcode (the second byte) and advance PC */

            switch (cb_op) {
                case 0x00: op_0x00_rlc_b(cpu); break;
                case 0x01: op_0x01_rlc_c(cpu); break;
                case 0x02: op_0x02_rlc_d(cpu); break;
                case 0x03: op_0x03_rlc_e(cpu); break;
                case 0x04: op_0x04_rlc_h(cpu); break;
                case 0x05: op_0x05_rlc_l(cpu); break;
                case 0x06: op_0x06_rlc_hlptr(cpu); break;
                case 0x07: op_0x07_rlc_a(cpu); break;
                case 0x08: op_0x08_rrc_b(cpu); break;
                case 0x09: op_0x09_rrc_c(cpu); break;
                case 0x0A: op_0x0a_rrc_d(cpu); break;
                case 0x0B: op_0x0b_rrc_e(cpu); break;
                case 0x0C: op_0x0c_rrc_h(cpu); break;
                case 0x0D: op_0x0d_rrc_l(cpu); break;
                case 0x0E: op_0x0e_rrc_hlptr(cpu); break;
                case 0x0F: op_0x0f_rrc_a(cpu); break;
                case 0x10: op_0x10_rl_b(cpu); break;
                case 0x11: op_0x11_rl_c(cpu); break;
                case 0x12: op_0x12_rl_d(cpu); break;
                case 0x13: op_0x13_rl_e(cpu); break;
                case 0x14: op_0x14_rl_h(cpu); break;
                case 0x15: op_0x15_rl_l(cpu); break;
                case 0x16: op_0x16_rl_hlptr(cpu); break;
                case 0x17: op_0x17_rl_a(cpu); break;
                case 0x18: op_0x18_rr_b(cpu); break;
                case 0x19: op_0x19_rr_c(cpu); break;
                case 0x1A: op_0x1a_rr_d(cpu); break;
                case 0x1B: op_0x1b_rr_e(cpu); break;
                case 0x1C: op_0x1c_rr_h(cpu); break;
                case 0x1D: op_0x1d_rr_l(cpu); break;
                case 0x1E: op_0x1e_rr_hlptr(cpu); break;
                case 0x1F: op_0x1f_rr_a(cpu); break;
                case 0x20: op_0x20_sla_b(cpu); break;
                case 0x21: op_0x21_sla_c(cpu); break;
                case 0x22: op_0x22_sla_d(cpu); break;
                case 0x23: op_0x23_sla_e(cpu); break;
                case 0x24: op_0x24_sla_h(cpu); break;
                case 0x25: op_0x25_sla_l(cpu); break;
                case 0x26: op_0x26_sla_hlptr(cpu); break;
                case 0x27: op_0x27_sla_a(cpu); break;
                case 0x28: op_0x28_sra_b(cpu); break;
                case 0x29: op_0x29_sra_c(cpu); break;
                case 0x2A: op_0x2a_sra_d(cpu); break;
                case 0x2B: op_0x2b_sra_e(cpu); break;
                case 0x2C: op_0x2c_sra_h(cpu); break;
                case 0x2D: op_0x2d_sra_l(cpu); break;
                case 0x2E: op_0x2e_sra_hlptr(cpu); break;
                case 0x2F: op_0x2f_sra_a(cpu); break;
                case 0x30: op_0x30_swap_b(cpu); break;
                case 0x31: op_0x31_swap_c(cpu); break;
                case 0x32: op_0x32_swap_d(cpu); break;
                case 0x33: op_0x33_swap_e(cpu); break;
                case 0x34: op_0x34_swap_h(cpu); break;
                case 0x35: op_0x35_swap_l(cpu); break;
                case 0x36: op_0x36_swap_hlptr(cpu); break;
                case 0x37: op_0x37_swap_a(cpu); break;
                case 0x38: op_0x38_srl_b(cpu); break;
                case 0x39: op_0x39_srl_c(cpu); break;
                case 0x3A: op_0x3a_srl_d(cpu); break;
                case 0x3B: op_0x3b_srl_e(cpu); break;
                case 0x3C: op_0x3c_srl_h(cpu); break;
                case 0x3D: op_0x3d_srl_l(cpu); break;
                case 0x3E: op_0x3e_srl_hlptr(cpu); break;
                case 0x3F: op_0x3f_srl_a(cpu); break;
                case 0x40: op_0x40_bit_0_b(cpu); break;
                case 0x41: op_0x41_bit_0_c(cpu); break;
                case 0x42: op_0x42_bit_0_d(cpu); break;
                case 0x43: op_0x43_bit_0_e(cpu); break;
                case 0x44: op_0x44_bit_0_h(cpu); break;
                case 0x45: op_0x45_bit_0_l(cpu); break;
                case 0x46: op_0x46_bit_0_hlptr(cpu); break;
                case 0x47: op_0x47_bit_0_a(cpu); break;
                case 0x48: op_0x48_bit_1_b(cpu); break;
                case 0x49: op_0x49_bit_1_c(cpu); break;
                case 0x4A: op_0x4a_bit_1_d(cpu); break;
                case 0x4B: op_0x4b_bit_1_e(cpu); break;
                case 0x4C: op_0x4c_bit_1_h(cpu); break;
                case 0x4D: op_0x4d_bit_1_l(cpu); break;
                case 0x4E: op_0x4e_bit_1_hlptr(cpu); break;
                case 0x4F: op_0x4f_bit_1_a(cpu); break;
                case 0x50: op_0x50_bit_2_b(cpu); break;
                case 0x51: op_0x51_bit_2_c(cpu); break;
                case 0x52: op_0x52_bit_2_d(cpu); break;
                case 0x53: op_0x53_bit_2_e(cpu); break;
                case 0x54: op_0x54_bit_2_h(cpu); break;
                case 0x55: op_0x55_bit_2_l(cpu); break;
                case 0x56: op_0x56_bit_2_hlptr(cpu); break;
                case 0x57: op_0x57_bit_2_a(cpu); break;
                case 0x58: op_0x58_bit_3_b(cpu); break;
                case 0x59: op_0x59_bit_3_c(cpu); break;
                case 0x5A: op_0x5a_bit_3_d(cpu); break;
                case 0x5B: op_0x5b_bit_3_e(cpu); break;
                case 0x5C: op_0x5c_bit_3_h(cpu); break;
                case 0x5D: op_0x5d_bit_3_l(cpu); break;
                case 0x5E: op_0x5e_bit_3_hlptr(cpu); break;
                case 0x5F: op_0x5f_bit_3_a(cpu); break;
                case 0x60: op_0x60_bit_4_b(cpu); break;
                case 0x61: op_0x61_bit_4_c(cpu); break;
                case 0x62: op_0x62_bit_4_d(cpu); break;
                case 0x63: op_0x63_bit_4_e(cpu); break;
                case 0x64: op_0x64_bit_4_h(cpu); break;
                case 0x65: op_0x65_bit_4_l(cpu); break;
                case 0x66: op_0x66_bit_4_hlptr(cpu); break;
                case 0x67: op_0x67_bit_4_a(cpu); break;
                case 0x68: op_0x68_bit_5_b(cpu); break;
                case 0x69: op_0x69_bit_5_c(cpu); break;
                case 0x6A: op_0x6a_bit_5_d(cpu); break;
                case 0x6B: op_0x6b_bit_5_e(cpu); break;
                case 0x6C: op_0x6c_bit_5_h(cpu); break;
                case 0x6D: op_0x6d_bit_5_l(cpu); break;
                case 0x6E: op_0x6e_bit_5_hlptr(cpu); break;
                case 0x6F: op_0x6f_bit_5_a(cpu); break;
                case 0x70: op_0x70_bit_6_b(cpu); break;
                case 0x71: op_0x71_bit_6_c(cpu); break;
                case 0x72: op_0x72_bit_6_d(cpu); break;
                case 0x73: op_0x73_bit_6_e(cpu); break;
                case 0x74: op_0x74_bit_6_h(cpu); break;
                case 0x75: op_0x75_bit_6_l(cpu); break;
                case 0x76: op_0x76_bit_6_hlptr(cpu); break;
                case 0x77: op_0x77_bit_6_a(cpu); break;
                case 0x78: op_0x78_bit_7_b(cpu); break;
                case 0x79: op_0x79_bit_7_c(cpu); break;
                case 0x7A: op_0x7a_bit_7_d(cpu); break;
                case 0x7B: op_0x7b_bit_7_e(cpu); break;
                case 0x7C: op_0x7c_bit_7_h(cpu); break;
                case 0x7D: op_0x7d_bit_7_l(cpu); break;
                case 0x7E: op_0x7e_bit_7_hlptr(cpu); break;
                case 0x7F: op_0x7f_bit_7_a(cpu); break;
                case 0x80: op_0x80_res_0_b(cpu); break;
                case 0x81: op_0x81_res_0_c(cpu); break;
                case 0x82: op_0x82_res_0_d(cpu); break;
                case 0x83: op_0x83_res_0_e(cpu); break;
                case 0x84: op_0x84_res_0_h(cpu); break;
                case 0x85: op_0x85_res_0_l(cpu); break;
                case 0x86: op_0x86_res_0_hlptr(cpu); break;
                case 0x87: op_0x87_res_0_a(cpu); break;
                case 0x88: op_0x88_res_1_b(cpu); break;
                case 0x89: op_0x89_res_1_c(cpu); break;
                case 0x8A: op_0x8a_res_1_d(cpu); break;
                case 0x8B: op_0x8b_res_1_e(cpu); break;
                case 0x8C: op_0x8c_res_1_h(cpu); break;
                case 0x8D: op_0x8d_res_1_l(cpu); break;
                case 0x8E: op_0x8e_res_1_hlptr(cpu); break;
                case 0x8F: op_0x8f_res_1_a(cpu); break;
                case 0x90: op_0x90_res_2_b(cpu); break;
                case 0x91: op_0x91_res_2_c(cpu); break;
                case 0x92: op_0x92_res_2_d(cpu); break;
                case 0x93: op_0x93_res_2_e(cpu); break;
                case 0x94: op_0x94_res_2_h(cpu); break;
                case 0x95: op_0x95_res_2_l(cpu); break;
                case 0x96: op_0x96_res_2_hlptr(cpu); break;
                case 0x97: op_0x97_res_2_a(cpu); break;
                case 0x98: op_0x98_res_3_b(cpu); break;
                case 0x99: op_0x99_res_3_c(cpu); break;
                case 0x9A: op_0x9a_res_3_d(cpu); break;
                case 0x9B: op_0x9b_res_3_e(cpu); break;
                case 0x9C: op_0x9c_res_3_h(cpu); break;
                case 0x9D: op_0x9d_res_3_l(cpu); break;
                case 0x9E: op_0x9e_res_3_hlptr(cpu); break;
                case 0x9F: op_0x9f_res_3_a(cpu); break;
                case 0xA0: op_0xa0_res_4_b(cpu); break;
                case 0xA1: op_0xa1_res_4_c(cpu); break;
                case 0xA2: op_0xa2_res_4_d(cpu); break;
                case 0xA3: op_0xa3_res_4_e(cpu); break;
                case 0xA4: op_0xa4_res_4_h(cpu); break;
                case 0xA5: op_0xa5_res_4_l(cpu); break;
                case 0xA6: op_0xa6_res_4_hlptr(cpu); break;
                case 0xA7: op_0xa7_res_4_a(cpu); break;
                case 0xA8: op_0xa8_res_5_b(cpu); break;
                case 0xA9: op_0xa9_res_5_c(cpu); break;
                case 0xAA: op_0xaa_res_5_d(cpu); break;
                case 0xAB: op_0xab_res_5_e(cpu); break;
                case 0xAC: op_0xac_res_5_h(cpu); break;
                case 0xAD: op_0xad_res_5_l(cpu); break;
                case 0xAE: op_0xae_res_5_hlptr(cpu); break;
                case 0xAF: op_0xaf_res_5_a(cpu); break;
                case 0xB0: op_0xb0_res_6_b(cpu); break;
                case 0xB1: op_0xb1_res_6_c(cpu); break;
                case 0xB2: op_0xb2_res_6_d(cpu); break;
                case 0xB3: op_0xb3_res_6_e(cpu); break;
                case 0xB4: op_0xb4_res_6_h(cpu); break;
                case 0xB5: op_0xb5_res_6_l(cpu); break;
                case 0xB6: op_0xb6_res_6_hlptr(cpu); break;
                case 0xB7: op_0xb7_res_6_a(cpu); break;
                case 0xB8: op_0xb8_res_7_b(cpu); break;
                case 0xB9: op_0xb9_res_7_c(cpu); break;
                case 0xBA: op_0xba_res_7_d(cpu); break;
                case 0xBB: op_0xbb_res_7_e(cpu); break;
                case 0xBC: op_0xbc_res_7_h(cpu); break;
                case 0xBD: op_0xbd_res_7_l(cpu); break;
                case 0xBE: op_0xbe_res_7_hlptr(cpu); break;
                case 0xBF: op_0xbf_res_7_a(cpu); break;
                case 0xC0: op_0xc0_set_0_b(cpu); break;
                case 0xC1: op_0xc1_set_0_c(cpu); break;
                case 0xC2: op_0xc2_set_0_d(cpu); break;
                case 0xC3: op_0xc3_set_0_e(cpu); break;
                case 0xC4: op_0xc4_set_0_h(cpu); break;
                case 0xC5: op_0xc5_set_0_l(cpu); break;
                case 0xC6: op_0xc6_set_0_hlptr(cpu); break;
                case 0xC7: op_0xc7_set_0_a(cpu); break;
                case 0xC8: op_0xc8_set_1_b(cpu); break;
                case 0xC9: op_0xc9_set_1_c(cpu); break;
                case 0xCA: op_0xca_set_1_d(cpu); break;
                case 0xCB: op_0xcb_set_1_e(cpu); break;
                case 0xCC: op_0xcc_set_1_h(cpu); break;
                case 0xCD: op_0xcd_set_1_l(cpu); break;
                case 0xCE: op_0xce_set_1_hlptr(cpu); break;
                case 0xCF: op_0xcf_set_1_a(cpu); break;
                case 0xD0: op_0xd0_set_2_b(cpu); break;
                case 0xD1: op_0xd1_set_2_c(cpu); break;
                case 0xD2: op_0xd2_set_2_d(cpu); break;
                case 0xD3: op_0xd3_set_2_e(cpu); break;
                case 0xD4: op_0xd4_set_2_h(cpu); break;
                case 0xD5: op_0xd5_set_2_l(cpu); break;
                case 0xD6: op_0xd6_set_2_hlptr(cpu); break;
                case 0xD7: op_0xd7_set_2_a(cpu); break;
                case 0xD8: op_0xd8_set_3_b(cpu); break;
                case 0xD9: op_0xd9_set_3_c(cpu); break;
                case 0xDA: op_0xda_set_3_d(cpu); break;
                case 0xDB: op_0xdb_set_3_e(cpu); break;
                case 0xDC: op_0xdc_set_3_h(cpu); break;
                case 0xDD: op_0xdd_set_3_l(cpu); break;
                case 0xDE: op_0xde_set_3_hlptr(cpu); break;
                case 0xDF: op_0xdf_set_3_a(cpu); break;
                case 0xE0: op_0xe0_set_4_b(cpu); break;
                case 0xE1: op_0xe1_set_4_c(cpu); break;
                case 0xE2: op_0xe2_set_4_d(cpu); break;
                case 0xE3: op_0xe3_set_4_e(cpu); break;
                case 0xE4: op_0xe4_set_4_h(cpu); break;
                case 0xE5: op_0xe5_set_4_l(cpu); break;
                case 0xE6: op_0xe6_set_4_hlptr(cpu); break;
                case 0xE7: op_0xe7_set_4_a(cpu); break;
                case 0xE8: op_0xe8_set_5_b(cpu); break;
                case 0xE9: op_0xe9_set_5_c(cpu); break;
                case 0xEA: op_0xea_set_5_d(cpu); break;
                case 0xEB: op_0xeb_set_5_e(cpu); break;
                case 0xEC: op_0xec_set_5_h(cpu); break;
                case 0xED: op_0xed_set_5_l(cpu); break;
                case 0xEE: op_0xee_set_5_hlptr(cpu); break;
                case 0xEF: op_0xef_set_5_a(cpu); break;
                case 0xF0: op_0xf0_set_6_b(cpu); break;
                case 0xF1: op_0xf1_set_6_c(cpu); break;
                case 0xF2: op_0xf2_set_6_d(cpu); break;
                case 0xF3: op_0xf3_set_6_e(cpu); break;
                case 0xF4: op_0xf4_set_6_h(cpu); break;
                case 0xF5: op_0xf5_set_6_l(cpu); break;
                case 0xF6: op_0xf6_set_6_hlptr(cpu); break;
                case 0xF7: op_0xf7_set_6_a(cpu); break;
                case 0xF8: op_0xf8_set_7_b(cpu); break;
                case 0xF9: op_0xf9_set_7_c(cpu); break;
                case 0xFA: op_0xfa_set_7_d(cpu); break;
                case 0xFB: op_0xfb_set_7_e(cpu); break;
                case 0xFC: op_0xfc_set_7_h(cpu); break;
                case 0xFD: op_0xfd_set_7_l(cpu); break;
                case 0xFE: op_0xfe_set_7_hlptr(cpu); break;
                case 0xFF: op_0xff_set_7_a(cpu); break;
                default:   log_cpu_error(cpu, "unimplemented CB opcode: 0x%02X", op);
            }
        } break;

        /* ---- unimplemented / illegal ---- */
        default: log_cpu_error(cpu, "unimplemented opcode: 0x%02X", op); break;
    }
}
