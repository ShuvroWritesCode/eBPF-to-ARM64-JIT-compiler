#include "tp4091.h"
#include "ebpf.h"
#include "helpers.h"
#include "maps.h"
#include "nvme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global program table
static tp4091_program_t programs[MAX_PROGRAMS];

// Currently executing program (TLS-like context for helpers)
static tp4091_program_t *current_executing_program = NULL;

// Initialize TP4091 program manager
int tp4091_init(void) {
  memset(programs, 0, sizeof(programs));
  for (int i = 0; i < MAX_PROGRAMS; i++) {
    programs[i].state = PROGRAM_STATE_EMPTY;
  }
  return maps_init();
}

// Cleanup TP4091 program manager
void tp4091_cleanup(void) {
  for (int i = 0; i < MAX_PROGRAMS; i++) {
    if (programs[i].state != PROGRAM_STATE_EMPTY && programs[i].jit_func) {
      jit_free(programs[i].jit_func, programs[i].code_size * 32 + 512);
      programs[i].jit_func = NULL;
    }
  }
  maps_cleanup();
}

// Load an eBPF program
int tp4091_load(const tp4091_load_cmd_t *cmd) {
  if (!cmd || !cmd->code || cmd->code_size == 0) {
    return -1;
  }

  if (cmd->program_id >= MAX_PROGRAMS) {
    return -1;
  }

  tp4091_program_t *prog = &programs[cmd->program_id];

  // Check if slot is available
  if (prog->state != PROGRAM_STATE_EMPTY) {
    fprintf(stderr, "Program %u already loaded\n", cmd->program_id);
    return -1;
  }

  // Compile the program
  ebpf_insn_t *insns = (ebpf_insn_t *)cmd->code;
  size_t insn_count = cmd->code_size / sizeof(ebpf_insn_t);

  // Define helpers
  // Helper IDs are 1-based, we need size 9 for map helpers (IDs 6-8)
  jit_helper_func_t helpers[9] = {0};
  helpers[BPF_FUNC_storage_read] = (jit_helper_func_t)helper_storage_read;
  helpers[BPF_FUNC_storage_write] = (jit_helper_func_t)helper_storage_write;
  helpers[BPF_FUNC_storage_flush] = (jit_helper_func_t)helper_storage_flush;
  helpers[BPF_FUNC_storage_get_ns] = (jit_helper_func_t)helper_storage_get_ns;
  helpers[BPF_FUNC_storage_log] = (jit_helper_func_t)helper_storage_log;
  helpers[BPF_FUNC_map_lookup_elem] = (jit_helper_func_t)helper_map_lookup_elem;
  helpers[BPF_FUNC_map_update_elem] = (jit_helper_func_t)helper_map_update_elem;
  helpers[BPF_FUNC_map_delete_elem] = (jit_helper_func_t)helper_map_delete_elem;

  jit_func_t jit = jit_compile_with_helpers(insns, insn_count, helpers, 9);
  if (!jit) {
    fprintf(stderr, "Failed to compile program %u\n", cmd->program_id);
    return -1;
  }

  // Store program
  prog->program_id = cmd->program_id;
  prog->state = PROGRAM_STATE_LOADED;
  prog->jit_func = jit;
  prog->code_size = cmd->code_size;
  prog->namespace_id = 0;

  printf("[TP4091] Loaded program %u (%zu instructions)\n", cmd->program_id,
         insn_count);

  return 0;
}

// Activate a program on a namespace
int tp4091_activate(const tp4091_activate_cmd_t *cmd) {
  if (!cmd || cmd->program_id >= MAX_PROGRAMS) {
    return -1;
  }

  tp4091_program_t *prog = &programs[cmd->program_id];

  if (prog->state != PROGRAM_STATE_LOADED) {
    fprintf(stderr, "Program %u not loaded\n", cmd->program_id);
    return -1;
  }

  // Verify namespace exists
  nvme_namespace_t *ns = nvme_get_namespace(cmd->namespace_id);
  if (!ns || ns->state != NS_STATE_ACTIVE) {
    fprintf(stderr, "Namespace %u not found\n", cmd->namespace_id);
    return -1;
  }

  // Activate program
  prog->state = PROGRAM_STATE_ACTIVE;
  prog->namespace_id = cmd->namespace_id;

  // Associate with namespace
  nvme_set_active_program(cmd->namespace_id, cmd->program_id);

  printf("[TP4091] Activated program %u on namespace %u\n", cmd->program_id,
         cmd->namespace_id);

  return 0;
}

// Execute a program
int tp4091_execute(const tp4091_execute_cmd_t *cmd, uint64_t *result) {
  if (!cmd || !result || cmd->program_id >= MAX_PROGRAMS) {
    return -1;
  }

  tp4091_program_t *prog = &programs[cmd->program_id];

  if (prog->state != PROGRAM_STATE_ACTIVE) {
    fprintf(stderr, "Program %u not active\n", cmd->program_id);
    return -1;
  }

  // Execute JIT-compiled program
  // Context could include LBA, block count, buffer, etc.
  current_executing_program = prog;
  *result = prog->jit_func(cmd->buffer);
  current_executing_program = NULL;

  printf("[TP4091] Executed program %u, result=%lu\n", cmd->program_id,
         *result);

  return 0;
}

// Unload a program
int tp4091_unload(uint32_t program_id) {
  if (program_id >= MAX_PROGRAMS) {
    return -1;
  }

  tp4091_program_t *prog = &programs[program_id];

  if (prog->state == PROGRAM_STATE_EMPTY) {
    return -1;
  }

  // Free JIT code
  if (prog->jit_func) {
    jit_free(prog->jit_func, prog->code_size * 32 + 512);
  }

  // Clear entry
  memset(prog, 0, sizeof(tp4091_program_t));
  prog->state = PROGRAM_STATE_EMPTY;

  printf("[TP4091] Unloaded program %u\n", program_id);

  return 0;
}

// Get program by ID
tp4091_program_t *tp4091_get_program(uint32_t program_id) {
  if (program_id >= MAX_PROGRAMS) {
    return NULL;
  }

  tp4091_program_t *prog = &programs[program_id];
  return (prog->state != PROGRAM_STATE_EMPTY) ? prog : NULL;
}

// Get currently executing program
tp4091_program_t *tp4091_get_current_program(void) {
  return current_executing_program;
}
