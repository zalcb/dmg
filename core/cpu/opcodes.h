#ifndef OPCODES_HEADER
#define OPCODES_HEADER

#include <stdint.h>

struct CPU;
void decode_and_execute(struct CPU *cpu, uint8_t op);

#endif
