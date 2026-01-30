#ifndef ARM64_H
#define ARM64_H

#include <stdint.h>

// Scratch registers (not used for BPF regs)
#define ARM64_SCRATCH 9   // x9
#define ARM64_SCRATCH2 10 // x10

// === Data Processing (Immediate) ===

// ADD immediate: add xd, xn, #imm12
static inline uint32_t arm64_add_imm(uint8_t rd, uint8_t rn, uint16_t imm12) {
  return 0x91000000 | ((imm12 & 0xFFF) << 10) | (rn << 5) | rd;
}

// SUB immediate: sub xd, xn, #imm12
static inline uint32_t arm64_sub_imm(uint8_t rd, uint8_t rn, uint16_t imm12) {
  return 0xD1000000 | ((imm12 & 0xFFF) << 10) | (rn << 5) | rd;
}

// MOV (register): mov xd, xm  =>  orr xd, xzr, xm
static inline uint32_t arm64_mov_reg(uint8_t rd, uint8_t rm) {
  return 0xAA0003E0 | (rm << 16) | rd;
}

// MOVZ: movz xd, #imm16, lsl #shift
static inline uint32_t arm64_movz(uint8_t rd, uint16_t imm16, uint8_t shift) {
  return 0xD2800000 | ((shift / 16) << 21) | (imm16 << 5) | rd;
}

// MOVK: movk xd, #imm16, lsl #shift
static inline uint32_t arm64_movk(uint8_t rd, uint16_t imm16, uint8_t shift) {
  return 0xF2800000 | ((shift / 16) << 21) | (imm16 << 5) | rd;
}

// === Data Processing (Register) ===

// ADD: add xd, xn, xm
static inline uint32_t arm64_add_reg(uint8_t rd, uint8_t rn, uint8_t rm) {
  return 0x8B000000 | (rm << 16) | (rn << 5) | rd;
}

// SUB: sub xd, xn, xm
static inline uint32_t arm64_sub_reg(uint8_t rd, uint8_t rn, uint8_t rm) {
  return 0xCB000000 | (rm << 16) | (rn << 5) | rd;
}

// MUL: mul xd, xn, xm
static inline uint32_t arm64_mul(uint8_t rd, uint8_t rn, uint8_t rm) {
  return 0x9B007C00 | (rm << 16) | (rn << 5) | rd;
}

// UDIV: udiv xd, xn, xm
static inline uint32_t arm64_udiv(uint8_t rd, uint8_t rn, uint8_t rm) {
  return 0x9AC00800 | (rm << 16) | (rn << 5) | rd;
}

// SDIV: sdiv xd, xn, xm
static inline uint32_t arm64_sdiv(uint8_t rd, uint8_t rn, uint8_t rm) {
  return 0x9AC00C00 | (rm << 16) | (rn << 5) | rd;
}

// AND: and xd, xn, xm
static inline uint32_t arm64_and(uint8_t rd, uint8_t rn, uint8_t rm) {
  return 0x8A000000 | (rm << 16) | (rn << 5) | rd;
}

// ORR: orr xd, xn, xm
static inline uint32_t arm64_orr(uint8_t rd, uint8_t rn, uint8_t rm) {
  return 0xAA000000 | (rm << 16) | (rn << 5) | rd;
}

// EOR (XOR): eor xd, xn, xm
static inline uint32_t arm64_eor(uint8_t rd, uint8_t rn, uint8_t rm) {
  return 0xCA000000 | (rm << 16) | (rn << 5) | rd;
}

// LSL: lsl xd, xn, xm
static inline uint32_t arm64_lsl(uint8_t rd, uint8_t rn, uint8_t rm) {
  return 0x9AC02000 | (rm << 16) | (rn << 5) | rd;
}

// LSR: lsr xd, xn, xm
static inline uint32_t arm64_lsr(uint8_t rd, uint8_t rn, uint8_t rm) {
  return 0x9AC02400 | (rm << 16) | (rn << 5) | rd;
}

// ASR: asr xd, xn, xm
static inline uint32_t arm64_asr(uint8_t rd, uint8_t rn, uint8_t rm) {
  return 0x9AC02800 | (rm << 16) | (rn << 5) | rd;
}

// === Loads and Stores ===

// LDR (unsigned offset): ldr xd, [xn, #imm12*8]
static inline uint32_t arm64_ldr_imm(uint8_t rd, uint8_t rn, uint16_t imm12) {
  return 0xF9400000 | ((imm12 & 0xFFF) << 10) | (rn << 5) | rd;
}

// STR (unsigned offset): str xd, [xn, #imm12*8]
static inline uint32_t arm64_str_imm(uint8_t rd, uint8_t rn, uint16_t imm12) {
  return 0xF9000000 | ((imm12 & 0xFFF) << 10) | (rn << 5) | rd;
}

// === Branches ===

// B: b #offset (PC-relative, ±128MB)
static inline uint32_t arm64_b(int32_t offset) {
  return 0x14000000 | ((offset >> 2) & 0x03FFFFFF);
}

// B.cond: b.<cond> #offset
static inline uint32_t arm64_bcond(uint8_t cond, int32_t offset) {
  return 0x54000000 | (((offset >> 2) & 0x7FFFF) << 5) | cond;
}

// BL: bl #offset (function call)
static inline uint32_t arm64_bl(int32_t offset) {
  return 0x94000000 | ((offset >> 2) & 0x03FFFFFF);
}

// BLR: blr xn (indirect call)
static inline uint32_t arm64_blr(uint8_t rn) { return 0xD63F0000 | (rn << 5); }

// RET: ret (return)
static inline uint32_t arm64_ret(void) { return 0xD65F03C0; }

// CMP: cmp xn, xm  (subs xzr, xn, xm)
static inline uint32_t arm64_cmp_reg(uint8_t rn, uint8_t rm) {
  return 0xEB00001F | (rm << 16) | (rn << 5);
}

// CMP immediate: cmp xn, #imm
static inline uint32_t arm64_cmp_imm(uint8_t rn, uint16_t imm12) {
  return 0xF100001F | ((imm12 & 0xFFF) << 10) | (rn << 5);
}

#endif // ARM64_H
