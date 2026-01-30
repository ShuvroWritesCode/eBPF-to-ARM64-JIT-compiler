#ifndef EBPF_H
#define EBPF_H

#include <stdint.h>

// eBPF instruction structure (8 bytes)
typedef struct __attribute__((packed)) {
  uint8_t opcode;      // Operation code
  uint8_t dst_reg : 4; // Destination register
  uint8_t src_reg : 4; // Source register
  int16_t offset;      // Memory offset
  int32_t imm;         // Immediate value
} ebpf_insn_t;

// Instruction classes (3 bits)
#define BPF_CLASS_LD 0x00
#define BPF_CLASS_LDX 0x01
#define BPF_CLASS_ST 0x02
#define BPF_CLASS_STX 0x03
#define BPF_CLASS_ALU 0x04
#define BPF_CLASS_JMP 0x05
#define BPF_CLASS_JMP32 0x06
#define BPF_CLASS_ALU64 0x07

// Aliases for convenience
#define BPF_LD BPF_CLASS_LD
#define BPF_LDX BPF_CLASS_LDX
#define BPF_ST BPF_CLASS_ST
#define BPF_STX BPF_CLASS_STX
#define BPF_ALU BPF_CLASS_ALU
#define BPF_JMP BPF_CLASS_JMP
#define BPF_JMP32 BPF_CLASS_JMP32
#define BPF_ALU64 BPF_CLASS_ALU64
#define BPF_MEM 0x60 // Used for memory mode

// ALU operations (4 bits)
#define BPF_ADD 0x00
#define BPF_SUB 0x10
#define BPF_MUL 0x20
#define BPF_DIV 0x30
#define BPF_OR 0x40
#define BPF_AND 0x50
#define BPF_LSH 0x60
#define BPF_RSH 0x70
#define BPF_NEG 0x80
#define BPF_MOD 0x90
#define BPF_XOR 0xa0
#define BPF_MOV 0xb0
#define BPF_ARSH 0xc0
#define BPF_END 0xd0

// Jump operations (4 bits)
#define BPF_JA 0x00
#define BPF_JEQ 0x10
#define BPF_JGT 0x20
#define BPF_JGE 0x30
#define BPF_JSET 0x40
#define BPF_JNE 0x50
#define BPF_JSGT 0x60
#define BPF_JSGE 0x70
#define BPF_CALL 0x80
#define BPF_EXIT 0x90
#define BPF_JLT 0xa0
#define BPF_JLE 0xb0
#define BPF_JSLT 0xc0
#define BPF_JSLE 0xd0

// Source modifiers
#define BPF_K 0x00 // Immediate
#define BPF_X 0x08 // Register

// Memory sizes
#define BPF_W 0x00  // 32-bit
#define BPF_H 0x08  // 16-bit
#define BPF_B 0x10  // 8-bit
#define BPF_DW 0x18 // 64-bit

// Helpers to decode opcodes
#define BPF_OP(code) ((code) & 0xf0)
#define BPF_SIZE(code) ((code) & 0x18)
#define BPF_MODE(code) ((code) & 0xe0)
#define BPF_SRC(code) ((code) & 0x08)
#define BPF_CLASS(code) ((code) & 0x07)

// Endianness modifiers (for BPF_END)
#define BPF_TO_LE 0x00
#define BPF_TO_BE 0x08

// Mode modifiers
#define BPF_IMM 0x00
#define BPF_ABS 0x20
#define BPF_IND 0x40
#define BPF_MEM 0x60
#define BPF_LEN 0x80 // reserved
#define BPF_MSH 0xa0 // reserved

// Atomic operations
#define BPF_XADD 0xc0

// eBPF VM constants
#define EBPF_MAX_INSNS 4096
#define EBPF_STACK_SIZE 512
#define EBPF_MAX_REGS 11

// Register numbers
#define BPF_REG_0 0
#define BPF_REG_1 1
#define BPF_REG_2 2
#define BPF_REG_3 3
#define BPF_REG_4 4
#define BPF_REG_5 5
#define BPF_REG_6 6
#define BPF_REG_7 7
#define BPF_REG_8 8
#define BPF_REG_9 9
#define BPF_REG_10 10

#endif // EBPF_H
