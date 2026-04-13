/* ═══════════════════════════════════════════════════════════
 *  bytecode.h — HEXA bytecode VM IR definitions (rt#36-A)
 *
 *  DESIGN-ONLY header for Tier-2 multi-session refactor rt#36.
 *  NOT yet wired into runtime.c — this is the IR specification
 *  and reference structs to be implemented in rt#36-B/C/D.
 *
 *  Spec: docs/rt-36-bytecode-design.md
 *
 *  Goal: replace the tree-walking interpreter (eval_expr_inner in
 *        self/hexa_full.hexa) with a linear bytecode VM. Stack-based,
 *        32-bit uniform instruction encoding, per-function constant
 *        pool, frame-local operand stack. Integrates with:
 *          - rt#37 inline cache (ic_slots side array)
 *          - rt#38 NaN-box HexaV (uint64 operand type)
 *          - rt#32-L/M arena (mark/rewind per call frame)
 *          - rt#39 trace JIT (PROFILE_HOOK opcode)
 *
 *  Success gate for Phase A: this header compiles with
 *    gcc -c -Wall -Wextra -O2 -std=c99 -Iself
 *  producing no errors (warnings OK for unused-static).
 * ═══════════════════════════════════════════════════════════ */
#ifndef HEXA_BYTECODE_H
#define HEXA_BYTECODE_H

#include <stdint.h>
#include <stddef.h>

/* ── Value type: NaN-boxed uint64 (rt#38). ───────────────────
 * During Phase A (rt#38 not yet wired into runtime.c), we ship a
 * typedef alias HexaV -> uint64_t that matches hexa_nanbox.h. When
 * rt#38-B flips the runtime typedef, this alias becomes a no-op.
 * Phase D's fallback for pre-rt#38 builds is a union-shim that
 * presents the same API at ~5% perf cost. */
#ifdef HEXA_NANBOX_H
/* Already included: use hexa_nanbox.h's HexaV directly. */
#else
typedef uint64_t HexaV;
#endif

/* ── Instruction type: 32-bit uniform. ───────────────────────
 * Layout A (most opcodes): [ 8-bit op | 24-bit operand ]
 * Layout B (IC-carrying):  [ 8-bit op | 8-bit hint | 16-bit operand ]
 * Layout C (calls):        [ 8-bit op | 8-bit A    | 16-bit B ]
 *
 * Decoders below work for all three; the opcode determines which
 * operand layout the bytecode compiler (rt#36-B) emitted. */
typedef struct HexaInstr {
    uint32_t raw;
} HexaInstr;

/* ── Decoder macros ──────────────────────────────────────────
 * These are the names LuaJIT 2.x uses (see luaconf.h iABC/iAD/iJ).
 * We copy them deliberately so Phase B/C/D implementers can read
 * LuaJIT source as a reference without translation. */
#define HEXA_OP(i)    ((uint8_t)((i).raw & 0xFFu))
#define HEXA_A(i)     ((uint8_t)(((i).raw >>  8) & 0xFFu))
#define HEXA_B(i)     ((uint8_t)(((i).raw >> 16) & 0xFFu))
#define HEXA_C(i)     ((uint8_t)(((i).raw >> 24) & 0xFFu))
#define HEXA_BX(i)    ((uint32_t)(((i).raw >> 8) & 0x00FFFFFFu))           /* 24-bit unsigned */
#define HEXA_SBX(i)   ((int32_t)((int32_t)((i).raw & 0xFFFFFF00u) >> 8))   /* 24-bit signed (sign-extend) */

/* Layout B: 8-bit op | 8-bit hint (bits 8..15) | 16-bit operand (bits 16..31) */
#define HEXA_HINT(i)   ((uint8_t) (((i).raw >>  8) & 0xFFu))
#define HEXA_OP16(i)   ((uint16_t)(((i).raw >> 16) & 0xFFFFu))

/* Layout C: 8-bit op | 8-bit A (bits 8..15) | 16-bit B (bits 16..31) */
#define HEXA_CA(i)     HEXA_A(i)
#define HEXA_CB16(i)   HEXA_OP16(i)

/* Encoders — used by rt#36-B compiler. */
static inline HexaInstr hexa_enc_abx (uint8_t op, uint32_t bx) {
    HexaInstr r; r.raw = (uint32_t)op | (bx & 0xFFFFFFu) << 8; return r;
}
static inline HexaInstr hexa_enc_sbx (uint8_t op, int32_t sbx) {
    HexaInstr r; r.raw = (uint32_t)op | ((uint32_t)(sbx & 0xFFFFFF) << 8); return r;
}
static inline HexaInstr hexa_enc_abc (uint8_t op, uint8_t a, uint8_t b, uint8_t c) {
    HexaInstr r;
    r.raw = (uint32_t)op
          | ((uint32_t)a << 8)
          | ((uint32_t)b << 16)
          | ((uint32_t)c << 24);
    return r;
}
static inline HexaInstr hexa_enc_op16(uint8_t op, uint8_t hint, uint16_t val16) {
    HexaInstr r;
    r.raw = (uint32_t)op
          | ((uint32_t)hint  << 8)
          | ((uint32_t)val16 << 16);
    return r;
}

/* ── Opcode enum (v1 = 62 opcodes, see §6 of design doc). ────
 * Numbering is stable — the bytecode files (future AOT .hexa-bc
 * cache) key on these values. Insertions only at the end. */
typedef enum HexaOp {
    /* Loads / stores (10) */
    OP_LOADK        = 0x00,   /* A: Bx=const_idx */
    OP_LOADI        = 0x01,   /* A: sBx=small-int literal */
    OP_LOADF        = 0x02,   /* A: Bx=float-const_idx */
    OP_LOADNIL      = 0x03,
    OP_LOADTRUE     = 0x04,
    OP_LOADFALSE    = 0x05,
    OP_MOVE         = 0x06,   /* A,B: locals[A] = locals[B] */
    OP_DUP          = 0x07,
    OP_POP          = 0x08,
    OP_POPN         = 0x09,   /* A: pop A items */

    /* Locals / upvals / globals (6) */
    OP_LOAD_LOCAL   = 0x0A,   /* A: slot */
    OP_STORE_LOCAL  = 0x0B,   /* A: slot */
    OP_LOAD_UPVAL   = 0x0C,   /* A: upval_idx */
    OP_STORE_UPVAL  = 0x0D,   /* A: upval_idx */
    OP_LOAD_GLOBAL  = 0x0E,   /* B: hint(8) + globals_idx(16) */
    OP_STORE_GLOBAL = 0x0F,   /* B: hint(8) + globals_idx(16) */

    /* Arithmetic — int specialization (5) */
    OP_ADD          = 0x10,
    OP_SUB          = 0x11,
    OP_MUL          = 0x12,
    OP_DIV          = 0x13,
    OP_MOD          = 0x14,

    /* Arithmetic — float specialization (4) */
    OP_ADDF         = 0x15,
    OP_SUBF         = 0x16,
    OP_MULF         = 0x17,
    OP_DIVF         = 0x18,

    /* Comparisons (6) */
    OP_EQ           = 0x19,
    OP_NEQ          = 0x1A,
    OP_LT           = 0x1B,
    OP_LE           = 0x1C,
    OP_GT           = 0x1D,
    OP_GE           = 0x1E,

    /* Logic (3) */
    OP_AND          = 0x1F,   /* reserved — usually lowered to JMPF */
    OP_OR           = 0x20,   /* reserved — usually lowered to JMPT */
    OP_NOT          = 0x21,

    /* Control flow (7) */
    OP_JMP          = 0x22,   /* sBx: relative */
    OP_JMPF         = 0x23,   /* sBx: jump if top is falsy, pops */
    OP_JMPT         = 0x24,   /* sBx: jump if top is truthy, pops */
    OP_CALL         = 0x25,   /* A=nargs, B16=fn_const_idx */
    OP_TAILCALL     = 0x26,   /* A=nargs, B16=fn_const_idx */
    OP_RETURN       = 0x27,   /* pop value, return to caller */
    OP_RETURN0      = 0x28,   /* return nil */

    /* Arrays (5) */
    OP_NEW_ARRAY    = 0x29,   /* A=count */
    OP_ARR_GET      = 0x2A,
    OP_ARR_SET      = 0x2B,
    OP_ARR_PUSH     = 0x2C,
    OP_ARR_LEN      = 0x2D,

    /* Maps (3) */
    OP_NEW_MAP      = 0x2E,   /* A=pairs */
    OP_MAP_GET      = 0x2F,
    OP_MAP_SET      = 0x30,

    /* Structs (3) — LOAD_FIELD / STORE_FIELD carry rt#37 IC hint */
    OP_NEW_STRUCT   = 0x31,   /* Bx=type_const_idx, side-packed n_fields */
    OP_LOAD_FIELD   = 0x32,   /* B: hint(8) + field_const_idx(16) */
    OP_STORE_FIELD  = 0x33,   /* B: hint(8) + field_const_idx(16) */

    /* Closures (2) */
    OP_CLOSURE          = 0x34,   /* Bx=proto_idx, followed by 0..n_upvals NEW_CLOSURE_UPVAL */
    OP_NEW_CLOSURE_UPVAL= 0x35,   /* A: source slot (0=local, 1=outer upval), B16: idx */

    /* Exceptions (3) — stub for v1.5 */
    OP_TRY          = 0x36,   /* sBx: handler pc-relative */
    OP_THROW        = 0x37,
    OP_ENDTRY       = 0x38,

    /* Profiling / JIT entry (1) — rt#39 trace recorder hook */
    OP_PROFILE_HOOK = 0x39,   /* Bx=profile_slot_idx */

    /* Misc (5) */
    OP_NOP          = 0x3A,
    OP_HALT         = 0x3B,
    OP_ASSERT_INT   = 0x3C,
    OP_ASSERT_FLOAT = 0x3D,
    OP_PRINT        = 0x3E,   /* debug only */

    OP__COUNT       = 0x3F    /* sentinel: update when inserting */
} HexaOp;

/* Number of opcodes in v1 (keep in sync with enum). */
#define HEXA_OP_COUNT 62

/* ── Function prototype (one per source-level fn decl). ──────
 * This is what rt#36-B emits from AST. The VM (rt#36-C) reads
 * these structures and executes `code` through the dispatch
 * loop. All pointers are owned by the enclosing HexaModule. */
typedef struct HexaFnProto {
    HexaInstr       *code;        /* instr stream (owned)                  */
    uint32_t         code_len;    /* instructions                          */
    HexaV           *consts;      /* constant table (NaN-boxed vals)       */
    uint32_t         n_consts;
    struct HexaFnProto **proto_pool; /* nested fn prototypes (for CLOSURE)  */
    uint32_t         n_protos;
    uint16_t         n_locals;    /* incl params + temps                   */
    uint16_t         n_params;
    uint16_t         n_upvals;
    uint16_t         max_stack;   /* operand stack high-water mark         */
    uint32_t        *line_info;   /* PC -> src line, length code_len       */
    uint16_t        *ic_slots;    /* rt#37 cache offsets (0 = slow path)   */
    uint32_t        *ic_shape;    /* rt#37 cached shape/version IDs        */
    uint32_t         n_ic_slots;  /* length of ic_slots/ic_shape           */
    const char      *name;        /* debug / disasm                        */
    uint32_t         flags;       /* HEXA_FN_* bits below                  */
} HexaFnProto;

/* Function proto flags. */
#define HEXA_FN_HAS_UPVALS       0x00000001u   /* closure must be built   */
#define HEXA_FN_TAIL_RECURSIVE   0x00000002u   /* compiler proved TCO safe */
#define HEXA_FN_HOT              0x00000004u   /* PROFILE_HOOK triggered  */
#define HEXA_FN_NO_ALLOC         0x00000008u   /* arena rewindable        */
#define HEXA_FN_VARIADIC         0x00000010u

/* ── Call frame (one per active fn invocation). ──────────────
 * Laid out on a contiguous thread-local VM stack. `locals` and
 * the operand stack live immediately above the frame header in
 * the same VM-stack region so the whole frame is one allocation. */
typedef struct HexaCallFrame {
    struct HexaFnProto *proto;   /* executing prototype               */
    HexaInstr          *pc;      /* current instruction pointer       */
    HexaV              *fp;      /* base of this frame's locals       */
    HexaV              *sp;      /* top of this frame's operand stack */
    uint32_t            arena_mark; /* rt#32-L/M arena restore point  */
    struct HexaClo     *closure; /* non-NULL if invoked via CLOSURE   */
    struct HexaCallFrame *caller;/* backlink                          */
    uint32_t            flags;
} HexaCallFrame;

/* Closure object — heap-boxed, pointed to via NaN-box TAG_CLOSURE. */
typedef struct HexaUpval {
    HexaV  value;    /* if open: pointer into stack; if closed: the val */
    HexaV *slot;     /* non-NULL while upval is open */
} HexaUpval;

typedef struct HexaClo {
    HexaFnProto *proto;
    uint32_t     n_upvals;
    HexaUpval  **upvals;   /* length n_upvals */
} HexaClo;

/* ── VM state (global, thread-local in rt#39+). ──────────────
 * Phase A declares the shape; rt#36-C instantiates it. */
typedef struct HexaVM {
    HexaV          *stack;       /* contiguous VM stack (owned)         */
    uint32_t        stack_cap;   /* capacity in HexaV slots             */
    HexaV          *stack_top;   /* first unused slot (sp + 1)          */
    HexaCallFrame  *frame;       /* current frame                       */
    HexaCallFrame  *frame_base;  /* bottom of frame stack               */
    HexaCallFrame  *frame_top;   /* next unused frame slot              */
    uint32_t        frame_cap;
    HexaV          *globals;     /* globals table (indexed by id)       */
    uint32_t        n_globals;
    uint32_t        globals_version; /* bumped on any STORE_GLOBAL      */
    /* Profiling counters (rt#39). */
    uint32_t       *hot_counts;  /* per-profile-slot counter            */
    uint32_t        n_hot_slots;
    uint32_t        hot_threshold;
} HexaVM;

/* ── API surface (Phase C+ implements). ─────────────────────── */

/* Construct a VM with a given stack / frame capacity. */
extern HexaVM *hexa_vm_new(uint32_t stack_cap, uint32_t frame_cap);

/* Release all VM memory (does not free HexaFnProtos). */
extern void hexa_vm_free(HexaVM *vm);

/* Run proto to completion, returning its return value as HexaV.
 * Args pushed onto vm->stack_top before the call; pops them. */
extern HexaV hexa_vm_call(HexaVM *vm, HexaFnProto *proto, HexaV *args, uint32_t nargs);

/* Disassemble proto to stderr (Phase B debug tool). */
extern void hexa_vm_disasm(const HexaFnProto *proto);

/* Verify proto's stack balance / jump targets / local counts (Phase D). */
extern int hexa_vm_verify(const HexaFnProto *proto, char *err, size_t err_cap);

#endif /* HEXA_BYTECODE_H */
