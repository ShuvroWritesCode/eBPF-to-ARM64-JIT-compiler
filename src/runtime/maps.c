#include "maps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Simple hash map entry
typedef struct map_entry {
  uint8_t key[MAX_KEY_SIZE];
  uint8_t value[MAX_VALUE_SIZE];
  int occupied;
  struct map_entry *next; // Simple chaining for collisions
} map_entry_t;

// Hash map data structure
typedef struct {
  map_entry_t *buckets;
  uint32_t bucket_count;
} hash_map_data_t;

static bpf_map_t maps[MAX_MAPS];
static int maps_initialized = 0;

// DJB2 Hash function
static uint32_t hash(const void *key, uint32_t key_size) {
  uint32_t hash = 5381;
  const uint8_t *k = (const uint8_t *)key;
  for (uint32_t i = 0; i < key_size; i++) {
    hash = ((hash << 5) + hash) + k[i];
  }
  return hash;
}

int maps_init(void) {
  memset(maps, 0, sizeof(maps));
  maps_initialized = 1;
  return 0;
}

void maps_cleanup(void) {
  for (int i = 0; i < MAX_MAPS; i++) {
    if (maps[i].data) {
      hash_map_data_t *hm = (hash_map_data_t *)maps[i].data;
      if (hm->buckets) {
        // Free chains
        for (uint32_t b = 0; b < hm->bucket_count; b++) {
          map_entry_t *curr = hm->buckets[b].next;
          while (curr) {
            map_entry_t *next = curr->next;
            free(curr);
            curr = next;
          }
        }
        free(hm->buckets);
      }
      free(hm);
      maps[i].data = NULL;
    }
  }
  maps_initialized = 0;
}

bpf_map_t *map_create(uint32_t type, uint32_t key_size, uint32_t value_size,
                      uint32_t max_entries) {
  if (!maps_initialized)
    maps_init();

  if (type != BPF_MAP_TYPE_HASH)
    return NULL; // Only hash supported for now
  if (key_size > MAX_KEY_SIZE || value_size > MAX_VALUE_SIZE)
    return NULL;

  for (int i = 0; i < MAX_MAPS; i++) {
    if (maps[i].data == NULL) {
      hash_map_data_t *hm = calloc(1, sizeof(hash_map_data_t));
      // Simple bucket size = max_entries
      hm->bucket_count = max_entries;
      hm->buckets = calloc(max_entries, sizeof(map_entry_t));

      maps[i].type = type;
      maps[i].key_size = key_size;
      maps[i].value_size = value_size;
      maps[i].max_entries = max_entries;
      maps[i].id = i + 1; // 1-based ID
      maps[i].data = hm;

      return &maps[i];
    }
  }
  return NULL;
}

bpf_map_t *map_get(uint32_t id) {
  if (id == 0 || id > MAX_MAPS)
    return NULL;
  bpf_map_t *map = &maps[id - 1];
  if (map->data == NULL)
    return NULL;
  return map;
}

// --- Helpers ---

uint64_t helper_map_lookup_elem(uint64_t map_ptr, uint64_t key_ptr, uint64_t r3,
                                uint64_t r4, uint64_t r5) {
  (void)r3;
  (void)r4;
  (void)r5;
  bpf_map_t *map = (bpf_map_t *)map_ptr;
  void *key = (void *)key_ptr;

  if (!map || !key || !map->data)
    return 0;

  hash_map_data_t *hm = (hash_map_data_t *)map->data;
  uint32_t h = hash(key, map->key_size) % hm->bucket_count;

  map_entry_t *entry = &hm->buckets[h];

  // Check bucket head
  if (entry->occupied && memcmp(entry->key, key, map->key_size) == 0) {
    return (uint64_t)entry->value;
  }

  // Check chain
  map_entry_t *curr = entry->next;
  while (curr) {
    if (memcmp(curr->key, key, map->key_size) == 0) {
      return (uint64_t)curr->value;
    }
    curr = curr->next;
  }

  return 0; // Not found
}

uint64_t helper_map_update_elem(uint64_t map_ptr, uint64_t key_ptr,
                                uint64_t value_ptr, uint64_t flags,
                                uint64_t r5) {
  (void)r5;
  bpf_map_t *map = (bpf_map_t *)map_ptr;
  void *key = (void *)key_ptr;
  void *value = (void *)value_ptr;

  if (!map || !key || !value || !map->data)
    return -1;

  hash_map_data_t *hm = (hash_map_data_t *)map->data;
  uint32_t h = hash(key, map->key_size) % hm->bucket_count;

  map_entry_t *entry = &hm->buckets[h];

  // Update if exists
  if (entry->occupied && memcmp(entry->key, key, map->key_size) == 0) {
    memcpy(entry->value, value, map->value_size);
    return 0;
  }

  map_entry_t *curr = entry;
  while (curr->next) {
    if (memcmp(curr->next->key, key, map->key_size) == 0) {
      memcpy(curr->next->value, value, map->value_size);
      return 0;
    }
    curr = curr->next;
  }

  // Insert new
  if (!entry->occupied) {
    entry->occupied = 1;
    memcpy(entry->key, key, map->key_size);
    memcpy(entry->value, value, map->value_size);
    return 0;
  } else {
    // Add to chain
    map_entry_t *new_node = calloc(1, sizeof(map_entry_t));
    new_node->occupied = 1;
    memcpy(new_node->key, key, map->key_size);
    memcpy(new_node->value, value, map->value_size);
    curr->next = new_node; // Append to end of chain found above
    return 0;
  }
}

uint64_t helper_map_delete_elem(uint64_t map_ptr, uint64_t key_ptr, uint64_t r3,
                                uint64_t r4, uint64_t r5) {
  (void)r3;
  (void)r4;
  (void)r5;
  bpf_map_t *map = (bpf_map_t *)map_ptr;
  void *key = (void *)key_ptr;

  if (!map || !key || !map->data)
    return -1;

  hash_map_data_t *hm = (hash_map_data_t *)map->data;
  uint32_t h = hash(key, map->key_size) % hm->bucket_count;

  map_entry_t *entry = &hm->buckets[h];

  if (entry->occupied && memcmp(entry->key, key, map->key_size) == 0) {
    // Head delete - complicated without double pointer or flag
    // Just mark unoccupied if no next, else copy next to head
    if (entry->next) {
      map_entry_t *next = entry->next;
      *entry = *next; // Copy content
      free(next);     // Free old next container
    } else {
      entry->occupied = 0;
    }
    return 0;
  }

  map_entry_t *prev = entry;
  map_entry_t *curr = entry->next;
  while (curr) {
    if (memcmp(curr->key, key, map->key_size) == 0) {
      prev->next = curr->next;
      free(curr);
      return 0;
    }
    prev = curr;
    curr = curr->next;
  }

  return -1; // Not found
}
