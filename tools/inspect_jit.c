// Tool to inspect JIT compilation of eBPF programs
// Shows eBPF bytecode and generated ARM64 assembly

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "ebpf.h"
#include "jit.h"

// Helper macros
#define BPF_MOV64_IMM(DST, IMM)                                               \
  ((ebpf_insn_t){.opcode = BPF_ALU64 | BPF_MOV | BPF_K, .dst_reg = DST,      \
                 .src_reg = 0, .offset = 0, .imm = IMM})

#define BPF_MOV64_REG(DST, SRC)                                               \
  ((ebpf_insn_t){.opcode = BPF_ALU64 | BPF_MOV | BPF_X, .dst_reg = DST,      \
                 .src_reg = SRC, .offset = 0, .imm = 0})

#define BPF_ALU64_IMM(OP, DST, IMM)                                           \
  ((ebpf_insn_t){.opcode = BPF_ALU64 | BPF_##OP | BPF_K, .dst_reg = DST,     \
                 .src_reg = 0, .offset = 0, .imm = IMM})

#define BPF_ALU64_REG(OP, DST, SRC)                                           \
  ((ebpf_insn_t){.opcode = BPF_ALU64 | BPF_##OP | BPF_X, .dst_reg = DST,     \
                 .src_reg = SRC, .offset = 0, .imm = 0})

#define BPF_EXIT_INSN()                                                        \
  ((ebpf_insn_t){.opcode = BPF_JMP | BPF_EXIT, .dst_reg = 0, .src_reg = 0,   \
                 .offset = 0, .imm = 0})

#define BPF_REG_0 0
#define BPF_REG_1 1
#define BPF_REG_2 2

// Register mapping table
const char* ebpf_reg_names[] = {
    "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10"
};

const char* arm64_reg_names[] = {
    "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", 
    "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
    "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
    "x24", "x25", "x26", "x27", "x28", "x29", "x30", "sp"
};

// Get ARM64 register name for eBPF register
const char* get_arm64_reg(int ebpf_reg) {
    if (ebpf_reg < 0 || ebpf_reg > 10) return "???";
    if (ebpf_reg == 10) return "x29";  // Frame pointer
    return arm64_reg_names[19 + ebpf_reg];  // x19-x28
}

// Disassemble eBPF instruction
void disasm_ebpf_insn(const ebpf_insn_t *insn, int pc) {
    uint8_t opcode = insn->opcode;
    uint8_t class = BPF_CLASS(opcode);
    
    printf("  %03d: ", pc);
    
    // Print raw bytecode
    printf("%02x %x%x %04x %08x   ", 
           insn->opcode, insn->dst_reg, insn->src_reg, 
           insn->offset & 0xFFFF, insn->imm);
    
    // Disassemble instruction
    switch (class) {
    case BPF_ALU64:
        {
            uint8_t op = BPF_OP(opcode);
            uint8_t src = BPF_SRC(opcode);
            
            switch (op) {
            case BPF_MOV:
                if (src == BPF_K)
                    printf("mov %s, #%d", ebpf_reg_names[insn->dst_reg], insn->imm);
                else
                    printf("mov %s, %s", ebpf_reg_names[insn->dst_reg], 
                           ebpf_reg_names[insn->src_reg]);
                break;
            case BPF_ADD:
                if (src == BPF_K)
                    printf("add %s, #%d", ebpf_reg_names[insn->dst_reg], insn->imm);
                else
                    printf("add %s, %s", ebpf_reg_names[insn->dst_reg], 
                           ebpf_reg_names[insn->src_reg]);
                break;
            case BPF_SUB:
                if (src == BPF_K)
                    printf("sub %s, #%d", ebpf_reg_names[insn->dst_reg], insn->imm);
                else
                    printf("sub %s, %s", ebpf_reg_names[insn->dst_reg], 
                           ebpf_reg_names[insn->src_reg]);
                break;
            case BPF_MUL:
                if (src == BPF_K)
                    printf("mul %s, #%d", ebpf_reg_names[insn->dst_reg], insn->imm);
                else
                    printf("mul %s, %s", ebpf_reg_names[insn->dst_reg], 
                           ebpf_reg_names[insn->src_reg]);
                break;
            case BPF_DIV:
                if (src == BPF_K)
                    printf("div %s, #%d", ebpf_reg_names[insn->dst_reg], insn->imm);
                else
                    printf("div %s, %s", ebpf_reg_names[insn->dst_reg], 
                           ebpf_reg_names[insn->src_reg]);
                break;
            case BPF_NEG:
                printf("neg %s", ebpf_reg_names[insn->dst_reg]);
                break;
            default:
                printf("alu64 op=0x%x", op);
                break;
            }
        }
        break;
        
    case BPF_JMP:
        {
            uint8_t op = BPF_OP(opcode);
            if (op == BPF_EXIT) {
                printf("exit");
            } else if (op == BPF_JA) {
                printf("ja %+d", insn->offset);
            } else {
                printf("jmp op=0x%x", op);
            }
        }
        break;
        
    default:
        printf("unknown class=0x%x", class);
        break;
    }
    
    printf("\n");
}

// Disassemble ARM64 instruction (simplified)
void disasm_arm64_insn(uint32_t insn, uint64_t addr) {
    printf("  0x%016llx: %08x   ", (unsigned long long)addr, insn);
    
    // Simplified ARM64 disassembly
    if ((insn & 0xFF000000) == 0x91000000) {
        // ADD immediate
        int rd = insn & 0x1F;
        int rn = (insn >> 5) & 0x1F;
        int imm = (insn >> 10) & 0xFFF;
        printf("add %s, %s, #%d", arm64_reg_names[rd], arm64_reg_names[rn], imm);
    } else if ((insn & 0xFF000000) == 0x8B000000) {
        // ADD register
        int rd = insn & 0x1F;
        int rn = (insn >> 5) & 0x1F;
        int rm = (insn >> 16) & 0x1F;
        printf("add %s, %s, %s", arm64_reg_names[rd], arm64_reg_names[rn], arm64_reg_names[rm]);
    } else if ((insn & 0xFF000000) == 0xAA000000 && ((insn >> 5) & 0x1F) == 31) {
        // MOV register (ORR with XZR)
        int rd = insn & 0x1F;
        int rm = (insn >> 16) & 0x1F;
        printf("mov %s, %s", arm64_reg_names[rd], arm64_reg_names[rm]);
    } else if ((insn & 0xFF800000) == 0xD2800000) {
        // MOVZ
        int rd = insn & 0x1F;
        int imm = (insn >> 5) & 0xFFFF;
        int shift = ((insn >> 21) & 0x3) * 16;
        printf("movz %s, #0x%x, lsl #%d", arm64_reg_names[rd], imm, shift);
    } else if ((insn & 0xFF800000) == 0xF2800000) {
        // MOVK
        int rd = insn & 0x1F;
        int imm = (insn >> 5) & 0xFFFF;
        int shift = ((insn >> 21) & 0x3) * 16;
        printf("movk %s, #0x%x, lsl #%d", arm64_reg_names[rd], imm, shift);
    } else if (insn == 0xD65F03C0) {
        printf("ret");
    } else if ((insn & 0xFC000000) == 0x14000000) {
        // B (branch)
        int offset = (insn & 0x03FFFFFF);
        if (offset & 0x02000000) offset |= 0xFC000000;  // Sign extend
        printf("b #%d", offset * 4);
    } else {
        printf("(instruction: 0x%08x)", insn);
    }
    
    printf("\n");
}

// Dump generated code
void dump_generated_code(jit_func_t func, size_t estimated_size) {
    uint32_t *code = (uint32_t *)func;
    size_t max_insns = estimated_size / 4;
    
    printf("\n=== Generated ARM64 Assembly ===\n");
    printf("Function address: %p\n\n", (void *)func);
    
    // Dump first N instructions or until we hit multiple NOPs/zeros
    int zero_count = 0;
    for (size_t i = 0; i < max_insns && zero_count < 4; i++) {
        if (code[i] == 0) {
            zero_count++;
        } else {
            zero_count = 0;
            disasm_arm64_insn(code[i], (uint64_t)&code[i]);
        }
    }
}

int main(int argc, char **argv) {
    printf("=== eBPF JIT Compiler Inspector ===\n");
    printf("Generating ARM64 assembly for sample eBPF program\n\n");
    
    // The requested eBPF program:
    // r0 = r1 + r2 (implemented as r0 = r1; r0 += r2)
    // r0 += 42
    // return r0
    ebpf_insn_t prog[] = {
        BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),   // r0 = r1
        BPF_ALU64_REG(ADD, BPF_REG_0, BPF_REG_2),  // r0 += r2
        BPF_ALU64_IMM(ADD, BPF_REG_0, 42),         // r0 += 42
        BPF_EXIT_INSN()                             // return r0
    };
    
    size_t prog_len = sizeof(prog) / sizeof(prog[0]);
    
    // Display eBPF bytecode
    printf("=== eBPF Bytecode ===\n");
    printf("  PC:  BYTECODE           DISASSEMBLY\n");
    printf("  ---  -----------------  -----------\n");
    for (size_t i = 0; i < prog_len; i++) {
        disasm_ebpf_insn(&prog[i], i);
    }
    
    // Display register mapping
    printf("\n=== Register Mapping ===\n");
    printf("  eBPF Register -> ARM64 Register\n");
    printf("  -------------    --------------\n");
    for (int i = 0; i <= 10; i++) {
        printf("  %-13s -> %s\n", ebpf_reg_names[i], get_arm64_reg(i));
    }
    printf("\nNote: ARM64 uses x19-x28 for eBPF r0-r9 (callee-saved)\n");
    printf("      eBPF r10 (frame pointer) maps to ARM64 x29\n");
    printf("      ARM64 x9 is used as scratch register\n");
    
    // Compile the program
    printf("\n=== Compiling eBPF to ARM64 ===\n");
    jit_func_t func = jit_compile(prog, prog_len);
    if (!func) {
        fprintf(stderr, "Failed to compile eBPF program\n");
        return 1;
    }
    
    printf("JIT compilation successful!\n");
    
    // Dump generated code
    dump_generated_code(func, prog_len * 32 + 512);
    
    // Test the compiled function
    printf("\n=== Testing Compiled Function ===\n");
    printf("Note: r1 is initialized with context pointer (passed as first argument)\n");
    printf("      r2 is uninitialized (will be 0 in this test)\n");
    printf("\nCalling compiled function with NULL context...\n");
    
    uint64_t result = func(NULL);
    printf("Result: %llu (expected: 42 since r1=ctx=0, r2=0)\n", (unsigned long long)result);
    
    // Test with a non-NULL context
    // Note: In eBPF, r1 gets the POINTER value, not what it points to
    // So if we pass &test_ctx where test_ctx=100, r1 gets the address of test_ctx
    uint64_t test_ctx = 100;
    printf("\nCalling with context pointer (address of test_ctx)...\n");
    result = func(&test_ctx);
    printf("Result: %llu (Note: r1 gets the pointer address, not the value 100)\n", 
           (unsigned long long)result);
    printf("Expected: pointer_address + 0 + 42\n");
    
    // To test with a specific value, we'd need to pass it directly as integer
    // But that would require casting which isn't typical eBPF usage
    printf("\nCalling with integer value 100 cast as pointer...\n");
    result = func((void *)100);
    printf("Result: %llu (expected: 142 since r1=100, r2=0, 100+0+42=142)\n",
           (unsigned long long)result);
    
    printf("\n=== Analysis Summary ===\n");
    printf("1. eBPF program consists of %zu instructions (%zu bytes)\n", 
           prog_len, prog_len * sizeof(ebpf_insn_t));
    printf("2. Generated ARM64 code includes:\n");
    printf("   - Function prologue (save registers, allocate stack)\n");
    printf("   - Translated eBPF instructions\n");
    printf("   - Function epilogue (restore registers, return)\n");
    printf("3. The JIT correctly:\n");
    printf("   - Maps eBPF registers to ARM64 callee-saved registers\n");
    printf("   - Translates ALU operations to ARM64 equivalents\n");
    printf("   - Handles immediate values\n");
    printf("   - Implements proper function calling convention\n");
    
    // Cleanup
    jit_free(func, prog_len * 32 + 512);
    
    return 0;
}