// ═══════════════════════════════════════════════════════════
//  HEXA Native Code Generator — ARM64 Mach-O Direct Output
//  Zero dependency: no gcc, no as, no ld
//  .hexa → tokenize → parse → ARM64 machine code → executable
// ═══════════════════════════════════════════════════════════

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>

// ══════════════════════════════════════════════════════════
// ARM64 Instruction Encoding
// ══════════════════════════════════════════════════════════

// Register aliases
#define X0  0
#define X1  1
#define X2  2
#define X8  8
#define X16 16
#define X29 29  // frame pointer
#define X30 30  // link register (return address)
#define SP  31  // stack pointer (in some encodings)
#define XZR 31  // zero register

// ARM64 instruction builders
static uint32_t arm64_movz(int rd, uint16_t imm, int shift) {
    // MOVZ Xd, #imm, LSL #shift
    return 0xD2800000 | ((shift/16) << 21) | ((uint32_t)imm << 5) | rd;
}

static uint32_t arm64_movk(int rd, uint16_t imm, int shift) {
    // MOVK Xd, #imm, LSL #shift
    return 0xF2800000 | ((shift/16) << 21) | ((uint32_t)imm << 5) | rd;
}

static uint32_t arm64_svc(uint16_t imm) {
    // SVC #imm (supervisor call / syscall)
    return 0xD4000001 | ((uint32_t)imm << 5);
}

static uint32_t arm64_ret() {
    // RET (return via X30)
    return 0xD65F03C0;
}

static uint32_t arm64_add_imm(int rd, int rn, uint16_t imm) {
    // ADD Xd, Xn, #imm
    return 0x91000000 | ((uint32_t)imm << 10) | (rn << 5) | rd;
}

static uint32_t arm64_sub_imm(int rd, int rn, uint16_t imm) {
    // SUB Xd, Xn, #imm
    return 0xD1000000 | ((uint32_t)imm << 10) | (rn << 5) | rd;
}

static uint32_t arm64_add_reg(int rd, int rn, int rm) {
    // ADD Xd, Xn, Xm
    return 0x8B000000 | (rm << 16) | (rn << 5) | rd;
}

static uint32_t arm64_sub_reg(int rd, int rn, int rm) {
    // SUB Xd, Xn, Xm
    return 0xCB000000 | (rm << 16) | (rn << 5) | rd;
}

static uint32_t arm64_mul(int rd, int rn, int rm) {
    // MUL Xd, Xn, Xm (alias for MADD Xd, Xn, Xm, XZR)
    return 0x9B007C00 | (rm << 16) | (rn << 5) | rd;
}

static uint32_t arm64_sdiv(int rd, int rn, int rm) {
    // SDIV Xd, Xn, Xm
    return 0x9AC00C00 | (rm << 16) | (rn << 5) | rd;
}

static uint32_t arm64_stp(int rt, int rt2, int rn, int imm7) {
    // STP Xt, Xt2, [Xn, #imm7*8]!  (pre-index)
    return 0xA9800000 | ((imm7 & 0x7F) << 15) | (rt2 << 10) | (rn << 5) | rt;
}

static uint32_t arm64_ldp(int rt, int rt2, int rn, int imm7) {
    // LDP Xt, Xt2, [Xn], #imm7*8  (post-index)
    return 0xA8C00000 | ((imm7 & 0x7F) << 15) | (rt2 << 10) | (rn << 5) | rt;
}

static uint32_t arm64_bl(int32_t offset) {
    // BL #offset (branch with link, offset in instructions)
    return 0x94000000 | (offset & 0x03FFFFFF);
}

static uint32_t arm64_b(int32_t offset) {
    // B #offset
    return 0x14000000 | (offset & 0x03FFFFFF);
}

static uint32_t arm64_cmp_imm(int rn, uint16_t imm) {
    // CMP Xn, #imm (alias for SUBS XZR, Xn, #imm)
    return 0xF1000000 | ((uint32_t)imm << 10) | (rn << 5) | XZR;
}

static uint32_t arm64_cmp_reg(int rn, int rm) {
    // CMP Xn, Xm
    return 0xEB000000 | (rm << 16) | (rn << 5) | XZR;
}

static uint32_t arm64_b_cond(int cond, int32_t offset) {
    // B.cond #offset (offset in instructions)
    return 0x54000000 | ((offset & 0x7FFFF) << 5) | cond;
}

// Condition codes
#define COND_EQ 0x0
#define COND_NE 0x1
#define COND_LT 0xB
#define COND_LE 0xD
#define COND_GT 0xC
#define COND_GE 0xA

static uint32_t arm64_mov_reg(int rd, int rm) {
    // MOV Xd, Xm (alias for ORR Xd, XZR, Xm)
    return 0xAA0003E0 | (rm << 16) | rd;
}

static uint32_t arm64_str(int rt, int rn, int imm12) {
    // STR Xt, [Xn, #imm12*8]
    return 0xF9000000 | ((imm12 & 0xFFF) << 10) | (rn << 5) | rt;
}

static uint32_t arm64_ldr(int rt, int rn, int imm12) {
    // LDR Xt, [Xn, #imm12*8]
    return 0xF9400000 | ((imm12 & 0xFFF) << 10) | (rn << 5) | rt;
}

// ══════════════════════════════════════════════════════════
// Code Buffer
// ══════════════════════════════════════════════════════════

typedef struct {
    uint32_t* code;
    int len;
    int cap;
    uint8_t* data;   // string data section
    int data_len;
    int data_cap;
} CodeBuf;

CodeBuf* codebuf_new() {
    CodeBuf* cb = calloc(1, sizeof(CodeBuf));
    cb->cap = 4096;
    cb->code = malloc(cb->cap * sizeof(uint32_t));
    cb->data_cap = 4096;
    cb->data = malloc(cb->data_cap);
    return cb;
}

void emit(CodeBuf* cb, uint32_t inst) {
    if (cb->len >= cb->cap) {
        cb->cap *= 2;
        cb->code = realloc(cb->code, cb->cap * sizeof(uint32_t));
    }
    cb->code[cb->len++] = inst;
}

int emit_data(CodeBuf* cb, const char* str) {
    int offset = cb->data_len;
    int slen = strlen(str) + 1;
    while (cb->data_len + slen > cb->data_cap) {
        cb->data_cap *= 2;
        cb->data = realloc(cb->data, cb->data_cap);
    }
    memcpy(cb->data + cb->data_len, str, slen);
    cb->data_len += slen;
    // Align to 8 bytes
    while (cb->data_len % 8) cb->data[cb->data_len++] = 0;
    return offset;
}

// Load 64-bit immediate into register
void emit_mov64(CodeBuf* cb, int rd, uint64_t val) {
    emit(cb, arm64_movz(rd, val & 0xFFFF, 0));
    if (val > 0xFFFF)
        emit(cb, arm64_movk(rd, (val >> 16) & 0xFFFF, 16));
    if (val > 0xFFFFFFFF)
        emit(cb, arm64_movk(rd, (val >> 32) & 0xFFFF, 32));
    if (val > 0xFFFFFFFFFFFF)
        emit(cb, arm64_movk(rd, (val >> 48) & 0xFFFF, 48));
}

// ══════════════════════════════════════════════════════════
// Mach-O 64-bit ARM64 Binary Generation
// ══════════════════════════════════════════════════════════

// Mach-O header constants
#define MH_MAGIC_64     0xFEEDFACF
#define MH_EXECUTE      2
#define CPU_TYPE_ARM64  0x0100000C
#define CPU_SUBTYPE_ALL 0

// Load command types
#define LC_SEGMENT_64   0x19
#define LC_MAIN         0x80000028
#define LC_DYLD_INFO    0x80000022
#define LC_SYMTAB       0x02
#define LC_DYSYMTAB     0x0B
#define LC_LOAD_DYLINKER 0x0E
#define LC_LOAD_DYLIB   0x0C
#define LC_UUID         0x1B

// Segment/section constants
#define VM_PROT_READ    1
#define VM_PROT_WRITE   2
#define VM_PROT_EXECUTE 4

// Generate a minimal Mach-O executable
// This creates a static executable that uses raw syscalls (no libc)
int generate_macho(CodeBuf* cb, const char* output_path) {
    FILE* f = fopen(output_path, "wb");
    if (!f) { perror("fopen"); return 1; }

    // Calculate sizes
    uint32_t code_size = cb->len * 4;
    uint32_t data_size = cb->data_len;

    // Align sizes to page (16KB on ARM64 macOS)
    uint32_t page_size = 0x4000;
    uint32_t header_size = page_size;  // header + load commands in first page
    uint32_t text_size = (code_size + page_size - 1) & ~(page_size - 1);
    uint32_t data_aligned = (data_size + page_size - 1) & ~(page_size - 1);

    // ── Mach-O Header ──
    uint32_t header[8] = {
        MH_MAGIC_64,
        CPU_TYPE_ARM64,
        CPU_SUBTYPE_ALL,
        MH_EXECUTE,
        2,              // ncmds (LC_SEGMENT_64 for __TEXT, LC_MAIN)
        0,              // sizeofcmds (filled later)
        0x00200085,     // flags: MH_PIE | MH_DYLDLINK | etc
        0               // reserved
    };

    // For a truly minimal static binary, we use:
    // 1. __TEXT segment with code
    // 2. LC_MAIN entry point

    // LC_SEGMENT_64 for __TEXT (covers everything)
    uint8_t seg_cmd[72] = {0};
    *(uint32_t*)&seg_cmd[0] = LC_SEGMENT_64;
    *(uint32_t*)&seg_cmd[4] = 72;  // cmdsize
    memcpy(&seg_cmd[8], "__TEXT\0\0\0\0\0\0\0\0\0\0\0", 16);  // segname
    *(uint64_t*)&seg_cmd[24] = 0;                // vmaddr
    *(uint64_t*)&seg_cmd[32] = header_size + text_size;  // vmsize
    *(uint64_t*)&seg_cmd[40] = 0;                // fileoff
    *(uint64_t*)&seg_cmd[48] = header_size + text_size;  // filesize
    *(uint32_t*)&seg_cmd[56] = VM_PROT_READ | VM_PROT_EXECUTE;  // maxprot
    *(uint32_t*)&seg_cmd[60] = VM_PROT_READ | VM_PROT_EXECUTE;  // initprot
    *(uint32_t*)&seg_cmd[64] = 0;  // nsects
    *(uint32_t*)&seg_cmd[68] = 0;  // flags

    // LC_MAIN
    uint8_t main_cmd[24] = {0};
    *(uint32_t*)&main_cmd[0] = LC_MAIN;
    *(uint32_t*)&main_cmd[4] = 24;
    *(uint64_t*)&main_cmd[8] = header_size;  // entry offset (start of code)
    *(uint64_t*)&main_cmd[16] = 0;           // stack size (0 = default)

    // Update header
    header[4] = 2;  // ncmds
    header[5] = 72 + 24;  // sizeofcmds

    // Write file
    fwrite(header, 1, 32, f);
    fwrite(seg_cmd, 1, 72, f);
    fwrite(main_cmd, 1, 24, f);

    // Pad to page boundary
    uint32_t header_written = 32 + 72 + 24;
    uint8_t zero = 0;
    for (uint32_t i = header_written; i < header_size; i++) fwrite(&zero, 1, 1, f);

    // Write code
    fwrite(cb->code, 4, cb->len, f);

    // Pad code to page boundary
    for (uint32_t i = code_size; i < text_size; i++) fwrite(&zero, 1, 1, f);

    fclose(f);
    chmod(output_path, 0755);

    printf("[native] Generated %s (%d bytes, %d instructions)\n",
           output_path, header_size + text_size, cb->len);
    return 0;
}

// ══════════════════════════════════════════════════════════
// Test: generate a minimal program that exits with code 42
// ══════════════════════════════════════════════════════════

int main(int argc, char** argv) {
    printf("=== HEXA Native Code Generator (ARM64 Mach-O) ===\n\n");

    CodeBuf* cb = codebuf_new();

    // Program: exit(42)
    // macOS ARM64 syscall: x16 = syscall number, x0 = arg1
    // exit = syscall 1
    emit_mov64(cb, X0, 42);       // x0 = 42 (exit code)
    emit_mov64(cb, X16, 1);       // x16 = 1 (SYS_exit)
    emit(cb, arm64_svc(0x80));    // syscall

    const char* out = argc > 1 ? argv[1] : "/tmp/hexa_native_test";
    int ret = generate_macho(cb, out);
    if (ret == 0) {
        printf("Run: %s ; echo $?\n", out);
        printf("Expected: 42\n");
    }

    free(cb->code);
    free(cb->data);
    free(cb);
    return ret;
}
