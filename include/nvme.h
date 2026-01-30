#ifndef NVME_H
#define NVME_H

#include <stdint.h>

// NVMe namespace configuration
#define MAX_NAMESPACES 8
#define DEFAULT_BLOCK_SIZE 4096
#define DEFAULT_NS_SIZE_BLOCKS 1024 // 4MB namespace

// Namespace states
#define NS_STATE_EMPTY 0
#define NS_STATE_ACTIVE 1

// Namespace structure
typedef struct {
  uint32_t nsid;
  uint32_t state;
  uint64_t size_blocks;
  uint32_t block_size;
  uint8_t *data;
  uint32_t active_program_id;
} nvme_namespace_t;

// NVMe simulator functions
int nvme_init(void);
void nvme_cleanup(void);

// Namespace management
int nvme_create_namespace(uint32_t nsid, uint64_t size_blocks);
int nvme_delete_namespace(uint32_t nsid);
nvme_namespace_t *nvme_get_namespace(uint32_t nsid);

// Block I/O operations
int nvme_read_blocks(uint32_t nsid, uint64_t lba, uint32_t count, void *buffer);
int nvme_write_blocks(uint32_t nsid, uint64_t lba, uint32_t count,
                      const void *buffer);
int nvme_flush(uint32_t nsid);

// Program association
int nvme_set_active_program(uint32_t nsid, uint32_t program_id);
uint32_t nvme_get_active_program(uint32_t nsid);

#endif // NVME_H
