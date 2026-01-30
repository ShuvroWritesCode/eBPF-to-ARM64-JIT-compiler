#include "helpers.h"
#include "nvme.h"
#include "tp4091.h"
#include <stdio.h>
#include <string.h>

// Helper function registry
helper_func_t get_helper_func(uint32_t func_id) {
  switch (func_id) {
  case BPF_FUNC_storage_read:
    return helper_storage_read;
  case BPF_FUNC_storage_write:
    return helper_storage_write;
  case BPF_FUNC_storage_flush:
    return helper_storage_flush;
  case BPF_FUNC_storage_get_ns:
    return helper_storage_get_ns;
  case BPF_FUNC_storage_log:
    return helper_storage_log;
  default:
    return NULL;
  }
}

// Storage helper implementations
uint64_t helper_storage_read(uint64_t ns_id, uint64_t lba, uint64_t count,
                             uint64_t buffer, uint64_t unused) {
  (void)unused;

  // 1. Verify Namespace
  if (nvme_verify_namespace((uint32_t)ns_id) != 0) {
    printf("[HELPER] Error: Invalid namespace %lu\n", ns_id);
    return -1;
  }

  // 2. Verify Access Control
  tp4091_program_t *prog = tp4091_get_current_program();
  if (prog && prog->namespace_id != ns_id) {
    printf("[HELPER] Error: Access denied. Program %u cannot access Namespace "
           "%lu\n",
           prog->program_id, ns_id);
    return -1;
  }

  // 3. Perform Read (nvme_read_blocks performs internal bounds checks)
  if (nvme_read_blocks((uint32_t)ns_id, lba, (uint32_t)count, (void *)buffer) !=
      0) {
    printf("[HELPER] Error: Read failed (LBA %lu, Count %lu)\n", lba, count);
    return -1;
  }

  printf("[HELPER] storage_read(ns=%lu, lba=%lu, count=%lu) -> Success\n",
         ns_id, lba, count);
  return 0;
}

uint64_t helper_storage_write(uint64_t ns_id, uint64_t lba, uint64_t count,
                              uint64_t buffer, uint64_t unused) {
  (void)unused;

  // 1. Verify Namespace
  if (nvme_verify_namespace((uint32_t)ns_id) != 0) {
    printf("[HELPER] Error: Invalid namespace %lu\n", ns_id);
    return -1;
  }

  // 2. Verify Access Control
  tp4091_program_t *prog = tp4091_get_current_program();
  if (prog && prog->namespace_id != ns_id) {
    printf("[HELPER] Error: Access denied. Program %u cannot access Namespace "
           "%lu\n",
           prog->program_id, ns_id);
    return -1;
  }

  // 3. Perform Write
  if (nvme_write_blocks((uint32_t)ns_id, lba, (uint32_t)count,
                        (void *)buffer) != 0) {
    printf("[HELPER] Error: Write failed (LBA %lu, Count %lu)\n", lba, count);
    return -1;
  }

  printf("[HELPER] storage_write(ns=%lu, lba=%lu, count=%lu) -> Success\n",
         ns_id, lba, count);
  return 0;
}

uint64_t helper_storage_flush(uint64_t ns_id, uint64_t unused1,
                              uint64_t unused2, uint64_t unused3,
                              uint64_t unused4) {
  (void)unused1;
  (void)unused2;
  (void)unused3;
  (void)unused4;

  printf("[HELPER] storage_flush(ns=%lu)\n", ns_id);

  return 0;
}

uint64_t helper_storage_get_ns(uint64_t unused1, uint64_t unused2,
                               uint64_t unused3, uint64_t unused4,
                               uint64_t unused5) {
  (void)unused1;
  (void)unused2;
  (void)unused3;
  (void)unused4;
  (void)unused5;

  tp4091_program_t *prog = tp4091_get_current_program();
  if (prog) {
    return prog->namespace_id;
  }
  return 0;
}

uint64_t helper_storage_log(uint64_t level, uint64_t message, uint64_t unused1,
                            uint64_t unused2, uint64_t unused3) {
  (void)unused1;
  (void)unused2;
  (void)unused3;

  printf("[HELPER] Log(level=%lu): %lu\n", level, message);
  return 0;
}
