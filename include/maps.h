#ifndef MAPS_H
#define MAPS_H

#include <stddef.h>
#include <stdint.h>

// Map Types
#define BPF_MAP_TYPE_HASH 1
#define BPF_MAP_TYPE_ARRAY 2

// Max limits for simplicity
#define MAX_MAPS 64
#define MAX_KEY_SIZE 128
#define MAX_VALUE_SIZE 128
#define MAX_ENTRIES 1024

// Map Definitions
typedef struct {
  uint32_t type;
  uint32_t key_size;
  uint32_t value_size;
  uint32_t max_entries;
  uint32_t id;
  void *data; // Implementation specific data
} bpf_map_t;

// Public API
int maps_init(void);
void maps_cleanup(void);
bpf_map_t *map_create(uint32_t type, uint32_t key_size, uint32_t value_size,
                      uint32_t max_entries);
bpf_map_t *map_get(uint32_t id);
int map_delete(uint32_t id);

// Helper Implementations (to be registered in JIT)
// void *map_lookup_elem(struct bpf_map *map, const void *key)
uint64_t helper_map_lookup_elem(uint64_t map_ptr, uint64_t key_ptr, uint64_t r3,
                                uint64_t r4, uint64_t r5);

// int map_update_elem(struct bpf_map *map, const void *key, const void *value,
// u64 flags)
uint64_t helper_map_update_elem(uint64_t map_ptr, uint64_t key_ptr,
                                uint64_t value_ptr, uint64_t flags,
                                uint64_t r5);

// int map_delete_elem(struct bpf_map *map, const void *key)
uint64_t helper_map_delete_elem(uint64_t map_ptr, uint64_t key_ptr, uint64_t r3,
                                uint64_t r4, uint64_t r5);

#endif // MAPS_H
