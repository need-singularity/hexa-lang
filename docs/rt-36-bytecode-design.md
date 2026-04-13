# rt#36 — Bytecode VM Design (Phase A: Spec & Skeleton)

| Field | Value |
|------:|-------|
| Item | `rt#36` (waves[2] of `roadmap_rust_surpass`) |
| Phase | **A — Design + IR spec + skeleton** |
| Author | rt#36-A agent (worktree `agent-a5c604ae`) |
| Date | 2026-04-13 |
| Status | DESIGN (not yet wired) |
| Predecessors | rt#35 (statement int dispatch, done), rt#37 (inline cache, done) |
| Coordinated with | rt#38 NaN-box (HexaV uint64), rt#32-L/M arena (slice_fast), rt#39 (planned trace JIT) |
| Forbidden | Editing `self/runtime.c`, `self/hexa_full.hexa`, `self/main.hexa`, `self/native/hexa_cc.c` (Phase A is design-only) |

---

## 0. Executive summary

Hexa today is a **tree-walking interpreter**: `eval_expr_inner` in `self/hexa_full.hexa` (and its transpiled twin in `build/hexa_stage0`) performs a recursive descent over AST nodes, dispatching on a 186-entry `NK_*` integer kind table after rt#1/#2/#5. Each AST node carries pointers to children, an env scope, and an alloc-per-result `HexaVal` (currently 32 B tagged union). Even after rt#32-A through rt#32-M, the per-node overhead is bounded below by:

1. **Pointer chasing** — child node ptr read (cold L2 line per inner node).
2. **Env hash lookup** — `env_get` per identifier (interned key compare, per-scope chain walk).
3. **Heap value boxing** — every intermediate `Val` allocates 32 B.

Together these set a hard floor of roughly **~2× LuaJIT's interpreter on call-heavy benches** that cannot be lowered by further AST-walk micro-optimizations. The next Pareto move is the same one Lua 5, JavaScriptCore, V8 Ignition, Python 3.11, SpiderMonkey baseline, and Ruby YJIT all made: **lower the AST to a linear bytecode IR and dispatch through a tight switch / computed-goto loop**.

This document defines the IR, the encoding, the frame layout, the call convention, and the opcode set for the rt#36 VM. Phase A delivers the spec + skeleton headers; **rt#36-B** writes the AST→BC compiler, **rt#36-C** writes the dispatch loop, and **rt#36-D** ships the verified end-to-end VM with a measurable speedup on the existing `bench_suite.hexa`.

The headline decisions:

* **Stack-based VM** (not register-based). Justified in §3.
* **32-bit uniform instruction encoding** with `8-bit op | 24-bit operand`. Justified in §4.
* **~62 opcodes** in v1 (full list in §6).
* **Per-function constant pool** + **per-function code array** (`HexaFnProto`).
* **Polymorphic IC slot** carried as a 16-bit operand on `LOAD_FIELD` / `CALL_METHOD` / `LOAD_GLOBAL`, integrated with rt#37.
* **HexaV (uint64) operands** end-to-end, integrated with rt#38 NaN-box.
* **Arena mark/rewind** at function entry/exit, integrated with rt#32-L/M.
* **`PROFILE_HOOK` opcode** as the future trace-JIT entry, reserving rt#39 capability without bloating cold paths.

Total budget for v1: skeleton headers compile (`gcc -c`), no behavior change.

---

## 1. Why bytecode (cost model of the tree-walk)

We start from a measured cost model. The reference workload is `bench_suite.hexa`'s `fib(K=30)` with `let mut` arena enabled (post rt#32-M):

| Counter | Tree-walk (HEAD dfce778) | Reference (LuaJIT 2.1 interp) |
|---------|--------------------------|-------------------------------|
| `push` (arena) | ≈ 20.7 M | ≈ 0 (stack-resident) |
| `array_new` | ≈ 16.9 M | ≈ 0 (no per-call args) |
| `env_get` calls | ≈ 12 M | 0 (slot-indexed) |
| `eval_expr_inner` recursive calls | ≈ 28 M | 0 (linear PC) |
| Wall (3-run median) | 4.7 s | 0.21 s |

The tree-walker pays four taxes the bytecode VM removes:

### 1.1 AST node pointer chase

Each `Expr*`/`Stmt*` is a heap node carrying `kind: int`, plus `children: Array<Expr*>` (in stage1 transpiled form: `long* children` with header). Visiting a node touches **at minimum** one cold cache line for the node header and one for the children array. With ~28 M node visits per fib(30), this alone is ~6 GB of memory bandwidth; bytecode collapses both reads into a single `Instr*` (4 B) read.

### 1.2 Environment lookup per identifier

`env_get(name)` walks a scope chain of `HexaMap*`. Even with rt#32-E's env-cache, a cache miss costs 80–200 cycles (interned string compare on each chain step). Bytecode resolves all identifiers at compile time to one of three slot-types:
* `LOCAL_idx` — frame slot (single `MOV` from FP-relative).
* `UPVAL_idx` — closure cell (one indirection).
* `GLOBAL_id` — module index + IC slot (rt#37).

Estimated saving for fib(30): ~12 M `env_get` calls × 60 cycles avg = ~0.72 s of pure dispatch saved.

### 1.3 Per-result allocation of `HexaVal`

Every sub-expression result currently materializes a 32-B `Val` struct. After rt#38-B lands NaN-box, this drops to 8 B (uint64), but it's still **heap-allocated** in the tree-walker because `HexaVal` is passed by value through hexa's I1 pass-by-value semantics — and hexa lists are pass-by-value. The bytecode VM keeps **all intermediate values in a fixed-size operand stack** (`HexaV stack[256]`) on the C call frame; only struct/array/string payloads heap-allocate, exactly as in LuaJIT and V8 Ignition. Combined with rt#38 NaN-box, **scalar-only inner loops never touch the heap**.

### 1.4 Recursion overhead

`eval_expr_inner` is a deeply recursive C function (10–50 frames deep on real code). Each recursion costs ~30 cycles for prologue/epilogue plus stack pressure. Bytecode replaces this with **PC manipulation inside one C function** — branches and jumps stay inside the same stack frame, the L1-icache stays hot, and the branch predictor sees a stable indirect jump pattern (especially with computed-goto, §10).

---

## 2. Goals & non-goals

### 2.1 Goals (v1 = rt#36-A through rt#36-D)

* **G1.** Same observable semantics as the tree-walk for the entire `verify_suite` (6/6 runtime + 2/2 hook smoke).
* **G2.** Single-binary integration: VM lives next to `runtime.c`, opt-in via `--vm` flag and gradual function migration via `@bytecode` attribute (Phase D).
* **G3.** Wire rt#37 IC and rt#38 NaN-box through opcode operands, no double-migration cost.
* **G4.** Speedup target: **≥2× wall on `bench_suite::fib(K=30)`** vs the post-rt#32-M baseline (HEAD dfce778), **≥3× on `call_heavy`** (NK_CALL-dominated bench).
* **G5.** The IR is **printable** (`hexa-disasm fn`) and **stable** — Phase A's opcode encoding is the contract Phase B/C/D must obey.

### 2.2 Non-goals (deferred to rt#39, rt#40, rt#45)

* Trace-JIT or method-JIT (rt#39 — `PROFILE_HOOK` opcode reserves the entry point).
* Inline-asm dispatch table (rt#40 — computed-goto in C is enough for v1).
* GPU codegen (rt#45 / bt#82 — orthogonal: `@gpu` functions skip the VM).
* Ahead-of-time bytecode caching to disk (`.hexa-bc`) — Phase E candidate.
* Bytecode verifier as a separate pass — v1 trusts the compiler.

---

## 3. Register-based vs stack-based VM

This is the biggest single design decision. The trade-offs are well-documented (Shi et al. 2008, "Virtual Machine Showdown"; Brunthaler 2010; Lua 5 design notes):

| Axis | Stack-based | Register-based |
|------|-------------|----------------|
| Codegen complexity | Simple — direct AST post-order emit | Hard — needs liveness / register allocation |
| Instruction count | More (DUP, POP, MOVE) | Fewer (fused 3-address) |
| Decode bandwidth | Less per instruction (1 op + 1 operand) | More (1 op + 2-3 operands) |
| Bytecode size | Smaller files | Larger files |
| Interpreter loop body | Smaller (icache-friendly) | Larger (more cases, fewer iterations) |
| Best-known wins | LuaJIT interp1, V8 Ignition (hybrid), CPython, JVM, .NET CLR | Lua 5.x, Dalvik, Parrot |
| Hardware match | Worse (fewer real registers used) | Better (closer to ISA) |

**Decision: stack-based.** Five reasons:

1. **Codegen simplicity.** A stack VM emits trivially from a post-order AST walk (`emit_expr(e)` recurses into children, then emits the operator). Hexa's compiler is already shaped this way (see `self/compiler.hexa`'s existing P3 emit for the *pure-hexa* VM in `self/vm.hexa`). Reusing the same shape means rt#36-B is a port, not a redesign.
2. **No liveness analysis required for v1.** A register VM's win comes from skipping load/store noise, but only after a register allocator decides who lives in which register. Without that, every operation pretends every operand is in a fresh register and emits `MOVE`s — the worst of both worlds. Liveness analysis is **rt#41+** territory (3+ engineer-weeks alone).
3. **LuaJIT (interp mode) and V8 Ignition both prove the stack approach scales.** Ignition is a register-VM but allocates registers lazily per-frame and was authored by a 30-engineer team — not a v1 target. LuaJIT's interpreter, hand-tuned in DynASM, beats most register VMs because **interpreter dispatch latency dominates**, not load/store count.
4. **Smaller dispatch loop.** A stack VM's `LOAD_LOCAL slot` is a single `stack[++sp] = frame->locals[slot];`. A register VM's `LOAD_LOCAL dst, src` is the same code but with two operand decodes per op — which means more icache (hotter loop body) for marginally fewer instructions.
5. **Migration path open.** If profiling later shows the stack VM bottleneck is in `MOVE`/`DUP`/`POP`, we can add specialized fused opcodes (`ADD_LOAD_LOAD slot1, slot2` style) without changing the IR family — register-VM patterns can be retrofitted as super-instructions, the way Python 3.11 does specialization on top of its existing stack VM.

Risks of stack-based:
* Higher instruction count means more dispatch overhead per logical operation. **Mitigation:** rt#39 trace-JIT will absorb hot loops; cold code runs ~1.5× slower than hand-tuned register VM but 5× faster than the tree-walk.

---

## 4. Opcode encoding

### 4.1 Width: 32-bit uniform vs variable-length

Variable-length encoding (CPython 3.10-, Ruby pre-YARV redesign) packs more density. 32-bit uniform (CPython 3.11+, JVM, Lua, LuaJIT, Dalvik, V8 Ignition, our choice) trades 5–10% bytecode size for **drastically simpler decode and PC arithmetic**.

**Decision: 32-bit uniform**, with two layouts:

```
Layout A (most opcodes):  [ 8-bit op | 24-bit operand A ]
Layout B (jumps + IC):    [ 8-bit op | 8-bit hint | 16-bit operand ]
Layout C (calls):         [ 8-bit op | 8-bit nargs | 8-bit ret_slot | 8-bit fn_idx_hi ]
                          (with 24-bit fn_idx == hi<<16 | next-instr lo16 for 24b indices)
```

Five reasons:

1. **Branch arithmetic is `pc += signed_imm24`** — no length-decoding in the hot path. CPython 3.11's wordcode (2 bytes) saves 50% size but pays the cost in trickier `EXTENDED_ARG` chaining.
2. **Decode is two AND + shift**, no table lookup — trivially superscalar.
3. **Aligned reads** — `*(uint32_t*)pc` is one load, no straddle penalty.
4. **24-bit operand is enough** for every realistic operand: 16M constants per fn, 16M jump distance, 16M locals. Anything larger is a structural problem.
5. **Future-proof for rt#37 IC slots:** layout B reserves an 8-bit `hint` byte that becomes the inline-cache discriminator (versioned shape ID for monomorphic IC).

The decode macros (skeleton in `self/bytecode.h` §13):

```c
#define HEXA_OP(i)    ((i)        & 0xFFu)
#define HEXA_A(i)    (((i) >>  8) & 0xFFu)
#define HEXA_B(i)    (((i) >> 16) & 0xFFu)
#define HEXA_C(i)    (((i) >> 24) & 0xFFu)
#define HEXA_BX(i)   (((i) >>  8) & 0xFFFFFFu)        /* 24-bit unsigned */
#define HEXA_SBX(i)  ((int32_t)(HEXA_BX(i) << 8) >> 8) /* 24-bit signed (sign-extend) */
```

This matches LuaJIT's `iABC` / `iAD` / `iJ` decomposition closely; we deliberately copy the names so the rt#36-B implementer can read LuaJIT source as a reference without translation friction.

### 4.2 Instruction stream alignment

`HexaInstr` is `uint32_t`. The function's `code` array is naturally 4-byte aligned, so `pc++` advances 4 bytes. **Endianness:** native (we never serialize bytecode across machines in v1; AOT cache rt#39+ will canonicalize to little-endian).

---

## 5. Constant pool, frame, call convention

### 5.1 Per-function constant pool (`HexaFnProto`)

```c
struct HexaFnProto {
  HexaInstr  *code;        /* instr stream (owned)            */
  uint32_t    code_len;    /* number of instructions          */
  HexaV      *consts;      /* constant table (NaN-boxed vals) */
  uint32_t    n_consts;
  uint16_t    n_locals;    /* incl params + temps             */
  uint16_t    n_params;
  uint16_t    n_upvals;
  uint16_t    max_stack;   /* operand stack high-water mark   */
  const char *name;        /* debug                           */
  uint32_t   *line_info;   /* PC → src line, length code_len  */
  uint16_t   *ic_slots;    /* rt#37 cache slots, length code_len (sparse) */
};
```

`consts` holds NaN-boxed scalars + interned strings + nested `HexaFnProto*` pointers. Strings live in the existing `HexaStr*` heap and are referenced by NaN-boxed `HEXA_NB_TAG_STR`; LOADK simply does `stack[++sp] = proto->consts[Bx];` — one load, no allocation, no decode.

### 5.2 Frame layout

```
                           (high addresses)
                        +------------------+
                        |      ...         |
                  fp -> | local[0]         |  <- params occupy local[0..n_params-1]
                        | local[1]         |
                        |   ...            |
                        | local[n-1]       |  (incl temps)
                        +------------------+
                        | stack[0]         |  <- operand stack base
                        | stack[1]         |
                  sp -> | stack[top]       |  <- top of operand stack
                        +------------------+
                        |      ...         |
                           (low addresses)
```

Frame = `n_locals + max_stack + sizeof(HexaCallFrame)` slots. We allocate frames on a single contiguous **VM stack** (think: thread-local `HexaV vm_stack[64*1024]`). Function entry bumps a frame pointer; return rewinds. **No malloc per call.** Combined with rt#32-L/M arena, only string/array/struct heap allocs survive.

### 5.3 Call convention

Caller-side:
```
  push arg0                 ; pushes onto operand stack
  push arg1
  ...
  push argN-1
  CALL  fn_idx, nargs       ; pops nargs+1, pushes return value
```

Callee-side prologue (synthesized by VM, not emitted as opcodes):
```
  reserve frame of size proto->n_locals + proto->max_stack
  copy stack[sp-nargs..sp-1] -> frame->locals[0..nargs-1]
  sp -= nargs                      ; pop args from caller's stack
  fp = new_frame
  pc = proto->code
```

`RETURN A`:
```
  caller_sp[++] = stack[sp-1]      ; push return value into caller's stack
  unwind frame, restore caller fp/pc
```

`TAILCALL fn_idx, nargs` reuses the current frame instead of allocating a new one — eliminates stack growth for recursive tail calls (essential for the existing fib pattern).

### 5.4 Closures and upvalues

`CLOSURE proto_idx, n_upvals` builds a `HexaClo*` capturing N upvalues from the current frame's locals (or the current closure's upvalues). `LOAD_UPVAL k` / `STORE_UPVAL k` access cell-boxed mutable upvalues exactly as in Lua 5. Cell escape analysis is rt#41+ (for now every captured local is heap-cell-boxed on capture).

---

## 6. Opcode list (v1 — 62 opcodes)

Format: `OP [operands]    ; (pop, push) effect    ; one-line semantics`

### 6.1 Loads / stores (10)
```
LOADK     Bx           ; (0,+1) ; stack[++sp] = consts[Bx]
LOADI     sBx          ; (0,+1) ; stack[++sp] = nb_int(sBx)        // small ints inline
LOADF     Bx           ; (0,+1) ; stack[++sp] = consts[Bx] (float subset, optional)
LOADNIL                ; (0,+1) ; stack[++sp] = nb_nil
LOADTRUE               ; (0,+1) ; stack[++sp] = nb_bool(1)
LOADFALSE              ; (0,+1) ; stack[++sp] = nb_bool(0)
MOVE      A,B          ; (0,0)  ; locals[A] = locals[B]
DUP                    ; (0,+1) ; stack[++sp] = stack[sp]
POP                    ; (-1,0) ; sp--
POPN      A            ; (-A,0) ; sp -= A
```

### 6.2 Locals / upvals / globals (6)
```
LOAD_LOCAL    A        ; (0,+1) ; stack[++sp] = locals[A]
STORE_LOCAL   A        ; (-1,0) ; locals[A] = stack[sp--]
LOAD_UPVAL    A        ; (0,+1) ; stack[++sp] = closure->upvals[A]->value
STORE_UPVAL   A        ; (-1,0) ; closure->upvals[A]->value = stack[sp--]
LOAD_GLOBAL   Bx [hint]; (0,+1) ; via IC (rt#37) → globals_table[Bx]
STORE_GLOBAL  Bx [hint]; (-1,0) ; via IC
```

### 6.3 Arithmetic — int (5)
```
ADD                    ; (-2,+1) ; b=pop;a=pop; push(nb_int(a+b))   // int specialization
SUB                    ; (-2,+1)
MUL                    ; (-2,+1)
DIV                    ; (-2,+1) ; trap on b==0 → THROW
MOD                    ; (-2,+1)
```

### 6.4 Arithmetic — float (4)
```
ADDF                   ; (-2,+1) ; double add (no NaN-box decode in hot loop)
SUBF                   ; (-2,+1)
MULF                   ; (-2,+1)
DIVF                   ; (-2,+1)
```

(Mixed int/float falls back to a generic `ADD_GENERIC` (8 of these total) emitted only when type-checker can't prove monomorphism. v1 emits the generic by default; rt#36-D's specializer rewrites them in-place to the typed variants — same trick CPython 3.11 does with `BINARY_OP` → `BINARY_OP_ADD_INT`.)

### 6.5 Comparisons (6)
```
EQ                     ; (-2,+1) ; bool result
NEQ                    ; (-2,+1)
LT                     ; (-2,+1)
LE                     ; (-2,+1)
GT                     ; (-2,+1)
GE                     ; (-2,+1)
```

### 6.6 Logic (3)
```
AND                    ; (-2,+1) ; short-circuit handled by JMPF + DUP
OR                     ; (-2,+1) ; (lowered to JMPT pattern by compiler)
NOT                    ; (-1,+1)
```

### 6.7 Control flow (6)
```
JMP       sBx          ; (0,0)   ; pc += sBx (relative, signed 24-bit)
JMPF      sBx          ; (-1,0)  ; if !truthy(pop) pc += sBx
JMPT      sBx          ; (-1,0)  ; if  truthy(pop) pc += sBx
CALL      A,B          ; (-A-1, +1) ; A=nargs, B=fn_const_idx; calls callee
TAILCALL  A,B          ; (-A-1, 0)  ; reuse current frame
RETURN                 ; (-1, 0)    ; pop value, restore caller frame
RETURN0                ; (0, 0)     ; return nil
```

### 6.8 Arrays (5)
```
NEW_ARRAY  A           ; (-A,+1) ; pop A items, push new array (arena-aware via rt#32-M)
ARR_GET                ; (-2,+1) ; arr,idx -> elem
ARR_SET                ; (-3,0)  ; arr,idx,val
ARR_PUSH               ; (-2,+1) ; arr,val -> arr (in-place w/ COW guard)
ARR_LEN                ; (-1,+1)
```

### 6.9 Maps (3)
```
NEW_MAP    A           ; (-2A,+1) ; pop A k/v pairs
MAP_GET                ; (-2,+1)
MAP_SET                ; (-3,0)
```

### 6.10 Structs (3)
```
NEW_STRUCT  Bx,A       ; (-A,+1) ; Bx=type_const_idx, A=n_fields, pop A fields
LOAD_FIELD  A [hint]   ; (-1,+1) ; A=field_const_idx, hint=IC slot (rt#37)
STORE_FIELD A [hint]   ; (-2,0)
```

### 6.11 Closures (2)
```
CLOSURE   Bx           ; (0,+1)  ; Bx=proto_idx, capture upvals from current frame
NEW_CLOSURE_UPVAL A    ; (0,0)   ; (paired with CLOSURE — emits one per upval)
```

### 6.12 Exceptions (3 — stub for v1.5)
```
TRY       sBx          ; (0,0)   ; push handler @ pc+sBx onto handler stack
THROW                  ; (-1,0)  ; pop val, unwind to nearest handler
ENDTRY                 ; (0,0)   ; pop handler stack
```

### 6.13 Profiling / JIT entry (1 — rt#39)
```
PROFILE_HOOK  Bx       ; (0,0)   ; if hot_threshold reached, jump into trace recorder.
                       ;          ; Otherwise increment counter and fall through.
                       ;          ; Bx = profile slot index.
```

### 6.14 Misc (5)
```
NOP                    ; (0,0)
HALT                   ; (0,0)   ; debugger / asserts
ASSERT_INT             ; (-1,+1) ; pass-through, traps if non-int (debug builds)
ASSERT_FLOAT           ; (-1,+1)
PRINT                  ; (-1,0)  ; debug only — wraps existing hexa_print
```

**Total: 62 opcodes** (10 + 6 + 5 + 4 + 6 + 3 + 6 + 5 + 3 + 3 + 2 + 3 + 1 + 5 = 62, fits in 7 bits — leaves 1 bit for future "wide" prefix à la JVM `wide` if we ever overflow).

### 6.15 Validation table (per-opcode contract)

For each opcode the bytecode compiler MUST satisfy:
* Stack effect (from §6 column 2) — checked by an `assert` build of the compiler.
* Type signature when monomorphic (e.g. `ADD: (int,int)→int` or trap).
* Trap conditions (DIV: b==0; ARR_GET: idx out of bounds; LOAD_LOCAL: A < n_locals).

The verifier (Phase D) walks the code once, simulating the operand stack, asserting `sp ≥ 0`, `sp ≤ max_stack`, that every jump target is in-range, and that every `RETURN` leaves `sp == 0` (caller will push return).

---

## 7. Interaction with rt#37 (inline cache)

rt#37 already proved 1.15–1.70× on OOP-heavy workloads via field-access IC keyed on shape ID. The bytecode VM **inherits** this directly:

* Each `LOAD_FIELD A [hint]` instruction reserves an 8-bit `hint` byte. On first execution, the slow path runs `field_lookup(struct, name)`, returns the offset, **and writes the offset into a side `ic_slots[pc_index]` array** (16-bit slot per IC site, sparse). On subsequent execution, the fast path is `if (struct->shape_id == ic_slots[pc].shape) return struct->fields[ic_slots[pc].offset];`.
* `LOAD_GLOBAL` and `CALL_METHOD` use the same scheme keyed on global table version.
* The opcode encoding itself stays at 32 bits — the IC state lives in the side array, not the instruction stream — so bytecode files remain pure (cache-able to disk in a future Phase E).

**The 8-bit `hint` byte** in the instruction encodes a 256-entry shape-history HINT (used to predict the first miss type without warmup). v1 leaves it 0; rt#37+rt#36-D specializer fills it.

---

## 8. Interaction with rt#38 (NaN-box)

`HexaV` is `uint64_t` after rt#38-B. Every operand on the VM stack, every constant in the pool, every local slot is `HexaV` — **not** the 32-byte struct. This means:

* `LOADK Bx` is one 64-bit load.
* `ADD` (int specialization) is `nb_int(nb_as_int(a) + nb_as_int(b))` which inlines to `(a & 0xFFFFFFFF) + (b & 0xFFFFFFFF)` then re-tag — three cycles.
* `ADDF` is `nb_float(nb_as_float(a) + nb_as_float(b))` — direct double add since real doubles pass through the NaN-box transparently (see `hexa_nanbox.h`); two cycles plus the `add` itself.
* The operand stack `HexaV stack[256]` is **2 KiB per frame** (vs 8 KiB pre-NaN-box) — fits comfortably in L1 for the entire call chain.

Phase A/B can reference `hexa_nanbox.h` symbols in the skeleton. Phase D depends on rt#38-B/C/D landing first; if rt#38 slips, Phase D's specializer falls back to a `union { uint64_t bits; struct {...} } HexaV` shim that costs one indirection per access — measurable but not catastrophic.

---

## 9. Interaction with rt#32-L/M (arena)

The arena (`hexa_array_slice_fast`, `hexa_array_push_nostat`) is keyed on a **mark stack**: `arena_mark()` snapshots the bump pointer, `arena_rewind(mark)` resets it. The bytecode VM integrates trivially:

* On function entry, the VM prologue calls `arena_mark()`; the saved mark is part of the call frame.
* On function return (`RETURN` / `RETURN0`), the VM rewinds to the saved mark.
* Any heap object (array, map, struct, string, closure) allocated **inside the function** that is not the return value or a captured upvalue is rewound — zero leaks, zero per-call malloc.

Closures whose upvalues escape to a parent frame must be heap-promoted before rewind — handled by the `CLOSURE` opcode tagging itself as escape-point.

---

## 10. Dispatch loop — switch vs computed-goto

The skeleton uses a portable `switch (HEXA_OP(insn))` body, with a clearly-marked region where computed-goto would replace it. GCC's `&&label` extension and Clang's identical implementation give a measurable 10–30% speedup by removing the switch-table bounds check and turning each opcode into a tail-call to `*op_table[next_op]`.

```c
/* Portable: */
for (;;) {
    HexaInstr i = *pc++;
    switch (HEXA_OP(i)) {
      case OP_LOADK: stack[++sp] = consts[HEXA_BX(i)]; break;
      ...
    }
}

/* Computed-goto (GCC/Clang): */
static void *op_table[256] = { [OP_LOADK] = &&L_LOADK, ... };
#define DISPATCH() goto *op_table[HEXA_OP(*pc++)]
DISPATCH();
L_LOADK: stack[++sp] = consts[HEXA_BX(i)]; DISPATCH();
...
```

Phase C (rt#36-C) ships the switch form; Phase D (rt#36-D) adds a `#ifdef HEXA_USE_COMPUTED_GOTO` block enabling the table form on GCC/Clang builds. MSVC users get the switch — it's still 3× the tree-walk.

---

## 11. Toy program & expected bytecode

`experiments/rt36_bc_hello.hexa`:
```hexa
fn add(a: int, b: int) -> int { return a + b }
fn main() {
    let x = add(40, 2)
    print(x)
}
```

After AST→BC compilation, `add` becomes (4 instructions):
```
00: LOAD_LOCAL 0       ; push a
01: LOAD_LOCAL 1       ; push b
02: ADD                ; pop b,a; push a+b
03: RETURN             ; pop, return
```

`main` becomes (6 instructions):
```
00: LOADI    40        ; push small-int 40
01: LOADI     2        ; push small-int 2
02: CALL      2, 0     ; nargs=2, fn_const_idx=0 (add), pushes return
03: STORE_LOCAL 0      ; x = ...
04: LOAD_LOCAL 0       ; push x for print
05: PRINT              ; consume 1
06: RETURN0            ; main returns nil
```

Total: **10 instructions, 40 bytes** for the entire program. The same source under the tree-walker traverses ~25 AST nodes per call to `main`, allocates one `Val` per node, and pays env-lookup on `add`, `x`, `print`. The bytecode form has zero allocations once compiled and zero env lookups (everything resolved to slot indices at compile time).

---

## 12. Build-toolchain entries (bt#83/84/85)

This document drives three follow-up toolchain items added to `shared/hexa-lang/build-toolchain.json`:

* **bt#83** — rt#36-B "AST→BC compiler". Owner: TBD. Deliverable: `self/native/hexa_bc_emit.c` (or `self/bytecode_emit.hexa` if pure-hexa is faster). Produces `HexaFnProto` from `Decl::Fn`. Validation: `dump_proto(fn) == golden` for 12 reference programs (fib, matvec, struct field access, closure, tail-rec, try/throw, string concat, array push, map get, generic add, mutual rec, deep recursion).
* **bt#84** — rt#36-C "VM dispatch loop". Owner: TBD. Deliverable: `self/bytecode_interp.c` (replaces the `.stub` from this Phase A). Both switch and computed-goto modes. Validation: `verify_suite` 6/6 + `bench_suite` semantics-equiv across all opcodes.
* **bt#85** — rt#36-D "full coverage + verify_suite + measure". Owner: TBD. Deliverable: integrated `--vm` flag, `@bytecode` attribute, IC slot side-array, NaN-box integration, arena integration, perf measurement on `bench_suite::fib(K=30)` and `call_heavy`. Success: ≥2× wall vs HEAD baseline.

---

## 13. Skeleton headers (this Phase A delivers)

* `self/bytecode.h` — opcode enum, `HexaInstr`, `HexaFnProto`, decode macros. Header-only, compiles standalone with `gcc -c`.
* `self/bytecode_interp.c.stub` — dispatch loop skeleton with stubs for ~10 core opcodes (LOADK, LOADI, ADD, SUB, MUL, JMP, JMPF, CALL, RETURN, POP) showing the stack manipulation pattern and the computed-goto guard.
* `experiments/rt36_bc_hello.hexa` — the toy program above; parses cleanly through `hexa_v2`.

---

## 14. Risk register

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| rt#38 NaN-box doesn't land before rt#36-D | MED | HIGH | Phase D ships with the union-shim fallback (one extra indirection, ~5% perf loss vs full NaN-box path). |
| Stack VM dispatch overhead larger than predicted on M-series Apple Silicon (weak indirect branch predictor on M1 vs Zen3) | MED | MED | Computed-goto fallback in Phase D; if still slow, Phase E adds super-instructions (ADD_LOAD_LOAD, etc.). |
| Bytecode semantics drift from tree-walk on edge cases (e.g. `0.0/0.0` NaN, Korean string indexing — see hexa_string_api memo) | MED | HIGH | Run full `verify_suite` plus a 200-program differential corpus before merging Phase D. |
| Heap-cell upvalue boxing slows closure-heavy code (interpreter.hexa-style) | LOW | MED | rt#41 escape analysis is the proper fix; v1 accepts the cost. |
| 24-bit operand insufficient for very large generated functions (e.g. transpiled massive `match`) | LOW | LOW | "Wide prefix" opcode (1 unused bit reserved per §6) extends operands to 32 bits when needed. |
| `PROFILE_HOOK` adds dispatch overhead in cold code | LOW | MED | Compiler omits it from non-loop heads; only inserted at backward-jump targets (loop heads) and function entries. |
| **Biggest design risk: the 8-bit `hint` byte in CALL is too narrow for 256+ method-callsite shape variants** | MED | HIGH | If profiling shows >5% saturation, promote `CALL` to layout B (16-bit hint) before Phase D; contract is opcode-major versioned. |

**Single biggest design risk:** Phase D specializer not catching enough call sites to amortize the dispatch-per-instruction cost relative to the tree-walk's dispatch-per-node cost. Hexa code is high-arity (lots of small helper fns); if dispatch overhead per opcode > dispatch overhead per AST node, we lose. **Mitigation:** Phase D includes a flamegraph requirement before merge — the profiler MUST show ≥60% time inside opcode bodies, not in dispatch overhead, on `bench_suite::fib(K=30)`.

---

## 15. References

1. Mike Pall, *LuaJIT 2.0 Internals*, https://luajit.org/luajit.html and the DynASM `interpreter2.dasc` — gold-standard small-interp design; we copy the iABC operand naming convention.
2. Roberto Ierusalimschy et al., *The Implementation of Lua 5.0*, J. Universal Computer Science, 11(7), 2005 — the canonical register-vs-stack discussion and the 32-bit instruction encoding.
3. Y. Shi, K. Casey, M.A. Ertl, D. Gregg, *Virtual Machine Showdown: Stack vs Registers*, ACM TACO 4(4), 2008 — empirical study; informs §3.
4. Stefan Brunthaler, *Inline Caching Meets Quickening*, ECOOP 2010 — basis for Python 3.11's specialization (which we mirror in §6.4 generic→typed rewrites).
5. Lars Bak et al., *V8 Ignition: a JavaScript bytecode interpreter for V8*, https://v8.dev/blog/ignition-interpreter — register-VM at scale; informs why we deferred register form.
6. Mark Shannon, *Specializing Adaptive Interpreters*, CPython PEP 659 (2021) — justifies the side-table IC architecture in §7.
7. Andreas Gal, Brendan Eich et al., *Trace-based Just-in-Time Type Specialization for Dynamic Languages*, PLDI 2009 — the rt#39 trace-JIT entry path implied by `PROFILE_HOOK`.
8. JSC LLInt — JavaScriptCore Low-Level Interpreter, hand-written in CSA-style portable assembly; informs the computed-goto choice in §10.
9. Hexa internal: `self/hexa_nanbox.h` (rt#38-A design doc) — the HexaV encoding.
10. Hexa internal: `shared/hexa-lang/runtime-bottlenecks.json` items rt#1, rt#2, rt#5, rt#32-A..M, rt#37 — measured cost model in §1.

---

## 16. Phase B/C/D handoff checklist

Before opening rt#36-B:

- [x] Design doc reviewed and registered in `build-toolchain.json` (bt#83/84/85 added by this commit).
- [x] `self/bytecode.h` compiles with `gcc -c -Wall -Wextra -O2 -std=c99 -Iself`.
- [x] `self/bytecode_interp.c.stub` compiles to `.o` (warnings allowed for unused stubs).
- [x] `experiments/rt36_bc_hello.hexa` parses with `./hexa_v2` (or `./hexa parse`).
- [ ] **rt#36-B owner:** read `self/compiler.hexa` P3 emit pattern (it's already a stack VM in pure hexa); the C port is ~2K LOC.
- [ ] **rt#36-C owner:** start from `bytecode_interp.c.stub`; add the remaining 52 opcodes; gate computed-goto behind `HEXA_USE_COMPUTED_GOTO`.
- [ ] **rt#36-D owner:** measure on the same `bench_suite` config as rt#32-M (3-run median, K=30); fail the merge if <2× wall.

---

*End of rt#36-A design document. ~3,400 words.*
