#include "ebpf.h"
#include "runtime.h"
#include <string.h>

void ebpf_vm_init(ebpf_vm_t *vm, const ebpf_insn_t *insns, size_t count) {
  memset(vm, 0, sizeof(ebpf_vm_t));
  vm->insns = insns;
  vm->insn_count = count;
}

int ebpf_exec(ebpf_vm_t *vm, uint64_t *result) {
  uint64_t *regs = vm->regs;
  size_t pc = 0;

  // Initialize frame pointer to end of stack
  regs[10] = (uint64_t)(vm->stack + EBPF_STACK_SIZE);

  // Very basic instruction loop
  while (pc < vm->insn_count) {
    ebpf_insn_t insn = vm->insns[pc];
    uint8_t opcode = insn.opcode;
    uint8_t dst = insn.dst_reg;
    uint8_t src = insn.src_reg;

    switch (opcode) {
    // === ALU64 IMMEDIATE ===
    case BPF_ALU64 | BPF_ADD | BPF_K:
      regs[dst] += insn.imm;
      break;
    case BPF_ALU64 | BPF_SUB | BPF_K:
      regs[dst] -= insn.imm;
      break;
    case BPF_ALU64 | BPF_MUL | BPF_K:
      regs[dst] *= insn.imm;
      break;
    case BPF_ALU64 | BPF_DIV | BPF_K:
      if (insn.imm == 0)
        return -1; // Div by zero
      regs[dst] /= insn.imm;
      break;
    case BPF_ALU64 | BPF_OR | BPF_K:
      regs[dst] |= insn.imm;
      break;
    case BPF_ALU64 | BPF_AND | BPF_K:
      regs[dst] &= insn.imm;
      break;
    case BPF_ALU64 | BPF_LSH | BPF_K:
      regs[dst] <<= insn.imm;
      break;
    case BPF_ALU64 | BPF_RSH | BPF_K:
      regs[dst] >>= insn.imm;
      break;
    case BPF_ALU64 | BPF_NEG | BPF_K:
      regs[dst] = -regs[dst];
      break; // src ignored
    case BPF_ALU64 | BPF_MOD | BPF_K:
      if (insn.imm == 0)
        return -1;
      regs[dst] %= insn.imm;
      break;
    case BPF_ALU64 | BPF_XOR | BPF_K:
      regs[dst] ^= insn.imm;
      break;
    case BPF_ALU64 | BPF_MOV | BPF_K:
      regs[dst] = insn.imm;
      break;
    case BPF_ALU64 | BPF_ARSH | BPF_K:
      regs[dst] = (int64_t)regs[dst] >> insn.imm;
      break;

    // === ALU64 REGISTER ===
    case BPF_ALU64 | BPF_ADD | BPF_X:
      regs[dst] += regs[src];
      break;
    case BPF_ALU64 | BPF_SUB | BPF_X:
      regs[dst] -= regs[src];
      break;
    case BPF_ALU64 | BPF_MUL | BPF_X:
      regs[dst] *= regs[src];
      break;
    case BPF_ALU64 | BPF_DIV | BPF_X:
      if (regs[src] == 0)
        return -1;
      regs[dst] /= regs[src];
      break;
    case BPF_ALU64 | BPF_OR | BPF_X:
      regs[dst] |= regs[src];
      break;
    case BPF_ALU64 | BPF_AND | BPF_X:
      regs[dst] &= regs[src];
      break;
    case BPF_ALU64 | BPF_LSH | BPF_X:
      regs[dst] <<= regs[src];
      break;
    case BPF_ALU64 | BPF_RSH | BPF_X:
      regs[dst] >>= regs[src];
      break;
    case BPF_ALU64 | BPF_NEG | BPF_X:
      regs[dst] = -regs[dst];
      break;
    case BPF_ALU64 | BPF_MOD | BPF_X:
      if (regs[src] == 0)
        return -1;
      regs[dst] %= regs[src];
      break;
    case BPF_ALU64 | BPF_XOR | BPF_X:
      regs[dst] ^= regs[src];
      break;
    case BPF_ALU64 | BPF_MOV | BPF_X:
      regs[dst] = regs[src];
      break;
    case BPF_ALU64 | BPF_ARSH | BPF_X:
      regs[dst] = (int64_t)regs[dst] >> regs[src];
      break;

    // === ALU32 IMMEDIATE ===
    case BPF_ALU | BPF_ADD | BPF_K:
      regs[dst] = (uint32_t)regs[dst] + (uint32_t)insn.imm;
      break;
    case BPF_ALU | BPF_SUB | BPF_K:
      regs[dst] = (uint32_t)regs[dst] - (uint32_t)insn.imm;
      break;
    case BPF_ALU | BPF_MUL | BPF_K:
      regs[dst] = (uint32_t)regs[dst] * (uint32_t)insn.imm;
      break;
    case BPF_ALU | BPF_DIV | BPF_K:
      if (insn.imm == 0)
        return -1;
      regs[dst] = (uint32_t)regs[dst] / (uint32_t)insn.imm;
      break;
    case BPF_ALU | BPF_OR | BPF_K:
      regs[dst] = (uint32_t)regs[dst] | (uint32_t)insn.imm;
      break;
    case BPF_ALU | BPF_AND | BPF_K:
      regs[dst] = (uint32_t)regs[dst] & (uint32_t)insn.imm;
      break;
    case BPF_ALU | BPF_LSH | BPF_K:
      regs[dst] = (uint32_t)regs[dst] << (uint32_t)insn.imm;
      break;
    case BPF_ALU | BPF_RSH | BPF_K:
      regs[dst] = (uint32_t)regs[dst] >> (uint32_t)insn.imm;
      break;
    case BPF_ALU | BPF_NEG | BPF_K:
      regs[dst] = (uint32_t)(-(int32_t)regs[dst]);
      break;
    case BPF_ALU | BPF_MOD | BPF_K:
      if (insn.imm == 0)
        return -1;
      regs[dst] = (uint32_t)regs[dst] % (uint32_t)insn.imm;
      break;
    case BPF_ALU | BPF_XOR | BPF_K:
      regs[dst] = (uint32_t)regs[dst] ^ (uint32_t)insn.imm;
      break;
    case BPF_ALU | BPF_MOV | BPF_K:
      regs[dst] = (uint32_t)insn.imm;
      break;
    // Note: ARSH32 operates on signed 32-bit values
    case BPF_ALU | BPF_ARSH | BPF_K:
      regs[dst] = (uint64_t)(int64_t)((int32_t)regs[dst] >> insn.imm);
      break;

    // === ALU32 REGISTER ===
    // (Similar pattern for ALU32 reg - implementing core set first)
    case BPF_ALU | BPF_MOV | BPF_X:
      regs[dst] = (uint32_t)regs[src];
      break;
    case BPF_ALU | BPF_ADD | BPF_X:
      regs[dst] = (uint32_t)regs[dst] + (uint32_t)regs[src];
      break;

    // === JUMP Immediate ===
    case BPF_JMP | BPF_JA:
      pc += insn.offset;
      break;
    case BPF_JMP | BPF_JEQ | BPF_K:
      if (regs[dst] == (uint64_t)insn.imm)
        pc += insn.offset;
      break;
    case BPF_JMP | BPF_JNE | BPF_K:
      if (regs[dst] != (uint64_t)insn.imm)
        pc += insn.offset;
      break;
    case BPF_JMP | BPF_JGT | BPF_K:
      if (regs[dst] > (uint64_t)insn.imm)
        pc += insn.offset;
      break;
    case BPF_JMP | BPF_JGE | BPF_K:
      if (regs[dst] >= (uint64_t)insn.imm)
        pc += insn.offset;
      break;
    case BPF_JMP | BPF_JLT | BPF_K:
      if (regs[dst] < (uint64_t)insn.imm)
        pc += insn.offset;
      break;
    case BPF_JMP | BPF_JLE | BPF_K:
      if (regs[dst] <= (uint64_t)insn.imm)
        pc += insn.offset;
      break;
    case BPF_JMP | BPF_JSET | BPF_K:
      if (regs[dst] & (uint64_t)insn.imm)
        pc += insn.offset;
      break;
    case BPF_JMP | BPF_JSGT | BPF_K:
      if ((int64_t)regs[dst] > (int64_t)insn.imm)
        pc += insn.offset;
      break;
    case BPF_JMP | BPF_JSGE | BPF_K:
      if ((int64_t)regs[dst] >= (int64_t)insn.imm)
        pc += insn.offset;
      break;
    case BPF_JMP | BPF_JSLT | BPF_K:
      if ((int64_t)regs[dst] < (int64_t)insn.imm)
        pc += insn.offset;
      break;
    case BPF_JMP | BPF_JSLE | BPF_K:
      if ((int64_t)regs[dst] <= (int64_t)insn.imm)
        pc += insn.offset;
      break;

    // === JUMP Register ===
    case BPF_JMP | BPF_JEQ | BPF_X:
      if (regs[dst] == regs[src])
        pc += insn.offset;
      break;
    case BPF_JMP | BPF_JNE | BPF_X:
      if (regs[dst] != regs[src])
        pc += insn.offset;
      break;
    case BPF_JMP | BPF_JGT | BPF_X:
      if (regs[dst] > regs[src])
        pc += insn.offset;
      break;
    case BPF_JMP | BPF_JGE | BPF_X:
      if (regs[dst] >= regs[src])
        pc += insn.offset;
      break;
    case BPF_JMP | BPF_JLT | BPF_X:
      if (regs[dst] < regs[src])
        pc += insn.offset;
      break;
    case BPF_JMP | BPF_JLE | BPF_X:
      if (regs[dst] <= regs[src])
        pc += insn.offset;
      break;
    case BPF_JMP | BPF_JSET | BPF_X:
      if (regs[dst] & regs[src])
        pc += insn.offset;
      break;
    case BPF_JMP | BPF_JSGT | BPF_X:
      if ((int64_t)regs[dst] > (int64_t)regs[src])
        pc += insn.offset;
      break;
    case BPF_JMP | BPF_JSGE | BPF_X:
      if ((int64_t)regs[dst] >= (int64_t)regs[src])
        pc += insn.offset;
      break;
    case BPF_JMP | BPF_JSLT | BPF_X:
      if ((int64_t)regs[dst] < (int64_t)regs[src])
        pc += insn.offset;
      break;
    case BPF_JMP | BPF_JSLE | BPF_X:
      if ((int64_t)regs[dst] <= (int64_t)regs[src])
        pc += insn.offset;
      break;

    // === JMP32 Immediate (only 32-bit comparison) ===
    case BPF_JMP32 | BPF_JA:
      pc += insn.offset;
      break;
    case BPF_JMP32 | BPF_JEQ | BPF_K:
      if ((uint32_t)regs[dst] == (uint32_t)insn.imm)
        pc += insn.offset;
      break;
    case BPF_JMP32 | BPF_JNE | BPF_K:
      if ((uint32_t)regs[dst] != (uint32_t)insn.imm)
        pc += insn.offset;
      break;
    case BPF_JMP32 | BPF_JGT | BPF_K:
      if ((uint32_t)regs[dst] > (uint32_t)insn.imm)
        pc += insn.offset;
      break;
    case BPF_JMP32 | BPF_JGE | BPF_K:
      if ((uint32_t)regs[dst] >= (uint32_t)insn.imm)
        pc += insn.offset;
      break;
    case BPF_JMP32 | BPF_JLT | BPF_K:
      if ((uint32_t)regs[dst] < (uint32_t)insn.imm)
        pc += insn.offset;
      break;
    case BPF_JMP32 | BPF_JLE | BPF_K:
      if ((uint32_t)regs[dst] <= (uint32_t)insn.imm)
        pc += insn.offset;
      break;
    case BPF_JMP32 | BPF_JSET | BPF_K:
      if ((uint32_t)regs[dst] & (uint32_t)insn.imm)
        pc += insn.offset;
      break;
    case BPF_JMP32 | BPF_JSGT | BPF_K:
      if ((int32_t)regs[dst] > (int32_t)insn.imm)
        pc += insn.offset;
      break;
    case BPF_JMP32 | BPF_JSGE | BPF_K:
      if ((int32_t)regs[dst] >= (int32_t)insn.imm)
        pc += insn.offset;
      break;
    case BPF_JMP32 | BPF_JSLT | BPF_K:
      if ((int32_t)regs[dst] < (int32_t)insn.imm)
        pc += insn.offset;
      break;
    case BPF_JMP32 | BPF_JSLE | BPF_K:
      if ((int32_t)regs[dst] <= (int32_t)insn.imm)
        pc += insn.offset;
      break;

    // === MEMORY LOAD (LDX) ===
    case BPF_LDX | BPF_MEM | BPF_B:
      regs[dst] = *(uint8_t *)(uintptr_t)(regs[src] + insn.offset);
      break;
    case BPF_LDX | BPF_MEM | BPF_H:
      regs[dst] = *(uint16_t *)(uintptr_t)(regs[src] + insn.offset);
      break;
    case BPF_LDX | BPF_MEM | BPF_W:
      regs[dst] = *(uint32_t *)(uintptr_t)(regs[src] + insn.offset);
      break;
    case BPF_LDX | BPF_MEM | BPF_DW:
      regs[dst] = *(uint64_t *)(uintptr_t)(regs[src] + insn.offset);
      break;

    // === MEMORY STORE (STX) ===
    case BPF_STX | BPF_MEM | BPF_B:
      *(uint8_t *)(uintptr_t)(regs[dst] + insn.offset) = (uint8_t)regs[src];
      break;
    case BPF_STX | BPF_MEM | BPF_H:
      *(uint16_t *)(uintptr_t)(regs[dst] + insn.offset) = (uint16_t)regs[src];
      break;
    case BPF_STX | BPF_MEM | BPF_W:
      *(uint32_t *)(uintptr_t)(regs[dst] + insn.offset) = (uint32_t)regs[src];
      break;
    case BPF_STX | BPF_MEM | BPF_DW:
      *(uint64_t *)(uintptr_t)(regs[dst] + insn.offset) = (uint64_t)regs[src];
      break;

    // === MEMORY STORE IMM (ST) ===
    case BPF_ST | BPF_MEM | BPF_B:
      *(uint8_t *)(uintptr_t)(regs[dst] + insn.offset) = (uint8_t)insn.imm;
      break;
    case BPF_ST | BPF_MEM | BPF_H:
      *(uint16_t *)(uintptr_t)(regs[dst] + insn.offset) = (uint16_t)insn.imm;
      break;
    case BPF_ST | BPF_MEM | BPF_W:
      *(uint32_t *)(uintptr_t)(regs[dst] + insn.offset) = (uint32_t)insn.imm;
      break;
    case BPF_ST | BPF_MEM | BPF_DW:
      *(uint64_t *)(uintptr_t)(regs[dst] + insn.offset) = (uint64_t)insn.imm;
      break;

    // === SPECIAL: LDDW (64-bit Immediate Load) ===
    case BPF_LD | BPF_IMM | BPF_DW:
      if (pc + 1 >= vm->insn_count)
        return -1; // Missing second half
      regs[dst] = (uint32_t)insn.imm | ((uint64_t)vm->insns[pc + 1].imm << 32);
      pc++; // Skip next instruction
      break;

    // JMP32 Register skipped for brevity but would follow same pattern

    // EXIT
    case BPF_JMP | BPF_EXIT:
      *result = regs[0];
      return 0;

    default:
      // Unimplemented opcode
      return -1;
    }
    pc++;
  }

  return -2; // Fell off end
}
