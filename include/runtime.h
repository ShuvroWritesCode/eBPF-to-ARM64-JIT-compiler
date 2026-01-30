#ifndef RUNTIME_H
#define RUNTIME_H

#include "ebpf.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Runtime context for program execution
typedef struct {
  uint64_t regs[EBPF_MAX_REGS]; // R0-R10
  uint8_t stack[EBPF_STACK_SIZE];
  const ebpf_insn_t *insns;
  size_t insn_count;

  // Memory access callbacks (can be NULL for direct access)
  void *user_context;
  // ... expansion for helpers later
} ebpf_vm_t;

// Standard helper function signature
typedef uint64_t (*ebpf_helper_func_t)(uint64_t r1, uint64_t r2, uint64_t r3,
                                       uint64_t r4, uint64_t r5);

// Public API
void ebpf_vm_init(ebpf_vm_t *vm, const ebpf_insn_t *insns, size_t count);
int ebpf_exec(ebpf_vm_t *vm, uint64_t *result);

#endif // RUNTIME_H
