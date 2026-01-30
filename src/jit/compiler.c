#include "arm64.h"
#include "jit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

// BPF scratch register (R9 is not commonly used, maps to x28)
#define BPF_SCRATCH_REG 9

// JIT context for code generation
typedef struct {
  uint8_t *code_buf; // Executable memory buffer
  size_t code_len;   // Current code length
  size_t code_cap;   // Buffer capacity

  size_t *insn_offsets; // BPF PC -> ARM64 offset mapping
  size_t insn_count;    // Number of BPF instructions

  // Jump fixup tracking
  struct {
    size_t arm64_offset; // Location of branch instruction
    size_t bpf_target;   // Target BPF instruction
    uint8_t cond;        // Condition code (0xFF = unconditional)
  } *jumps;
  size_t jump_count;
  size_t jump_cap;

  // Helper function table
  jit_helper_func_t *helpers;
  size_t helper_count;
} jit_ctx_t;

// BPF register to ARM64 register mapping
// BPF R0-R9 -> ARM64 x19-x28 (callee-saved)
// BPF R10   -> ARM64 x29 (frame pointer)
static inline uint8_t bpf_to_arm64_reg(uint8_t bpf_reg) {
  if (bpf_reg > 10)
    return 0; // Invalid
  return 19 + bpf_reg;
}

// Emit a 32-bit ARM64 instruction
static void emit32(jit_ctx_t *ctx, uint32_t insn) {
  if (ctx->code_len + 4 > ctx->code_cap) {
    fprintf(stderr, "JIT: Code buffer overflow\n");
    return;
  }
  *(uint32_t *)(ctx->code_buf + ctx->code_len) = insn;
  ctx->code_len += 4;
}

// Record a jump for later fixup
static void record_jump(jit_ctx_t *ctx, size_t bpf_target, uint8_t cond) {
  if (ctx->jump_count >= ctx->jump_cap) {
    ctx->jump_cap = ctx->jump_cap ? ctx->jump_cap * 2 : 16;
    ctx->jumps = realloc(ctx->jumps, ctx->jump_cap * sizeof(*ctx->jumps));
  }
  ctx->jumps[ctx->jump_count].arm64_offset = ctx->code_len;
  ctx->jumps[ctx->jump_count].bpf_target = bpf_target;
  ctx->jumps[ctx->jump_count].cond = cond;
  ctx->jump_count++;
}

// Emit function prologue
static void emit_prologue(jit_ctx_t *ctx) {
  // Save callee-saved registers (x19-x30, FP/LR)
  // stp x29, x30, [sp, #-16]!
  emit32(ctx, 0xa9bf7bfd);
  // stp x27, x28, [sp, #-16]!
  emit32(ctx, 0xa9bf73fb);
  // stp x25, x26, [sp, #-16]!
  emit32(ctx, 0xa9bf6bf9);
  // stp x23, x24, [sp, #-16]!
  emit32(ctx, 0xa9bf63f7);
  // stp x21, x22, [sp, #-16]!
  emit32(ctx, 0xa9bf5bf5);
  // stp x19, x20, [sp, #-16]!
  emit32(ctx, 0xa9bf53f3);

  // Initialize BPF R1 with context pointer (from x0)
  // mov x20, x0
  emit32(ctx, 0xaa0003f4);

  // Initialize BPF R2-R5 from argument registers x1-x4
  // mov x21, x1
  emit32(ctx, 0xaa0103f5);
  // mov x22, x2
  emit32(ctx, 0xaa0203f6);
  // mov x23, x3
  emit32(ctx, 0xaa0303f7);
  // mov x24, x4
  emit32(ctx, 0xaa0403f8);

  // Allocate BPF stack (512 bytes)
  // sub sp, sp, #512
  emit32(ctx, 0xd1080000 | (31 << 5) | 31); // sub sp, sp, #512

  // Initialize BPF R10 (frame pointer) to top of stack
  // add x29, sp, #512
  emit32(ctx, arm64_add_imm(29, 31, 512));
}

// Emit function epilogue
static void emit_epilogue(jit_ctx_t *ctx) {
  // Move return value from BPF R0 (x19) to ARM64 x0
  // mov x0, x19
  emit32(ctx, 0xaa1303e0);

  // Deallocate BPF stack
  // add sp, sp, #512
  emit32(ctx, 0x91080000 | (31 << 5) | 31);

  // Restore callee-saved registers (reverse order)
  // ldp x19, x20, [sp], #16
  emit32(ctx, 0xa8c153f3);
  // ldp x21, x22, [sp], #16
  emit32(ctx, 0xa8c15bf5);
  // ldp x23, x24, [sp], #16
  emit32(ctx, 0xa8c163f7);
  // ldp x25, x26, [sp], #16
  emit32(ctx, 0xa8c16bf9);
  // ldp x27, x28, [sp], #16
  emit32(ctx, 0xa8c173fb);
  // ldp x29, x30, [sp], #16
  emit32(ctx, 0xa8c17bfd);

  // ret
  emit32(ctx, 0xd65f03c0);
}

// Emit instruction translation
static void emit_insn(jit_ctx_t *ctx, const ebpf_insn_t *insns, size_t pc,
                      size_t count) {
  const ebpf_insn_t *insn = &insns[pc];
  uint8_t opcode = insn->opcode;
  uint8_t dst = bpf_to_arm64_reg(insn->dst_reg);
  uint8_t src = bpf_to_arm64_reg(insn->src_reg);

  switch (opcode) {
  // === ALU64 Register operations ===
  case BPF_ALU64 | BPF_ADD | BPF_X:
    emit32(ctx, arm64_add_reg(dst, dst, src));
    break;

  case BPF_ALU64 | BPF_SUB | BPF_X:
    emit32(ctx, arm64_sub_reg(dst, dst, src));
    break;

  case BPF_ALU64 | BPF_MUL | BPF_X:
    emit32(ctx, arm64_mul(dst, dst, src));
    break;

  case BPF_ALU64 | BPF_DIV | BPF_X:
    // TODO: Add division by zero check
    emit32(ctx, arm64_udiv(dst, dst, src));
    break;

  case BPF_ALU64 | BPF_MOD | BPF_X:
    // ARM64 doesn't have MOD, use: mod = a - (a/b)*b
    // tmp = dst / src
    emit32(ctx, arm64_udiv(ARM64_SCRATCH, dst, src));
    // tmp = tmp * src
    emit32(ctx, arm64_mul(ARM64_SCRATCH, ARM64_SCRATCH, src));
    // dst = dst - tmp
    emit32(ctx, arm64_sub_reg(dst, dst, ARM64_SCRATCH));
    break;

  case BPF_ALU64 | BPF_OR | BPF_X:
    emit32(ctx, arm64_orr(dst, dst, src));
    break;

  case BPF_ALU64 | BPF_AND | BPF_X:
    emit32(ctx, arm64_and(dst, dst, src));
    break;

  case BPF_ALU64 | BPF_XOR | BPF_X:
    emit32(ctx, arm64_eor(dst, dst, src));
    break;

  case BPF_ALU64 | BPF_LSH | BPF_X:
    emit32(ctx, arm64_lsl(dst, dst, src));
    break;

  case BPF_ALU64 | BPF_RSH | BPF_X:
    emit32(ctx, arm64_lsr(dst, dst, src));
    break;

  case BPF_ALU64 | BPF_ARSH | BPF_X:
    emit32(ctx, arm64_asr(dst, dst, src));
    break;

  case BPF_ALU64 | BPF_MOV | BPF_X:
    emit32(ctx, arm64_mov_reg(dst, src));
    break;

  case BPF_ALU64 | BPF_NEG:
    // neg xdst = sub xdst, xzr, xdst
    emit32(ctx, 0xcb0003e0 | (dst << 16) | dst);
    break;

  // === ALU64 Immediate operations ===
  case BPF_ALU64 | BPF_MOV | BPF_K:
    // Load 32-bit immediate (sign-extended to 64-bit)
    if (insn->imm >= 0 && insn->imm < 65536) {
      emit32(ctx, arm64_movz(dst, insn->imm, 0));
    } else if (insn->imm < 0 && insn->imm >= -65536) {
      // movn for negative values
      emit32(ctx, 0x92800000 | ((~insn->imm & 0xFFFF) << 5) | dst);
    } else {
      // Multi-instruction sequence for larger values
      emit32(ctx, arm64_movz(dst, insn->imm & 0xFFFF, 0));
      if ((insn->imm >> 16) & 0xFFFF) {
        emit32(ctx, arm64_movk(dst, (insn->imm >> 16) & 0xFFFF, 16));
      }
    }
    break;

  case BPF_ALU64 | BPF_ADD | BPF_K:
    if (insn->imm >= 0 && insn->imm < 4096) {
      emit32(ctx, arm64_add_imm(dst, dst, insn->imm));
    } else if (insn->imm < 0 && insn->imm >= -4096) {
      emit32(ctx, arm64_sub_imm(dst, dst, -insn->imm));
    } else {
      // Load to scratch and add
      emit32(ctx, arm64_movz(ARM64_SCRATCH, insn->imm & 0xFFFF, 0));
      if ((insn->imm >> 16) & 0xFFFF) {
        emit32(ctx, arm64_movk(ARM64_SCRATCH, (insn->imm >> 16) & 0xFFFF, 16));
      }
      emit32(ctx, arm64_add_reg(dst, dst, ARM64_SCRATCH));
    }
    break;

  case BPF_ALU64 | BPF_SUB | BPF_K:
    if (insn->imm >= 0 && insn->imm < 4096) {
      emit32(ctx, arm64_sub_imm(dst, dst, insn->imm));
    } else {
      // Load to scratch and subtract
      emit32(ctx, arm64_movz(ARM64_SCRATCH, insn->imm & 0xFFFF, 0));
      emit32(ctx, arm64_sub_reg(dst, dst, ARM64_SCRATCH));
    }
    break;

  case BPF_ALU64 | BPF_MUL | BPF_K:
    // Load immediate to scratch, then multiply
    emit32(ctx, arm64_movz(ARM64_SCRATCH, insn->imm & 0xFFFF, 0));
    if ((insn->imm >> 16) & 0xFFFF) {
      emit32(ctx, arm64_movk(ARM64_SCRATCH, (insn->imm >> 16) & 0xFFFF, 16));
    }
    emit32(ctx, arm64_mul(dst, dst, ARM64_SCRATCH));
    break;

  case BPF_ALU64 | BPF_DIV | BPF_K:
    // Load immediate to scratch, then divide
    emit32(ctx, arm64_movz(ARM64_SCRATCH, insn->imm & 0xFFFF, 0));
    if ((insn->imm >> 16) & 0xFFFF) {
      emit32(ctx, arm64_movk(ARM64_SCRATCH, (insn->imm >> 16) & 0xFFFF, 16));
    }
    // TODO: Add division by zero check
    emit32(ctx, arm64_udiv(dst, dst, ARM64_SCRATCH));
    break;

  case BPF_ALU64 | BPF_MOD | BPF_K:
    // Load immediate to scratch, then modulo
    emit32(ctx, arm64_movz(ARM64_SCRATCH, insn->imm & 0xFFFF, 0));
    if ((insn->imm >> 16) & 0xFFFF) {
      emit32(ctx, arm64_movk(ARM64_SCRATCH, (insn->imm >> 16) & 0xFFFF, 16));
    }
    // TODO: Add division by zero check
    // Use x10 as temporary for quotient
    emit32(ctx, arm64_udiv(10, dst, ARM64_SCRATCH));
    emit32(ctx, arm64_msub(dst, 10, ARM64_SCRATCH, dst));
    break;

  case BPF_ALU64 | BPF_OR | BPF_K:
    // Load immediate to scratch, then OR
    emit32(ctx, arm64_movz(ARM64_SCRATCH, insn->imm & 0xFFFF, 0));
    if ((insn->imm >> 16) & 0xFFFF) {
      emit32(ctx, arm64_movk(ARM64_SCRATCH, (insn->imm >> 16) & 0xFFFF, 16));
    }
    emit32(ctx, arm64_orr(dst, dst, ARM64_SCRATCH));
    break;

  case BPF_ALU64 | BPF_AND | BPF_K:
    // Load immediate to scratch, then AND
    emit32(ctx, arm64_movz(ARM64_SCRATCH, insn->imm & 0xFFFF, 0));
    if ((insn->imm >> 16) & 0xFFFF) {
      emit32(ctx, arm64_movk(ARM64_SCRATCH, (insn->imm >> 16) & 0xFFFF, 16));
    }
    emit32(ctx, arm64_and(dst, dst, ARM64_SCRATCH));
    break;

  case BPF_ALU64 | BPF_XOR | BPF_K:
    // Load immediate to scratch, then XOR
    emit32(ctx, arm64_movz(ARM64_SCRATCH, insn->imm & 0xFFFF, 0));
    if ((insn->imm >> 16) & 0xFFFF) {
      emit32(ctx, arm64_movk(ARM64_SCRATCH, (insn->imm >> 16) & 0xFFFF, 16));
    }
    emit32(ctx, arm64_eor(dst, dst, ARM64_SCRATCH));
    break;

  case BPF_ALU64 | BPF_LSH | BPF_K:
    // Load immediate to scratch, then shift left
    emit32(ctx, arm64_movz(ARM64_SCRATCH, insn->imm & 0xFFFF, 0));
    emit32(ctx, arm64_lsl(dst, dst, ARM64_SCRATCH));
    break;

  case BPF_ALU64 | BPF_RSH | BPF_K:
    // Load immediate to scratch, then shift right
    emit32(ctx, arm64_movz(ARM64_SCRATCH, insn->imm & 0xFFFF, 0));
    emit32(ctx, arm64_lsr(dst, dst, ARM64_SCRATCH));
    break;

  case BPF_ALU64 | BPF_ARSH | BPF_K:
    // Load immediate to scratch, then arithmetic shift right
    emit32(ctx, arm64_movz(ARM64_SCRATCH, insn->imm & 0xFFFF, 0));
    emit32(ctx, arm64_asr(dst, dst, ARM64_SCRATCH));
    break;

  // === ALU (32-bit) Register operations ===
  case BPF_ALU | BPF_ADD | BPF_X:
    emit32(ctx, arm64_add_reg32(dst, dst, src));
    break;

  case BPF_ALU | BPF_SUB | BPF_X:
    emit32(ctx, arm64_sub_reg32(dst, dst, src));
    break;

  case BPF_ALU | BPF_MUL | BPF_X:
    emit32(ctx, arm64_mul32(dst, dst, src));
    break;

  case BPF_ALU | BPF_DIV | BPF_X:
    emit32(ctx, arm64_udiv32(dst, dst, src));
    break;

  case BPF_ALU | BPF_MOD | BPF_X:
    // mod32 = a - (a/b)*b
    emit32(ctx, arm64_udiv32(ARM64_SCRATCH, dst, src));
    emit32(ctx, arm64_mul32(ARM64_SCRATCH, ARM64_SCRATCH, src));
    emit32(ctx, arm64_sub_reg32(dst, dst, ARM64_SCRATCH));
    break;

  case BPF_ALU | BPF_OR | BPF_X:
    emit32(ctx, arm64_orr32(dst, dst, src));
    break;

  case BPF_ALU | BPF_AND | BPF_X:
    emit32(ctx, arm64_and32(dst, dst, src));
    break;

  case BPF_ALU | BPF_XOR | BPF_X:
    emit32(ctx, arm64_eor32(dst, dst, src));
    break;

  case BPF_ALU | BPF_LSH | BPF_X:
    emit32(ctx, arm64_lsl32(dst, dst, src));
    break;

  case BPF_ALU | BPF_RSH | BPF_X:
    emit32(ctx, arm64_lsr32(dst, dst, src));
    break;

  case BPF_ALU | BPF_ARSH | BPF_X:
    emit32(ctx, arm64_asr32(dst, dst, src));
    break;

  case BPF_ALU | BPF_MOV | BPF_X:
    // mov w_dst, w_src (zero extends to 64-bit)
    emit32(ctx, 0x2A0003E0 | (src << 16) | dst); // orr wd, wzr, wm
    break;

  case BPF_ALU | BPF_NEG:
    // neg wd = sub wd, wzr, wd
    emit32(ctx, 0x4b0003e0 | (dst << 16) | dst);
    break;

  // === ALU (32-bit) Immediate operations ===
  case BPF_ALU | BPF_MOV | BPF_K:
    emit32(ctx, arm64_movz32(dst, insn->imm & 0xFFFF, 0));
    if ((insn->imm >> 16) & 0xFFFF) {
      // Create a 2nd instruction to load upper 16 bits if needed
      // movk wd, #imm16, lsl #16 (0x72a00000)
      emit32(ctx, 0x72a00000 | (((insn->imm >> 16) & 0xFFFF) << 5) | dst);
    }
    break;

  case BPF_ALU | BPF_ADD | BPF_K:
    if (insn->imm >= 0 && insn->imm < 4096) {
      emit32(ctx, arm64_add_imm32(dst, dst, insn->imm));
    } else if (insn->imm < 0 && insn->imm >= -4096) {
      emit32(ctx, arm64_sub_imm32(dst, dst, -insn->imm));
    } else {
      emit32(ctx, arm64_movz32(ARM64_SCRATCH, insn->imm & 0xFFFF, 0));
      emit32(ctx, arm64_add_reg32(dst, dst, ARM64_SCRATCH));
    }
    break;

  case BPF_ALU | BPF_SUB | BPF_K:
    if (insn->imm >= 0 && insn->imm < 4096) {
      emit32(ctx, arm64_sub_imm32(dst, dst, insn->imm));
    } else {
      emit32(ctx, arm64_movz32(ARM64_SCRATCH, insn->imm & 0xFFFF, 0));
      emit32(ctx, arm64_sub_reg32(dst, dst, ARM64_SCRATCH));
    }
    break;

  case BPF_ALU | BPF_MUL | BPF_K:
    emit32(ctx, arm64_movz32(ARM64_SCRATCH, insn->imm & 0xFFFF, 0));
    emit32(ctx, arm64_mul32(dst, dst, ARM64_SCRATCH));
    break;

  case BPF_ALU | BPF_DIV | BPF_K:
    emit32(ctx, arm64_movz32(ARM64_SCRATCH, insn->imm & 0xFFFF, 0));
    emit32(ctx, arm64_udiv32(dst, dst, ARM64_SCRATCH));
    break;

  case BPF_ALU | BPF_MOD | BPF_K:
    emit32(ctx, arm64_movz32(ARM64_SCRATCH, insn->imm & 0xFFFF, 0));
    emit32(ctx, arm64_udiv32(10, dst, ARM64_SCRATCH));
    emit32(ctx, 0x1B008000 | (ARM64_SCRATCH << 16) | (10 << 10) | (dst << 5) |
                    dst); // msub32
    break;

  case BPF_ALU | BPF_OR | BPF_K:
    emit32(ctx, arm64_movz32(ARM64_SCRATCH, insn->imm & 0xFFFF, 0));
    emit32(ctx, arm64_orr32(dst, dst, ARM64_SCRATCH));
    break;

  case BPF_ALU | BPF_AND | BPF_K:
    emit32(ctx, arm64_movz32(ARM64_SCRATCH, insn->imm & 0xFFFF, 0));
    emit32(ctx, arm64_and32(dst, dst, ARM64_SCRATCH));
    break;

  case BPF_ALU | BPF_XOR | BPF_K:
    emit32(ctx, arm64_movz32(ARM64_SCRATCH, insn->imm & 0xFFFF, 0));
    emit32(ctx, arm64_eor32(dst, dst, ARM64_SCRATCH));
    break;

  case BPF_ALU | BPF_LSH | BPF_K:
    emit32(ctx, arm64_movz32(ARM64_SCRATCH, insn->imm & 0xFFFF, 0));
    emit32(ctx, arm64_lsl32(dst, dst, ARM64_SCRATCH));
    break;

  case BPF_ALU | BPF_RSH | BPF_K:
    emit32(ctx, arm64_movz32(ARM64_SCRATCH, insn->imm & 0xFFFF, 0));
    emit32(ctx, arm64_lsr32(dst, dst, ARM64_SCRATCH));
    break;

  case BPF_ALU | BPF_ARSH | BPF_K:
    emit32(ctx, arm64_movz32(ARM64_SCRATCH, insn->imm & 0xFFFF, 0));
    emit32(ctx, arm64_asr32(dst, dst, ARM64_SCRATCH));
    break;

  // === Endianness conversion (BPF_END) ===
  case BPF_ALU64 | BPF_END |
      BPF_TO_LE: // Test suite expects swap for this opcode
  case BPF_ALU64 | BPF_END | BPF_TO_BE:
  case BPF_ALU | BPF_END | BPF_TO_BE:
    switch (insn->imm) {
    case 16:
      emit32(ctx, arm64_rev16(dst, dst));
      emit32(ctx, arm64_and(dst, dst, 0)); // and xd, xd, #0xffff (using ubfx
                                           // logic or similar if needed)
      // Actually simpler:
      // rev16 wd, wn swaps bytes in halfwords. 0x1234 -> 0x3412.
      // But we need to mask upper bits.
      // arm64_and doesn't support immediate directly nicely for all values
      // without encoding logic. Let's use ubfx or just and with immediate if we
      // had a helper. Or load mask 0xffff to scratch.
      emit32(ctx, arm64_movz(ARM64_SCRATCH, 0xFFFF, 0));
      emit32(ctx, arm64_and(dst, dst, ARM64_SCRATCH));
      break;
    case 32:
      emit32(ctx, arm64_rev32(dst, dst));
      // rev32 on 64-bit reg swaps bytes in each 32-bit word.
      // We need to zero upper 32-bits.
      // "mov w0, w0" does zero extension.
      // But rev32 returns 64-bit result if using 'x' regs?
      // Actually "rev32 xd, xn" swaps in 32-bit words.
      // "rev wd, wn" is 32-bit reverse.
      // Let's use 32-bit rev: rev wd, wn
      // arm64_rev32 actually encodes 32-bit REV if bit 31 is 0.
      // Wait, arm64_rev32 implementation: 0x5AC00800 | ...
      // 0x5... is 32-bit (sf=0). So it writes to wd, implicitly zero extending!
      // So arm64_rev32(dst, dst) is technically "rev wd, wn" which reverses
      // 32-bits and zero extends. So no extra mask needed!
      break;
    case 64:
      emit32(ctx, arm64_rev64(dst, dst));
      break;
    }
    break;

  case BPF_ALU | BPF_END | BPF_TO_LE:
    switch (insn->imm) {
    case 16:
      // Host is LE, so just truncate to 16 bits
      emit32(ctx, arm64_movz(ARM64_SCRATCH, 0xFFFF, 0));
      emit32(ctx, arm64_and(dst, dst, ARM64_SCRATCH));
      break;
    case 32:
      // Host is LE, just truncate to 32 bits (zero extend)
      // mov wd, wn
      emit32(ctx, 0x2A0003E0 | (dst << 16) | dst);
      break;
    case 64:
      // No-op
      break;
    }
    break;

  // === CONTROL FLOW ===
  // Unconditional jump
  case BPF_JMP | BPF_JA:
    record_jump(ctx, pc + 1 + insn->offset, 0xFF); // 0xFF = unconditional
    emit32(ctx, 0x14000000);                       // b #0 (placeholder)
    break;

  // Conditional jumps - Register
  case BPF_JMP | BPF_JEQ | BPF_X:
    emit32(ctx, arm64_cmp_reg(dst, src));
    record_jump(ctx, pc + 1 + insn->offset, 0x0); // EQ condition
    emit32(ctx, 0x54000000);                      // b.eq #0 (placeholder)
    break;

  case BPF_JMP | BPF_JNE | BPF_X:
    emit32(ctx, arm64_cmp_reg(dst, src));
    record_jump(ctx, pc + 1 + insn->offset, 0x1); // NE condition
    emit32(ctx, 0x54000001);                      // b.ne #0 (placeholder)
    break;

  case BPF_JMP | BPF_JGT | BPF_X:
    emit32(ctx, arm64_cmp_reg(dst, src));
    record_jump(ctx, pc + 1 + insn->offset, 0x8); // HI condition (unsigned >)
    emit32(ctx, 0x54000008);                      // b.hi #0 (placeholder)
    break;

  case BPF_JMP | BPF_JGE | BPF_X:
    emit32(ctx, arm64_cmp_reg(dst, src));
    record_jump(ctx, pc + 1 + insn->offset,
                0x2);        // HS/CS condition (unsigned >=)
    emit32(ctx, 0x54000002); // b.hs #0 (placeholder)
    break;

  case BPF_JMP | BPF_JLT | BPF_X:
    emit32(ctx, arm64_cmp_reg(dst, src));
    record_jump(ctx, pc + 1 + insn->offset,
                0x3);        // LO/CC condition (unsigned <)
    emit32(ctx, 0x54000003); // b.lo #0 (placeholder)
    break;

  case BPF_JMP | BPF_JLE | BPF_X:
    emit32(ctx, arm64_cmp_reg(dst, src));
    record_jump(ctx, pc + 1 + insn->offset, 0x9); // LS condition (unsigned <=)
    emit32(ctx, 0x54000009);                      // b.ls #0 (placeholder)
    break;

  case BPF_JMP | BPF_JSGT | BPF_X:
    emit32(ctx, arm64_cmp_reg(dst, src));
    record_jump(ctx, pc + 1 + insn->offset, 0xc); // GT condition (signed >)
    emit32(ctx, 0x5400000c);                      // b.gt #0 (placeholder)
    break;

  case BPF_JMP | BPF_JSGE | BPF_X:
    emit32(ctx, arm64_cmp_reg(dst, src));
    record_jump(ctx, pc + 1 + insn->offset, 0xa); // GE condition (signed >=)
    emit32(ctx, 0x5400000a);                      // b.ge #0 (placeholder)
    break;

  case BPF_JMP | BPF_JSLT | BPF_X:
    emit32(ctx, arm64_cmp_reg(dst, src));
    record_jump(ctx, pc + 1 + insn->offset, 0xb); // LT condition (signed <)
    emit32(ctx, 0x5400000b);                      // b.lt #0 (placeholder)
    break;

  case BPF_JMP | BPF_JSLE | BPF_X:
    emit32(ctx, arm64_cmp_reg(dst, src));
    record_jump(ctx, pc + 1 + insn->offset, 0xd); // LE condition (signed <=)
    emit32(ctx, 0x5400000d);                      // b.le #0 (placeholder)
    break;

  case BPF_JMP | BPF_JSET | BPF_X:
    // tst = ands xzr, dst, src
    emit32(ctx, 0xea00001f | (src << 16) | (dst << 5));
    record_jump(ctx, pc + 1 + insn->offset, 0x1); // NE condition (not zero)
    emit32(ctx, 0x54000001);                      // b.ne #0 (placeholder)
    break;

  // Conditional jumps - Immediate
  case BPF_JMP | BPF_JEQ | BPF_K:
    if (insn->imm == 0) {
      // Optimize JEQ r, 0 -> CBZ r, offset
      record_jump(ctx, pc + 1 + insn->offset,
                  0x0);               // != 0xFF means 19-bit offset
      emit32(ctx, arm64_cbz(dst, 0)); // cbz xn, 0 (placeholder)
    } else {
      emit32(ctx, arm64_movz(ARM64_SCRATCH, insn->imm & 0xFFFF, 0));
      if ((insn->imm >> 16) & 0xFFFF) {
        emit32(ctx, arm64_movk(ARM64_SCRATCH, (insn->imm >> 16) & 0xFFFF, 16));
      }
      emit32(ctx,
             arm64_sxtw(ARM64_SCRATCH, ARM64_SCRATCH)); // Sign extend to 64-bit
      emit32(ctx, arm64_cmp_reg(dst, ARM64_SCRATCH));
      record_jump(ctx, pc + 1 + insn->offset, 0x0); // EQ condition
      emit32(ctx, 0x54000000);                      // b.eq #0 (placeholder)
    }
    break;

  case BPF_JMP | BPF_JNE | BPF_K:
    if (insn->imm == 0) {
      // Optimize JNE r, 0 -> CBNZ r, offset
      record_jump(ctx, pc + 1 + insn->offset,
                  0x1);                // != 0xFF means 19-bit offset
      emit32(ctx, arm64_cbnz(dst, 0)); // cbnz xn, 0 (placeholder)
    } else {
      emit32(ctx, arm64_movz(ARM64_SCRATCH, insn->imm & 0xFFFF, 0));
      if ((insn->imm >> 16) & 0xFFFF) {
        emit32(ctx, arm64_movk(ARM64_SCRATCH, (insn->imm >> 16) & 0xFFFF, 16));
      }
      emit32(ctx,
             arm64_sxtw(ARM64_SCRATCH, ARM64_SCRATCH)); // Sign extend to 64-bit
      emit32(ctx, arm64_cmp_reg(dst, ARM64_SCRATCH));
      record_jump(ctx, pc + 1 + insn->offset, 0x1); // NE condition
      emit32(ctx, 0x54000001);                      // b.ne #0 (placeholder)
    }
    break;

  case BPF_JMP | BPF_JGT | BPF_K:
    emit32(ctx, arm64_movz(ARM64_SCRATCH, insn->imm & 0xFFFF, 0));
    if ((insn->imm >> 16) & 0xFFFF) {
      emit32(ctx, arm64_movk(ARM64_SCRATCH, (insn->imm >> 16) & 0xFFFF, 16));
    }
    emit32(ctx,
           arm64_sxtw(ARM64_SCRATCH, ARM64_SCRATCH)); // Sign extend to 64-bit
    emit32(ctx, arm64_cmp_reg(dst, ARM64_SCRATCH));
    record_jump(ctx, pc + 1 + insn->offset, 0x8); // HI condition (unsigned >)
    emit32(ctx, 0x54000008);                      // b.hi #0 (placeholder)
    break;

  case BPF_JMP | BPF_JGE | BPF_K:
    emit32(ctx, arm64_movz(ARM64_SCRATCH, insn->imm & 0xFFFF, 0));
    if ((insn->imm >> 16) & 0xFFFF) {
      emit32(ctx, arm64_movk(ARM64_SCRATCH, (insn->imm >> 16) & 0xFFFF, 16));
    }
    emit32(ctx,
           arm64_sxtw(ARM64_SCRATCH, ARM64_SCRATCH)); // Sign extend to 64-bit
    emit32(ctx, arm64_cmp_reg(dst, ARM64_SCRATCH));
    record_jump(ctx, pc + 1 + insn->offset,
                0x2);        // HS/CS condition (unsigned >=)
    emit32(ctx, 0x54000002); // b.hs #0 (placeholder)
    break;

  case BPF_JMP | BPF_JLT | BPF_K:
    emit32(ctx, arm64_movz(ARM64_SCRATCH, insn->imm & 0xFFFF, 0));
    if ((insn->imm >> 16) & 0xFFFF) {
      emit32(ctx, arm64_movk(ARM64_SCRATCH, (insn->imm >> 16) & 0xFFFF, 16));
    }
    emit32(ctx,
           arm64_sxtw(ARM64_SCRATCH, ARM64_SCRATCH)); // Sign extend to 64-bit
    emit32(ctx, arm64_cmp_reg(dst, ARM64_SCRATCH));
    record_jump(ctx, pc + 1 + insn->offset,
                0x3);        // LO/CC condition (unsigned <)
    emit32(ctx, 0x54000003); // b.lo #0 (placeholder)
    break;

  case BPF_JMP | BPF_JLE | BPF_K:
    emit32(ctx, arm64_movz(ARM64_SCRATCH, insn->imm & 0xFFFF, 0));
    if ((insn->imm >> 16) & 0xFFFF) {
      emit32(ctx, arm64_movk(ARM64_SCRATCH, (insn->imm >> 16) & 0xFFFF, 16));
    }
    emit32(ctx,
           arm64_sxtw(ARM64_SCRATCH, ARM64_SCRATCH)); // Sign extend to 64-bit
    emit32(ctx, arm64_cmp_reg(dst, ARM64_SCRATCH));
    record_jump(ctx, pc + 1 + insn->offset, 0x9); // LS condition (unsigned <=)
    emit32(ctx, 0x54000009);                      // b.ls #0 (placeholder)
    break;

  case BPF_JMP | BPF_JSGT | BPF_K:
    emit32(ctx, arm64_movz(ARM64_SCRATCH, insn->imm & 0xFFFF, 0));
    if ((insn->imm >> 16) & 0xFFFF) {
      emit32(ctx, arm64_movk(ARM64_SCRATCH, (insn->imm >> 16) & 0xFFFF, 16));
    }
    emit32(ctx,
           arm64_sxtw(ARM64_SCRATCH, ARM64_SCRATCH)); // Sign extend to 64-bit
    emit32(ctx, arm64_cmp_reg(dst, ARM64_SCRATCH));
    record_jump(ctx, pc + 1 + insn->offset, 0xC); // GT condition (signed >)
    emit32(ctx, 0x5400000C);                      // b.gt #0 (placeholder)
    break;

  case BPF_JMP | BPF_JSGE | BPF_K:
    emit32(ctx, arm64_movz(ARM64_SCRATCH, insn->imm & 0xFFFF, 0));
    if ((insn->imm >> 16) & 0xFFFF) {
      emit32(ctx, arm64_movk(ARM64_SCRATCH, (insn->imm >> 16) & 0xFFFF, 16));
    }
    emit32(ctx,
           arm64_sxtw(ARM64_SCRATCH, ARM64_SCRATCH)); // Sign extend to 64-bit
    emit32(ctx, arm64_cmp_reg(dst, ARM64_SCRATCH));
    record_jump(ctx, pc + 1 + insn->offset, 0xA); // GE condition (signed >=)
    emit32(ctx, 0x5400000A);                      // b.ge #0 (placeholder)
    break;

  case BPF_JMP | BPF_JSLT | BPF_K:
    emit32(ctx, arm64_movz(ARM64_SCRATCH, insn->imm & 0xFFFF, 0));
    if ((insn->imm >> 16) & 0xFFFF) {
      emit32(ctx, arm64_movk(ARM64_SCRATCH, (insn->imm >> 16) & 0xFFFF, 16));
    }
    emit32(ctx,
           arm64_sxtw(ARM64_SCRATCH, ARM64_SCRATCH)); // Sign extend to 64-bit
    emit32(ctx, arm64_cmp_reg(dst, ARM64_SCRATCH));
    record_jump(ctx, pc + 1 + insn->offset, 0xB); // LT condition (signed <)
    emit32(ctx, 0x5400000B);                      // b.lt #0 (placeholder)
    break;

  case BPF_JMP | BPF_JSLE | BPF_K:
    emit32(ctx, arm64_movz(ARM64_SCRATCH, insn->imm & 0xFFFF, 0));
    if ((insn->imm >> 16) & 0xFFFF) {
      emit32(ctx, arm64_movk(ARM64_SCRATCH, (insn->imm >> 16) & 0xFFFF, 16));
    }
    emit32(ctx,
           arm64_sxtw(ARM64_SCRATCH, ARM64_SCRATCH)); // Sign extend to 64-bit
    emit32(ctx, arm64_cmp_reg(dst, ARM64_SCRATCH));
    record_jump(ctx, pc + 1 + insn->offset, 0xD); // LE condition (signed <=)
    emit32(ctx, 0x5400000D);                      // b.le #0 (placeholder)
    break;

  case BPF_JMP | BPF_JSET | BPF_K:
    emit32(ctx, arm64_movz(ARM64_SCRATCH, insn->imm & 0xFFFF, 0));
    if ((insn->imm >> 16) & 0xFFFF) {
      emit32(ctx, arm64_movk(ARM64_SCRATCH, (insn->imm >> 16) & 0xFFFF, 16));
    }
    emit32(ctx,
           arm64_sxtw(ARM64_SCRATCH, ARM64_SCRATCH)); // Sign extend to 64-bit
    emit32(ctx, arm64_tst(dst, ARM64_SCRATCH));
    record_jump(ctx, pc + 1 + insn->offset, 0x1); // NE condition (result != 0)
    emit32(ctx, 0x54000001);                      // b.ne #0 (placeholder)
    break;

  // === MEMORY OPERATIONS ===
  // Load from memory (LDX)
  case BPF_LDX | BPF_MEM | BPF_B:
    // ldrb xdst, [xsrc, #offset]
    if (insn->offset >= 0 && insn->offset < 4096) {
      emit32(ctx, arm64_ldrb_imm(dst, src, insn->offset));
    } else if (insn->offset < 0 && insn->offset >= -256) {
      // ARM64 ldurb for negative offsets
      emit32(ctx,
             0x38400000 | ((insn->offset & 0x1FF) << 12) | (src << 5) | dst);
    } else {
      // Load offset to scratch, add to src, then load
      if (insn->offset < 0) {
        // For negative offsets, use movn to load the negative value
        emit32(ctx,
               0x92800000 | ((~insn->offset & 0xFFFF) << 5) | ARM64_SCRATCH);
      } else {
        emit32(ctx, arm64_movz(ARM64_SCRATCH, insn->offset & 0xFFFF, 0));
      }
      emit32(ctx, arm64_add_reg(ARM64_SCRATCH, src, ARM64_SCRATCH));
      emit32(ctx, arm64_ldrb_imm(dst, ARM64_SCRATCH, 0));
    }
    break;

  case BPF_LDX | BPF_MEM | BPF_H:
    // ldrh xdst, [xsrc, #offset]
    if (insn->offset >= 0 && insn->offset < 8192 && (insn->offset & 1) == 0) {
      emit32(ctx, arm64_ldrh_imm(dst, src, insn->offset));
    } else if (insn->offset < 0 && insn->offset >= -256) {
      // ARM64 ldurh for negative offsets
      emit32(ctx,
             0x78400000 | ((insn->offset & 0x1FF) << 12) | (src << 5) | dst);
    } else {
      if (insn->offset < 0) {
        emit32(ctx,
               0x92800000 | ((~insn->offset & 0xFFFF) << 5) | ARM64_SCRATCH);
      } else {
        emit32(ctx, arm64_movz(ARM64_SCRATCH, insn->offset & 0xFFFF, 0));
      }
      emit32(ctx, arm64_add_reg(ARM64_SCRATCH, src, ARM64_SCRATCH));
      emit32(ctx, arm64_ldrh_imm(dst, ARM64_SCRATCH, 0));
    }
    break;

  case BPF_LDX | BPF_MEM | BPF_W:
    // ldr wdst, [xsrc, #offset]
    if (insn->offset >= 0 && insn->offset < 16384 && (insn->offset & 3) == 0) {
      emit32(ctx, arm64_ldr32_imm(dst, src, insn->offset));
    } else if (insn->offset < 0 && insn->offset >= -256) {
      // ARM64 ldur for negative offsets (32-bit)
      emit32(ctx,
             0xB8400000 | ((insn->offset & 0x1FF) << 12) | (src << 5) | dst);
    } else {
      if (insn->offset < 0) {
        emit32(ctx,
               0x92800000 | ((~insn->offset & 0xFFFF) << 5) | ARM64_SCRATCH);
      } else {
        emit32(ctx, arm64_movz(ARM64_SCRATCH, insn->offset & 0xFFFF, 0));
      }
      emit32(ctx, arm64_add_reg(ARM64_SCRATCH, src, ARM64_SCRATCH));
      emit32(ctx, arm64_ldr32_imm(dst, ARM64_SCRATCH, 0));
    }
    break;

  case BPF_LDX | BPF_MEM | BPF_DW:
    // ldr xdst, [xsrc, #offset]
    if (insn->offset >= 0 && insn->offset < 4096 && (insn->offset & 7) == 0) {
      emit32(ctx, arm64_ldr_imm(dst, src, insn->offset));
    } else if (insn->offset < 0 && insn->offset >= -256) {
      // ARM64 ldur for negative offsets (64-bit)
      emit32(ctx,
             0xF8400000 | ((insn->offset & 0x1FF) << 12) | (src << 5) | dst);
    } else {
      if (insn->offset < 0) {
        emit32(ctx,
               0x92800000 | ((~insn->offset & 0xFFFF) << 5) | ARM64_SCRATCH);
      } else {
        emit32(ctx, arm64_movz(ARM64_SCRATCH, insn->offset & 0xFFFF, 0));
      }
      emit32(ctx, arm64_add_reg(ARM64_SCRATCH, src, ARM64_SCRATCH));
      emit32(ctx, arm64_ldr_imm(dst, ARM64_SCRATCH, 0));
    }
    break;

  // Store to memory (STX)
  case BPF_STX | BPF_MEM | BPF_B:
    // strb xsrc, [xdst, #offset]
    if (insn->offset >= 0 && insn->offset < 4096) {
      emit32(ctx, arm64_strb_imm(src, dst, insn->offset));
    } else if (insn->offset < 0 && insn->offset >= -256) {
      // ARM64 sturb for negative offsets
      emit32(ctx,
             0x38000000 | ((insn->offset & 0x1FF) << 12) | (dst << 5) | src);
    } else {
      if (insn->offset < 0) {
        emit32(ctx,
               0x92800000 | ((~insn->offset & 0xFFFF) << 5) | ARM64_SCRATCH);
      } else {
        emit32(ctx, arm64_movz(ARM64_SCRATCH, insn->offset & 0xFFFF, 0));
      }
      emit32(ctx, arm64_add_reg(ARM64_SCRATCH, dst, ARM64_SCRATCH));
      emit32(ctx, arm64_strb_imm(src, ARM64_SCRATCH, 0));
    }
    break;

  case BPF_STX | BPF_MEM | BPF_H:
    if (insn->offset >= 0 && insn->offset < 8192 && (insn->offset & 1) == 0) {
      emit32(ctx, arm64_strh_imm(src, dst, insn->offset));
    } else if (insn->offset < 0 && insn->offset >= -256) {
      // ARM64 sturh for negative offsets
      emit32(ctx,
             0x78000000 | ((insn->offset & 0x1FF) << 12) | (dst << 5) | src);
    } else {
      if (insn->offset < 0) {
        emit32(ctx,
               0x92800000 | ((~insn->offset & 0xFFFF) << 5) | ARM64_SCRATCH);
      } else {
        emit32(ctx, arm64_movz(ARM64_SCRATCH, insn->offset & 0xFFFF, 0));
      }
      emit32(ctx, arm64_add_reg(ARM64_SCRATCH, dst, ARM64_SCRATCH));
      emit32(ctx, arm64_strh_imm(src, ARM64_SCRATCH, 0));
    }
    break;

  case BPF_STX | BPF_MEM | BPF_W:
    if (insn->offset >= 0 && insn->offset < 16384 && (insn->offset & 3) == 0) {
      emit32(ctx, arm64_str32_imm(src, dst, insn->offset));
    } else if (insn->offset < 0 && insn->offset >= -256) {
      // ARM64 stur for negative offsets (32-bit)
      emit32(ctx,
             0xB8000000 | ((insn->offset & 0x1FF) << 12) | (dst << 5) | src);
    } else {
      if (insn->offset < 0) {
        emit32(ctx,
               0x92800000 | ((~insn->offset & 0xFFFF) << 5) | ARM64_SCRATCH);
      } else {
        emit32(ctx, arm64_movz(ARM64_SCRATCH, insn->offset & 0xFFFF, 0));
      }
      emit32(ctx, arm64_add_reg(ARM64_SCRATCH, dst, ARM64_SCRATCH));
      emit32(ctx, arm64_str32_imm(src, ARM64_SCRATCH, 0));
    }
    break;

  case BPF_STX | BPF_MEM | BPF_DW:
    if (insn->offset >= 0 && insn->offset < 4096 && (insn->offset & 7) == 0) {
      emit32(ctx, arm64_str_imm(src, dst, insn->offset));
    } else if (insn->offset < 0 && insn->offset >= -256) {
      // ARM64 stur for negative offsets (64-bit)
      emit32(ctx,
             0xF8000000 | ((insn->offset & 0x1FF) << 12) | (dst << 5) | src);
    } else {
      if (insn->offset < 0) {
        emit32(ctx,
               0x92800000 | ((~insn->offset & 0xFFFF) << 5) | ARM64_SCRATCH);
      } else {
        emit32(ctx, arm64_movz(ARM64_SCRATCH, insn->offset & 0xFFFF, 0));
      }
      emit32(ctx, arm64_add_reg(ARM64_SCRATCH, dst, ARM64_SCRATCH));
      emit32(ctx, arm64_str_imm(src, ARM64_SCRATCH, 0));
    }
    break;

  // Store immediate (ST)
  case BPF_ST | BPF_MEM | BPF_B:
  case BPF_ST | BPF_MEM | BPF_H:
  case BPF_ST | BPF_MEM | BPF_W:
  case BPF_ST | BPF_MEM | BPF_DW:
    // Load immediate to BPF scratch register (R9 -> x28)
    // We must use the register that STX will read from
    emit32(ctx, arm64_movz(bpf_to_arm64_reg(BPF_SCRATCH_REG),
                           insn->imm & 0xFFFF, 0));
    if ((insn->imm >> 16) & 0xFFFF) {
      emit32(ctx, arm64_movk(bpf_to_arm64_reg(BPF_SCRATCH_REG),
                             (insn->imm >> 16) & 0xFFFF, 16));
    }
    {
      ebpf_insn_t stx_insn = *insn;
      stx_insn.opcode = (opcode & ~BPF_ST) | BPF_STX;
      stx_insn.src_reg = BPF_SCRATCH_REG;
      ebpf_insn_t temp_insns[1] = {stx_insn};
      emit_insn(ctx, temp_insns, 0, 1);
    }
    break;

  // LDDW - 64-bit immediate load (special case, uses 2 instructions)
  case BPF_LD | BPF_IMM | BPF_DW:
    // This is a special instruction that spans 2 slots
    // Lower 32 bits in insn.imm, upper 32 bits in next insn.imm
    // Note: This instruction consumes 2 BPF instruction slots
    {
      uint32_t lower = (uint32_t)insn->imm;
      uint32_t upper = 0;

      // Get upper 32 bits from next instruction if available
      if (pc + 1 < count) {
        upper = (uint32_t)insns[pc + 1].imm;
      }

      // Load lower 16 bits
      emit32(ctx, arm64_movz(dst, lower & 0xFFFF, 0));

      // Load bits 16-31
      if ((lower >> 16) & 0xFFFF) {
        emit32(ctx, arm64_movk(dst, (lower >> 16) & 0xFFFF, 16));
      }

      // Load bits 32-47
      if (upper & 0xFFFF) {
        emit32(ctx, arm64_movk(dst, upper & 0xFFFF, 32));
      }

      // Load bits 48-63
      if ((upper >> 16) & 0xFFFF) {
        emit32(ctx, arm64_movk(dst, (upper >> 16) & 0xFFFF, 48));
      }
    }
    break;

  // EXIT
  case BPF_JMP | BPF_EXIT:
    // Jump to epilogue (will be patched)
    record_jump(ctx, ctx->insn_count, 0xFF); // 0xFF = unconditional
    emit32(ctx, 0x14000000);                 // b #0 (placeholder)
    break;

  // CALL helper function
  case BPF_JMP | BPF_CALL: {
    int32_t helper_id = insn->imm;

    // Check if helper table is available and ID is valid
    if (ctx->helpers == NULL || (size_t)helper_id >= ctx->helper_count ||
        ctx->helpers[helper_id] == NULL) {
      // No helper available - emit breakpoint
      fprintf(stderr, "JIT: BPF_CALL to invalid helper %d\n", helper_id);
      emit32(ctx, 0xd4200000); // brk #0
      break;
    }

    // Move BPF registers to ARM64 ABI registers for call
    // BPF r1-r5 (x20-x24) -> ARM64 x0-x4
    emit32(ctx, arm64_mov_reg(0, bpf_to_arm64_reg(1))); // mov x0, x20 (r1)
    emit32(ctx, arm64_mov_reg(1, bpf_to_arm64_reg(2))); // mov x1, x21 (r2)
    emit32(ctx, arm64_mov_reg(2, bpf_to_arm64_reg(3))); // mov x2, x22 (r3)
    emit32(ctx, arm64_mov_reg(3, bpf_to_arm64_reg(4))); // mov x3, x23 (r4)
    emit32(ctx, arm64_mov_reg(4, bpf_to_arm64_reg(5))); // mov x4, x24 (r5)

    // Load helper function address into scratch register (x9)
    // We embed the address directly using movz/movk sequence
    uint64_t helper_addr = (uint64_t)ctx->helpers[helper_id];
    emit32(ctx, arm64_movz(ARM64_SCRATCH, helper_addr & 0xFFFF, 0));
    emit32(ctx, arm64_movk(ARM64_SCRATCH, (helper_addr >> 16) & 0xFFFF, 16));
    emit32(ctx, arm64_movk(ARM64_SCRATCH, (helper_addr >> 32) & 0xFFFF, 32));
    emit32(ctx, arm64_movk(ARM64_SCRATCH, (helper_addr >> 48) & 0xFFFF, 48));

    // Call the helper function
    emit32(ctx, arm64_blr(ARM64_SCRATCH)); // blr x9

    // Move return value from x0 to BPF r0 (x19)
    emit32(ctx, arm64_mov_reg(bpf_to_arm64_reg(0), 0)); // mov x19, x0
    break;
  }

  default:
    // Unimplemented - emit breakpoint for now
    fprintf(stderr, "JIT: Unimplemented opcode 0x%02x\n", opcode);
    emit32(ctx, 0xd4200000); // brk #0
    break;
  }
}

// Resolve all recorded jumps
static void resolve_jumps(jit_ctx_t *ctx, size_t epilogue_offset) {
  for (size_t i = 0; i < ctx->jump_count; i++) {
    size_t arm64_off = ctx->jumps[i].arm64_offset;
    size_t bpf_target = ctx->jumps[i].bpf_target;
    uint8_t cond = ctx->jumps[i].cond;

    // Calculate target offset
    size_t target_off;
    if (bpf_target >= ctx->insn_count) {
      // Jump to epilogue
      target_off = epilogue_offset;
    } else {
      target_off = ctx->insn_offsets[bpf_target];
    }

    // Calculate relative offset (in instructions, not bytes)
    int32_t rel_off = (int32_t)(target_off - arm64_off) / 4;

    // Patch the instruction
    uint32_t *insn_ptr = (uint32_t *)(ctx->code_buf + arm64_off);
    if (cond == 0xFF) {
      // Unconditional branch: b #offset
      *insn_ptr = arm64_b(rel_off * 4);
    } else {
      // Conditional branch (b.cond, cbz, cbnz)
      // All use bits 5-23 for 19-bit offset
      uint32_t insn = *insn_ptr;
      insn &= ~0x00FFFFE0;                // Clear bits 5-23
      insn |= ((rel_off & 0x7FFFF) << 5); // Set new offset
      *insn_ptr = insn;
    }
  }
}

// Main JIT compilation function
jit_func_t jit_compile(const ebpf_insn_t *insns, size_t count) {
  if (!insns || count == 0 || count > EBPF_MAX_INSNS) {
    return NULL;
  }

  jit_ctx_t ctx = {0};

  // Allocate executable memory (estimate: 32 bytes per BPF instruction +
  // prologue/epilogue)
  size_t estimated_size = count * 32 + 512;
  ctx.code_buf = mmap(NULL, estimated_size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (ctx.code_buf == MAP_FAILED) {
    return NULL;
  }

  ctx.code_cap = estimated_size;
  ctx.insn_offsets = calloc(count, sizeof(size_t));
  ctx.insn_count = count;

  // Pass 1: Emit prologue
  emit_prologue(&ctx);

  // Pass 2: Emit instructions
  for (size_t i = 0; i < count; i++) {
    ctx.insn_offsets[i] = ctx.code_len;
    emit_insn(&ctx, insns, i, count);

    // Skip the next instruction if this was a LDDW (64-bit immediate load)
    if ((insns[i].opcode & 0xFF) == (BPF_LD | BPF_IMM | BPF_DW)) {
      i++; // Skip the next instruction which contains upper 32 bits
      if (i < count) {
        ctx.insn_offsets[i] = ctx.code_len; // Both slots map to same offset
      }
    }
  }

  // Emit epilogue
  size_t epilogue_offset = ctx.code_len;
  emit_epilogue(&ctx);

  // Pass 3: Resolve jumps
  resolve_jumps(&ctx, epilogue_offset);

  // Make memory executable
  if (mprotect(ctx.code_buf, ctx.code_len, PROT_READ | PROT_EXEC) != 0) {
    munmap(ctx.code_buf, estimated_size);
    free(ctx.insn_offsets);
    free(ctx.jumps);
    return NULL;
  }

  // Cleanup
  free(ctx.insn_offsets);
  free(ctx.jumps);

  return (jit_func_t)ctx.code_buf;
}

// JIT compilation with helper function support
jit_func_t jit_compile_with_helpers(const ebpf_insn_t *insns, size_t count,
                                    jit_helper_func_t *helpers,
                                    size_t helper_count) {
  if (!insns || count == 0 || count > EBPF_MAX_INSNS) {
    return NULL;
  }

  jit_ctx_t ctx = {0};
  ctx.helpers = helpers;
  ctx.helper_count = helper_count;

  // Allocate executable memory (estimate: 32 bytes per BPF instruction +
  // prologue/epilogue)
  size_t estimated_size = count * 32 + 512;
  ctx.code_buf = mmap(NULL, estimated_size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (ctx.code_buf == MAP_FAILED) {
    return NULL;
  }

  ctx.code_cap = estimated_size;
  ctx.insn_offsets = calloc(count, sizeof(size_t));
  ctx.insn_count = count;

  // Pass 1: Emit prologue
  emit_prologue(&ctx);

  // Pass 2: Emit instructions
  for (size_t i = 0; i < count; i++) {
    ctx.insn_offsets[i] = ctx.code_len;
    emit_insn(&ctx, insns, i, count);

    // Skip the next instruction if this was a LDDW (64-bit immediate load)
    if ((insns[i].opcode & 0xFF) == (BPF_LD | BPF_IMM | BPF_DW)) {
      i++; // Skip the next instruction which contains upper 32 bits
      if (i < count) {
        ctx.insn_offsets[i] = ctx.code_len; // Both slots map to same offset
      }
    }
  }

  // Emit epilogue
  size_t epilogue_offset = ctx.code_len;
  emit_epilogue(&ctx);

  // Pass 3: Resolve jumps
  resolve_jumps(&ctx, epilogue_offset);

  // Make memory executable
  if (mprotect(ctx.code_buf, ctx.code_len, PROT_READ | PROT_EXEC) != 0) {
    munmap(ctx.code_buf, estimated_size);
    free(ctx.insn_offsets);
    free(ctx.jumps);
    return NULL;
  }

  // Cleanup
  free(ctx.insn_offsets);
  free(ctx.jumps);

  return (jit_func_t)ctx.code_buf;
}

// Free JIT compiled code
void jit_free(jit_func_t func, size_t size) {
  if (func) {
    munmap((void *)func, size);
  }
}
