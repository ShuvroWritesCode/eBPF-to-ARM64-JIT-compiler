/**
 * QEMU NVMe JIT Integration Patch
 *
 * This is a standalone reference implementation showing how to hook
 * libjit.a into QEMU's hw/nvme/ctrl.c for computational storage.
 *
 * This file demonstrates the integration pattern and can be adapted
 * for actual QEMU source modification.
 *
 * Build: Place in hw/nvme/ and link with libjit.a
 */

#ifndef QEMU_NVME_JIT_PATCH_H
#define QEMU_NVME_JIT_PATCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Include libjit headers for type definitions */
#include "ebpf.h"
#include "helpers.h"
#include "jit.h"
#include "maps.h"
#include "tp4091.h"

/* TP4091 Commands */
#define NVME_ADM_CMD_TP4091_LOAD 0xC0
#define NVME_ADM_CMD_TP4091_ACTIVATE 0xC1
#define NVME_ADM_CMD_TP4091_EXECUTE 0xC2
#define NVME_ADM_CMD_TP4091_UNLOAD 0xC3

/* Computational Storage Instance (CSI) */
#define NVME_CSI_NVM 0x00     /* Standard NVM */
#define NVME_CSI_COMPUTE 0x80 /* Computational Storage */

/* Per-namespace program state */
typedef struct NvmeJitProgram {
  uint32_t program_id;
  uint32_t namespace_id;
  jit_func_t compiled_func;
  uint8_t *bytecode;
  size_t bytecode_len;
  bool active;

  /* Statistics */
  uint64_t exec_count;
  uint64_t total_cycles;
} NvmeJitProgram;

/* JIT subsystem state */
typedef struct NvmeJitState {
  bool initialized;
  NvmeJitProgram programs[64]; /* Max 64 programs */
  bpf_map_t *shared_maps[16];  /* Shared maps across programs */
} NvmeJitState;

#endif /* QEMU_NVME_JIT_PATCH_H */

/* ========================================================================== */
/* Implementation */
/* ========================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static NvmeJitState g_jit_state = {0};

/**
 * Initialize the JIT subsystem
 * Call this from nvme_realize() in ctrl.c
 */
int nvme_jit_init(void) {
  if (g_jit_state.initialized) {
    return 0;
  }

  /* Initialize TP4091 runtime */
  if (tp4091_init() != 0) {
    fprintf(stderr, "[NVMe-JIT] Failed to initialize TP4091 runtime\n");
    return -1;
  }

  /* Initialize map subsystem */
  if (maps_init() != 0) {
    fprintf(stderr, "[NVMe-JIT] Failed to initialize maps\n");
    return -1;
  }

  memset(&g_jit_state, 0, sizeof(g_jit_state));
  g_jit_state.initialized = true;

  printf("[NVMe-JIT] Initialized with libjit.a integration\n");
  return 0;
}

/**
 * Cleanup JIT subsystem
 * Call this from nvme_exit() in ctrl.c
 */
void nvme_jit_cleanup(void) {
  if (!g_jit_state.initialized) {
    return;
  }

  tp4091_cleanup();
  maps_cleanup();
  g_jit_state.initialized = false;
}

/**
 * Load a BPF program for a namespace
 *
 * @param nsid      Namespace ID
 * @param bytecode  eBPF bytecode
 * @param len       Bytecode length in bytes
 * @return 0 on success, -1 on failure
 */
int nvme_jit_load_program(uint32_t nsid, const uint8_t *bytecode, size_t len) {
  if (!g_jit_state.initialized || !bytecode || len == 0) {
    return -1;
  }

  tp4091_load_cmd_t load_cmd = {
      .program_id = nsid, /* Use nsid as program_id for simplicity */
      .code = bytecode,
      .code_size = len};

  if (tp4091_load(&load_cmd) != 0) {
    fprintf(stderr, "[NVMe-JIT] Failed to load program for ns=%u\n", nsid);
    return -1;
  }

  printf("[NVMe-JIT] Loaded program for namespace %u (%zu bytes)\n", nsid, len);
  return 0;
}

/**
 * Activate a program on a namespace
 */
int nvme_jit_activate_program(uint32_t program_id, uint32_t nsid) {
  if (!g_jit_state.initialized) {
    return -1;
  }

  tp4091_activate_cmd_t activate_cmd = {.program_id = program_id,
                                        .namespace_id = nsid};

  return tp4091_activate(&activate_cmd);
}

/**
 * Execute a BPF program on the I/O path
 *
 * @param nsid    Namespace ID
 * @param buffer  Data buffer for the operation
 * @param result  Output: Result from BPF program
 * @return 0 on success, -1 on failure
 */
int nvme_jit_execute(uint32_t nsid, void *buffer, uint64_t *result) {
  if (!g_jit_state.initialized || !result) {
    return -1;
  }

  tp4091_execute_cmd_t exec_cmd = {.program_id = nsid, .buffer = buffer};

  return tp4091_execute(&exec_cmd, result);
}

/**
 * Unload a program
 */
int nvme_jit_unload_program(uint32_t program_id) {
  if (!g_jit_state.initialized) {
    return -1;
  }

  return tp4091_unload(program_id);
}

/**
 * Create a shared map accessible by BPF programs
 */
uint32_t nvme_jit_create_map(uint32_t key_size, uint32_t value_size,
                             uint32_t max_entries) {
  bpf_map_t *map =
      map_create(BPF_MAP_TYPE_HASH, key_size, value_size, max_entries);
  if (!map) {
    return 0;
  }
  return map->id;
}

/* ========================================================================== */
/* QEMU Integration Hooks - Add these to hw/nvme/ctrl.c */
/* ========================================================================== */

/*
 * Example: Add to nvme_realize() after namespace setup:
 *
 *   if (nvme_jit_init() != 0) {
 *       error_setg(errp, "Failed to initialize JIT subsystem");
 *       return;
 *   }
 */

/*
 * Example: Add TP4091 admin command handling in nvme_admin_cmd():
 *
 *   case NVME_ADM_CMD_TP4091_LOAD: {
 *       uint32_t nsid = le32_to_cpu(req->cmd.nsid);
 *       uint32_t size = le32_to_cpu(req->cmd.cdw10);
 *       void *buf = g_malloc(size);
 *
 *       // Copy bytecode from guest
 *       nvme_copy_from_prp(n, buf, size, req->cmd.dptr.prp1);
 *
 *       int ret = nvme_jit_load_program(nsid, buf, size);
 *       g_free(buf);
 *
 *       return ret == 0 ? NVME_SUCCESS : NVME_INVALID_FIELD;
 *   }
 *
 *   case NVME_ADM_CMD_TP4091_ACTIVATE: {
 *       uint32_t prog_id = le32_to_cpu(req->cmd.cdw10);
 *       uint32_t nsid = le32_to_cpu(req->cmd.nsid);
 *
 *       int ret = nvme_jit_activate_program(prog_id, nsid);
 *       return ret == 0 ? NVME_SUCCESS : NVME_INVALID_FIELD;
 *   }
 */

/*
 * Example: Add computational I/O path in nvme_rw():
 *
 *   NvmeNamespace *ns = nvme_ns(n, nsid);
 *   if (ns && ns->params.csi == NVME_CSI_COMPUTE) {
 *       uint64_t result;
 *       void *data = nvme_map_prps(n, &req->sg, prp1, prp2, nlb * lba_size);
 *
 *       if (nvme_jit_execute(nsid, data, &result) == 0) {
 *           req->cqe.result = cpu_to_le64(result);
 *           return NVME_SUCCESS;
 *       }
 *   }
 *   // Fall through to normal I/O
 */

/* ========================================================================== */
/* Self-test (compile with -DTEST_QEMU_PATCH) */
/* ========================================================================== */

#ifdef TEST_QEMU_PATCH
#include "ebpf.h"
#include "nvme.h"

int main(void) {
  printf("=== QEMU NVMe JIT Patch Self-Test ===\n\n");

  /* Initialize NVMe subsystem and create namespace 1 */
  nvme_init();
  /* Try to create namespace 1 - may already exist after nvme_init() */
  int ns_ret = nvme_create_namespace(1, 1024);
  if (ns_ret != 0 && nvme_get_namespace(1) == NULL) {
    fprintf(stderr, "FAIL: Namespace creation failed\n");
    return 1;
  }
  printf("✓ NVMe namespace 1 ready\n");

  /* Initialize JIT*/
  if (nvme_jit_init() != 0) {
    fprintf(stderr, "FAIL: Init failed\n");
    return 1;
  }
  printf("✓ JIT initialized\n");

  /* Create a simple program: r0 = 42; exit */
  ebpf_insn_t prog[] = {{BPF_ALU64 | BPF_MOV | BPF_K, 0, 0, 0, 42},
                        {BPF_JMP | BPF_EXIT, 0, 0, 0, 0}};

  /* Load program for namespace 1 */
  if (nvme_jit_load_program(1, (uint8_t *)prog, sizeof(prog)) != 0) {
    fprintf(stderr, "FAIL: Load failed\n");
    return 1;
  }
  printf("✓ Program loaded\n");

  /* Activate */
  if (nvme_jit_activate_program(1, 1) != 0) {
    fprintf(stderr, "FAIL: Activate failed\n");
    return 1;
  }
  printf("✓ Program activated on namespace 1\n");

  /* Execute */
  uint64_t result;
  if (nvme_jit_execute(1, NULL, &result) != 0) {
    fprintf(stderr, "FAIL: Execute failed\n");
    return 1;
  }
  printf("✓ Program executed, result = %llu\n", (unsigned long long)result);

  if (result != 42) {
    fprintf(stderr, "FAIL: Expected 42, got %llu\n",
            (unsigned long long)result);
    return 1;
  }

  /* Create shared map */
  uint32_t map_id = nvme_jit_create_map(4, 8, 64);
  if (map_id == 0) {
    fprintf(stderr, "FAIL: Map creation failed\n");
    return 1;
  }
  printf("✓ Shared map created (ID=%u)\n", map_id);

  /* Cleanup */
  nvme_jit_unload_program(1);
  nvme_jit_cleanup();
  printf("✓ Cleanup complete\n");

  printf("\n=== All tests passed! ===\n");
  return 0;
}
#endif /* TEST_QEMU_PATCH */
