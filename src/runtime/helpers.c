#include "helpers.h"
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

// Storage helper implementations (stubs for now)
uint64_t helper_storage_read(uint64_t ns_id, uint64_t lba, uint64_t count,
                             uint64_t buffer, uint64_t unused, void *ctx) {
  (void)unused;
  (void)ctx;

  // TODO: Implement actual storage read
  printf("[HELPER] storage_read(ns=%lu, lba=%lu, count=%lu, buf=%p)\n", ns_id,
         lba, count, (void *)buffer);

  // Return success (0) for now
  return 0;
}

uint64_t helper_storage_write(uint64_t ns_id, uint64_t lba, uint64_t count,
                              uint64_t buffer, uint64_t unused, void *ctx) {
  (void)unused;
  (void)ctx;

  printf("[HELPER] storage_write(ns=%lu, lba=%lu, count=%lu, buf=%p)\n", ns_id,
         lba, count, (void *)buffer);

  return 0;
}

uint64_t helper_storage_flush(uint64_t ns_id, uint64_t unused1,
                              uint64_t unused2, uint64_t unused3,
                              uint64_t unused4, void *ctx) {
  (void)unused1;
  (void)unused2;
  (void)unused3;
  (void)unused4;
  (void)ctx;

  printf("[HELPER] storage_flush(ns=%lu)\n", ns_id);

  return 0;
}

uint64_t helper_storage_get_ns(uint64_t unused1, uint64_t unused2,
                               uint64_t unused3, uint64_t unused4,
                               uint64_t unused5, void *ctx) {
  (void)unused1;
  (void)unused2;
  (void)unused3;
  (void)unused4;
  (void)unused5;
  (void)ctx;

  printf("[HELPER] storage_get_ns()\n");

  // Return namespace ID 1 for now
  return 1;
}

uint64_t helper_storage_log(uint64_t level, uint64_t message, uint64_t unused1,
                            uint64_t unused2, uint64_t unused3, void *ctx) {
  (void)unused1;
  (void)unused2;
  (void)unused3;
  (void)ctx;

  printf("[HELPER] storage_log(level=%lu, msg=%p)\n", level, (void *)message);

  return 0;
}
