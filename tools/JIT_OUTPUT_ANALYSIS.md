# JIT Compiler Output Analysis

## Your eBPF Program

```c
// r0 += r1
// r0 += 42  
// exit
```

**eBPF Bytecode:**
```
0f 01 00 00 00 00 00 00    // r0 += r1 (BPF_ALU64 | BPF_ADD | BPF_X)
07 00 00 00 2a 00 00 00    // r0 += 42 (BPF_ALU64 | BPF_ADD | BPF_K)
95 00 00 00 00 00 00 00    // exit
```

## Generated ARM64 Code

### Prologue (Register Save)
```assembly
0000: stp x29, x30, [sp, #-16]!   // Save FP, LR
0004: stp x27, x28, [sp, #-16]!   // Save x27-x28
0008: stp x25, x26, [sp, #-16]!   // Save x25-x26
000c: stp x23, x24, [sp, #-16]!   // Save x23-x24
0010: stp x21, x22, [sp, #-16]!   // Save x21-x22
0014: stp x19, x20, [sp, #-16]!   // Save x19-x20 (BPF R0-R1)
0018: mov x20, x0                  // BPF R1 = context (input arg)
001c: sub sp, sp, #512             // Allocate stack
```

### Program Body
```assembly
0020: mov x29, sp                  // BPF R10 (FP) = stack pointer
0024: add x20, x20, x19            // r1 += r0 ❌ WRONG!
0028: add x19, x19, #42            // r0 += 42 ✓
002c: b #0x4                       // Jump to epilogue
```

### Epilogue (Register Restore & Return)
```assembly
0030: mov x0, x19                  // Return value = BPF R0
0034: add sp, sp, #512             // Deallocate stack
0038: ldp x19, x20, [sp], #16      // Restore x19-x20
003c: ldp x21, x22, [sp], #16      // Restore x21-x22
0040: ldp x23, x24, [sp], #16      // Restore x23-x24
0044: ldp x25, x26, [sp], #16      // Restore x25-x26
0048: ldp x27, x28, [sp], #16      // Restore x27-x28
004c: ldp x29, x30, [sp], #16      // Restore FP, LR
0050: ret                          // Return
```

## Register Mapping

| BPF Register | ARM64 Register |
|--------------|----------------|
| R0 | x19 |
| R1 | x20 |
| R2 | x21 |
| R3 | x22 |
| R4 | x23 |
| R5 | x24 |
| R10 (FP) | x29 |

## Problem Identified

**Line 0024:** `add x20, x20, x19` translates to **r1 += r0** instead of **r0 += r1**

The instruction encoding is backwards! The eBPF instruction `0x0f` (BPF_ALU64 | BPF_ADD | BPF_X) with:
- `dst_reg = 0` (r0)
- `src_reg = 1` (r1)

Should generate: `add x19, x19, x20` (r0 += r1)  
But generated: `add x20, x20, x19` (r1 += r0)

## Execution Result

**Input:** r1 = 10  
**Expected:** r0 = 10 + 42 = 52  
**Actual:** r0 = 8591966330 (garbage)

The result is wrong because:
1. r0 starts uninitialized (garbage value)
2. r1 += r0 adds garbage to 10
3. r0 += 42 adds 42 to the garbage

## How to Use This Tool

To inspect any eBPF program:

```bash
cd implementation
./build/tools/inspect_jit
```

The tool will:
1. Show the eBPF bytecode
2. Disassemble the ARM64 code
3. Execute and verify the result
4. Highlight any issues

## Fix Needed

The JIT compiler needs to swap dst and src registers in the ARM64 code generation for ALU operations.
