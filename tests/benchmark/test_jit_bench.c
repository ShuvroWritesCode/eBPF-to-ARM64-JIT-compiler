#include "ebpf.h"
#include "jit.h"
#include "tp4091.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "nvme.h"

// Define BPF registers if not available
#ifndef BPF_REG_0
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
#endif

// Benchmark program: Decrement loop
// r1 starts with N
// loop:
//   sub r1, 1
//   jnz r1, loop
//   exit
static const ebpf_insn_t bench_program[] = {
    // r1 is loop counter (passed as argument or initialized?)
    // Let's initialize r1 = 100,000,000 (0x05F5E100)

    // r1 = 100,000,000
    {BPF_LD | BPF_IMM | BPF_DW, BPF_REG_1, 0, 0, 100000000},
    {0, 0, 0, 0, 0}, // Second part of LDDW (0)

    // loop:
    // r1 = r1 - 1
    {BPF_ALU64 | BPF_SUB | BPF_K, BPF_REG_1, 0, 0, 1},

    // if r1 != 0 goto loop (-2 instructions: sub and jmp)
    // JNE r1, 0, -2
    {BPF_JMP | BPF_JNE | BPF_K, BPF_REG_1, 0, -2, 0},

    // exit
    {BPF_JMP | BPF_EXIT, 0, 0, 0, 0},
};

double get_time_sec() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main() {
  printf("JIT Benchmark: ALU Loop\n");
  printf("-----------------------\n");

  // Initialize NVMe (needed for TP4091)
  nvme_init();

  // Initialize TP4091
  tp4091_init();

  // Create a dummy namespace
  nvme_create_namespace(1, 1024);

  // Load program
  tp4091_load_cmd_t load_cmd = {
      .program_id = 1,
      .code_size = sizeof(bench_program),
      .code = (uint8_t *)bench_program,
  };

  if (tp4091_load(&load_cmd) != 0) {
    fprintf(stderr, "Failed to load benchmark program\n");
    return 1;
  }

  // Activate program
  tp4091_activate_cmd_t activate_cmd = {.program_id = 1, .namespace_id = 1};
  if (tp4091_activate(&activate_cmd) != 0) {
    fprintf(stderr, "Failed to activate program\n");
    return 1;
  }

  // Execute
  printf("Running loop with 100,000,000 iterations...\n");

  tp4091_execute_cmd_t exec_cmd = {.program_id = 1,
                                   .buffer = NULL, // Context not used
                                   .lba = 0,
                                   .block_count = 0};
  uint64_t result;

  double start = get_time_sec();
  if (tp4091_execute(&exec_cmd, &result) != 0) {
    fprintf(stderr, "Execution failed\n");
    return 1;
  }
  double end = get_time_sec();

  double duration = end - start;
  double ops = 100000000.0;

#include "runtime.h"

  printf("Done in %.6f seconds\n", duration);
  printf("JIT Throughput: %.2f Million Ops/sec\n", (ops / duration) / 1e6);

  // --- Interpreter Benchmark ---
  printf("\nInterpreter Benchmark: ALU Loop\n");
  ebpf_vm_t vm;
  // Reduce iterations for interpreter as it is slower
  // Modify the program in place? No, it's const.
  // Copy program buffer and modify IMM
  ebpf_insn_t *interp_prog = malloc(sizeof(bench_program));
  memcpy(interp_prog, bench_program, sizeof(bench_program));

  // Set r1 = 10,000,000 (10x less) for sanity
  // Instruction 0 is LD_IMM64. r1 is 1st arg. imm is 100000000.
  interp_prog[0].imm = 10000000;

  ebpf_vm_init(&vm, interp_prog, sizeof(bench_program) / sizeof(ebpf_insn_t));

  printf("Running loop with 10,000,000 iterations...\n");
  uint64_t interp_result;
  start = get_time_sec();
  if (ebpf_exec(&vm, &interp_result) != 0) {
    fprintf(stderr, "Interpreter execution failed\n");
  } else {
    end = get_time_sec();
    duration = end - start;
    ops = 10000000.0;
    printf("Done in %.6f seconds\n", duration);
    printf("Interpreter Throughput: %.2f Million Ops/sec\n",
           (ops / duration) / 1e6);
  }
  free(interp_prog);

  // Cleanup
  nvme_cleanup();
  tp4091_cleanup(); // Added cleanup

  return 0;
}
