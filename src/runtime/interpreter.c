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
    // ALU64 Immediate
    case BPF_ALU64 | BPF_ADD | BPF_K:
      regs[dst] += insn.imm;
      break;
    case BPF_ALU64 | BPF_SUB | BPF_K:
      regs[dst] -= insn.imm;
      break;
    case BPF_ALU64 | BPF_MOV | BPF_K:
      regs[dst] = insn.imm;
      break;

    // ALU64 Register
    case BPF_ALU64 | BPF_ADD | BPF_X:
      regs[dst] += regs[src];
      break;
    case BPF_ALU64 | BPF_SUB | BPF_X:
      regs[dst] -= regs[src];
      break;
    case BPF_ALU64 | BPF_MOV | BPF_X:
      regs[dst] = regs[src];
      break;

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
