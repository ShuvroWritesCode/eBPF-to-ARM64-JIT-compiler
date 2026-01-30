#ifndef JIT_H
#define JIT_H

#include "ebpf.h"
#include <stddef.h>
#include <stdint.h>

// JIT compiled function signature
// Takes a context pointer, returns result in r0
typedef uint64_t (*jit_func_t)(void *ctx);

// Helper function signature (5 arguments, matching BPF calling convention)
typedef uint64_t (*jit_helper_func_t)(uint64_t r1, uint64_t r2, uint64_t r3,
                                      uint64_t r4, uint64_t r5);

// JIT compilation API
// Basic compilation (no helper support)
jit_func_t jit_compile(const ebpf_insn_t *insns, size_t count);

// Compilation with helper function support
// helpers: array of helper function pointers indexed by helper ID
// helper_count: size of the helpers array
jit_func_t jit_compile_with_helpers(const ebpf_insn_t *insns, size_t count,
                                    jit_helper_func_t *helpers,
                                    size_t helper_count);

void jit_free(jit_func_t func, size_t size);

#endif // JIT_H
