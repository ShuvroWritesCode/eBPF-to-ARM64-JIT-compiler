#ifndef HELPERS_H
#define HELPERS_H

#include <stdint.h>

// Helper function signature (matches eBPF calling convention)
typedef uint64_t (*helper_func_t)(uint64_t r1, uint64_t r2, uint64_t r3,
                                  uint64_t r4, uint64_t r5, void *ctx);

// TP4091 Storage Helper Function IDs
#define BPF_FUNC_storage_read 1
#define BPF_FUNC_storage_write 2
#define BPF_FUNC_storage_flush 3
#define BPF_FUNC_storage_get_ns 4
#define BPF_FUNC_storage_log 5

// Helper function registry
helper_func_t get_helper_func(uint32_t func_id);

// Storage helper implementations
uint64_t helper_storage_read(uint64_t ns_id, uint64_t lba, uint64_t count,
                             uint64_t buffer, uint64_t unused, void *ctx);
uint64_t helper_storage_write(uint64_t ns_id, uint64_t lba, uint64_t count,
                              uint64_t buffer, uint64_t unused, void *ctx);
uint64_t helper_storage_flush(uint64_t ns_id, uint64_t unused1,
                              uint64_t unused2, uint64_t unused3,
                              uint64_t unused4, void *ctx);
uint64_t helper_storage_get_ns(uint64_t unused1, uint64_t unused2,
                               uint64_t unused3, uint64_t unused4,
                               uint64_t unused5, void *ctx);
uint64_t helper_storage_log(uint64_t level, uint64_t message, uint64_t unused1,
                            uint64_t unused2, uint64_t unused3, void *ctx);

#endif // HELPERS_H
