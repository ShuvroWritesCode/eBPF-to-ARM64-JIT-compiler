# QEMU NVMe JIT Integration Guide

This document describes how to integrate the eBPF JIT compiler (`libjit.a`) with QEMU's NVMe emulation for computational storage simulation.

## Overview

The integration hooks into QEMU's `hw/nvme/ctrl.c` to offload BPF program execution to our JIT compiler instead of software interpretation.

```
┌────────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│  Guest OS          │    │  QEMU NVMe       │    │  libjit.a       │
│  (NVMe Driver)     │───▶│  Controller      │───▶│  (JIT Compiler) │
│                    │    │  hw/nvme/ctrl.c  │    │                 │
└────────────────────┘    └──────────────────┘    └─────────────────┘
                                   │
                                   ▼
                          ┌──────────────────┐
                          │  TP4091 Runtime  │
                          │  + eBPF Maps     │
                          └──────────────────┘
```

## Prerequisites

1. **QEMU Source** (7.0+)
2. **ARM64 build toolchain**
3. **Compiled libjit.a** from this repository

## Build Integration

### Step 1: Copy Library and Headers

```bash
# From project root
cp libjit.a /path/to/qemu/hw/nvme/
cp include/*.h /path/to/qemu/hw/nvme/
```

### Step 2: Modify QEMU Build

Edit `hw/nvme/meson.build`:
```meson
nvme_ss = ss.source_set()
nvme_ss.add(files('ctrl.c', 'dif.c', 'ns.c', 'subsys.c'))

# Add JIT library
nvme_ss.add(files('jit_bridge.c'))
nvme_ss.add_link_args(['-L' + meson.current_source_dir(), '-ljit'])

softmmu_ss.add_all(nvme_ss)
```

### Step 3: Create JIT Bridge

Create `hw/nvme/jit_bridge.c` to interface QEMU with the JIT:

```c
#include "jit.h"
#include "tp4091.h"
#include "maps.h"

// Initialize JIT subsystem
void nvme_jit_init(void) {
    tp4091_init();
    maps_init();
}

// Load BPF program for namespace
int nvme_jit_load_program(uint32_t nsid, const uint8_t *bytecode, size_t len) {
    tp4091_load_cmd_t cmd = {
        .program_id = nsid,
        .code = bytecode,
        .code_size = len
    };
    return tp4091_load(&cmd);
}

// Execute BPF program on I/O path
int nvme_jit_execute(uint32_t nsid, void *buffer, uint64_t *result) {
    tp4091_execute_cmd_t cmd = {
        .program_id = nsid,
        .buffer = (uint64_t)buffer
    };
    return tp4091_execute(&cmd, result);
}
```

## Integration Points in ctrl.c

### 1. Initialization

In `nvme_realize()`:
```c
#include "jit_bridge.h"

static void nvme_realize(PCIDevice *pci_dev, Error **errp) {
    // ... existing code ...
    
    // Initialize JIT subsystem
    nvme_jit_init();
    
    // ... rest of initialization ...
}
```

### 2. Command Processing

In `nvme_io_cmd()` or custom vendor command handler:
```c
static uint16_t nvme_io_cmd(NvmeCtrl *n, NvmeRequest *req) {
    NvmeNamespace *ns = req->ns;
    
    // Check if computational storage is enabled
    if (ns->params.csi == NVME_CSI_COMPUTE) {
        uint64_t result;
        void *data_buffer = /* map PRPs/SGLs */;
        
        if (nvme_jit_execute(ns->id, data_buffer, &result) == 0) {
            req->cqe.result = cpu_to_le64(result);
            return NVME_SUCCESS;
        }
        return NVME_INTERNAL_DEV_ERROR;
    }
    
    // Normal I/O path
    return nvme_do_io(n, req);
}
```

### 3. Program Loading via Admin Command

Handle TP4091-style Load/Activate:
```c
static uint16_t nvme_admin_cmd(NvmeCtrl *n, NvmeRequest *req) {
    switch (req->cmd.opcode) {
    case NVME_ADM_CMD_VENDOR_LOAD_PROGRAM:
        return nvme_load_program(n, req);
    // ...
    }
}

static uint16_t nvme_load_program(NvmeCtrl *n, NvmeRequest *req) {
    uint32_t nsid = le32_to_cpu(req->cmd.nsid);
    uint32_t prog_size = le32_to_cpu(req->cmd.cdw10);
    
    void *prog_buf = g_malloc(prog_size);
    // Copy from guest memory via PRP
    nvme_copy_from_prp(n, prog_buf, prog_size, req->cmd.dptr.prp1);
    
    int ret = nvme_jit_load_program(nsid, prog_buf, prog_size);
    g_free(prog_buf);
    
    return ret == 0 ? NVME_SUCCESS : NVME_INVALID_FIELD;
}
```

## Testing

### 1. Build QEMU with JIT
```bash
cd /path/to/qemu
mkdir build && cd build
../configure --target-list=aarch64-softmmu
make -j$(nproc)
```

### 2. Run with NVMe Device
```bash
./qemu-system-aarch64 \
    -M virt -cpu cortex-a72 -m 4G \
    -drive file=test.qcow2,if=none,id=boot \
    -device nvme,serial=deadbeef,drive=boot
```

### 3. Verify from Guest
```bash
# Load BPF program
nvme admin-passthru /dev/nvme0n1 \
    --opcode=0xC0 --namespace-id=1 \
    --input-file=filter.bpf --data-len=256
    
# Execute
nvme read /dev/nvme0n1 --start-block=0 --block-count=8
```

## Standalone Patch Reference

See `tools/qemu_nvme_jit_patch.c` for a complete standalone reference implementation that demonstrates the integration pattern without modifying QEMU source directly.

## Performance Considerations

1. **JIT Caching**: Programs are compiled once and cached
2. **Zero-Copy**: Data buffers passed directly to JIT code
3. **Helper Overhead**: Map operations add ~10ns per call

## Security Notes

- Programs are validated before JIT compilation
- Namespace isolation is enforced via TP4091 runtime
- Memory bounds are checked by helpers
