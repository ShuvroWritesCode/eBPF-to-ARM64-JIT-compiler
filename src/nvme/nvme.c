#include "nvme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global namespace table
static nvme_namespace_t namespaces[MAX_NAMESPACES];

// Initialize NVMe simulator
int nvme_init(void) {
  memset(namespaces, 0, sizeof(namespaces));

  // Create default namespace (nsid=1)
  return nvme_create_namespace(1, DEFAULT_NS_SIZE_BLOCKS);
}

// Cleanup NVMe simulator
void nvme_cleanup(void) {
  for (int i = 0; i < MAX_NAMESPACES; i++) {
    if (namespaces[i].state == NS_STATE_ACTIVE && namespaces[i].data) {
      free(namespaces[i].data);
      namespaces[i].data = NULL;
    }
  }
}

// Create a namespace
int nvme_create_namespace(uint32_t nsid, uint64_t size_blocks) {
  if (nsid == 0 || nsid > MAX_NAMESPACES) {
    return -1;
  }

  nvme_namespace_t *ns = &namespaces[nsid - 1];

  if (ns->state == NS_STATE_ACTIVE) {
    fprintf(stderr, "Namespace %u already exists\n", nsid);
    return -1;
  }

  // Allocate storage
  size_t total_size = size_blocks * DEFAULT_BLOCK_SIZE;
  ns->data = (uint8_t *)calloc(1, total_size);
  if (!ns->data) {
    fprintf(stderr, "Failed to allocate %zu bytes for namespace %u\n",
            total_size, nsid);
    return -1;
  }

  ns->nsid = nsid;
  ns->state = NS_STATE_ACTIVE;
  ns->size_blocks = size_blocks;
  ns->block_size = DEFAULT_BLOCK_SIZE;
  ns->active_program_id = 0;

  printf("[NVMe] Created namespace %u (%lu blocks, %zu bytes)\n", nsid,
         size_blocks, total_size);

  return 0;
}

// Delete a namespace
int nvme_delete_namespace(uint32_t nsid) {
  if (nsid == 0 || nsid > MAX_NAMESPACES) {
    return -1;
  }

  nvme_namespace_t *ns = &namespaces[nsid - 1];

  if (ns->state != NS_STATE_ACTIVE) {
    return -1;
  }

  if (ns->data) {
    free(ns->data);
  }

  memset(ns, 0, sizeof(nvme_namespace_t));
  ns->state = NS_STATE_EMPTY;

  printf("[NVMe] Deleted namespace %u\n", nsid);

  return 0;
}

// Get namespace
nvme_namespace_t *nvme_get_namespace(uint32_t nsid) {
  if (nsid == 0 || nsid > MAX_NAMESPACES) {
    return NULL;
  }

  nvme_namespace_t *ns = &namespaces[nsid - 1];
  return (ns->state == NS_STATE_ACTIVE) ? ns : NULL;
}

// Verify namespace
int nvme_verify_namespace(uint32_t nsid) {
  if (nsid == 0 || nsid > MAX_NAMESPACES) {
    return -1;
  }

  nvme_namespace_t *ns = nvme_get_namespace(nsid);
  if (!ns || ns->state != NS_STATE_ACTIVE) {
    return -1;
  }

  return 0;
}

// Read blocks
int nvme_read_blocks(uint32_t nsid, uint64_t lba, uint32_t count,
                     void *buffer) {
  nvme_namespace_t *ns = nvme_get_namespace(nsid);
  if (!ns || !buffer) {
    return -1;
  }

  // Bounds check
  if (lba + count > ns->size_blocks) {
    fprintf(stderr, "Read out of bounds: LBA %lu + %u > %lu\n", lba, count,
            ns->size_blocks);
    return -1;
  }

  size_t offset = lba * ns->block_size;
  size_t size = count * ns->block_size;

  memcpy(buffer, ns->data + offset, size);

  return 0;
}

// Write blocks
int nvme_write_blocks(uint32_t nsid, uint64_t lba, uint32_t count,
                      const void *buffer) {
  nvme_namespace_t *ns = nvme_get_namespace(nsid);
  if (!ns || !buffer) {
    return -1;
  }

  // Bounds check
  if (lba + count > ns->size_blocks) {
    fprintf(stderr, "Write out of bounds: LBA %lu + %u > %lu\n", lba, count,
            ns->size_blocks);
    return -1;
  }

  size_t offset = lba * ns->block_size;
  size_t size = count * ns->block_size;

  memcpy(ns->data + offset, buffer, size);

  return 0;
}

// Flush namespace
int nvme_flush(uint32_t nsid) {
  nvme_namespace_t *ns = nvme_get_namespace(nsid);
  if (!ns) {
    return -1;
  }

  // In a real implementation, this would sync to disk
  // For simulator, it's a no-op

  return 0;
}

// Set active program
int nvme_set_active_program(uint32_t nsid, uint32_t program_id) {
  nvme_namespace_t *ns = nvme_get_namespace(nsid);
  if (!ns) {
    return -1;
  }

  ns->active_program_id = program_id;
  return 0;
}

// Get active program
uint32_t nvme_get_active_program(uint32_t nsid) {
  nvme_namespace_t *ns = nvme_get_namespace(nsid);
  return ns ? ns->active_program_id : 0;
}
