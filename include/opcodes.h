#ifndef OPCODES_HEADER
#define OPCODES_HEADER

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "cpu.h"
#include "mmu.h"

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
void decode_and_execute(CPU *cpu, uint8_t op);

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

#endif
