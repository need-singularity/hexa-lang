# rt#36 Bytecode VM — Opcode Coverage & Implementation Guide

> Companion to `docs/rt-36-bytecode-design.md`. This file catalogs every
> opcode in the v1 ISA (63 total), states its current VM dispatch status,
> and lays out the priority tracks for closing the gap to a fully usable
> bytecode backend.
>
> Source-of-truth references (read-only):
>   - `self/bytecode.h`               — canonical enum (`HexaOp`)
>   - `self/bc_emitter.hexa`          — constants `BC_OP_*` and emit sites
>   - `self/bc_vm.hexa`               — dispatch ladder (`bc_vm_run_loop`)
>   - `docs/rt-36-bytecode-design.md` — overall v1 ISA spec
>
> Status snapshot (2026-04-14):
>   - ISA size: **63 opcodes** (0x00..0x3E, `BC_OP_COUNT = 63`)
>   - VM implemented: **38 / 63** (60.3%)
>   - VM unimplemented: **25 / 63** (39.7%)
>   - Smoke tests: 22 / 22 PASS, collatz / primes: 5 / 5 PASS
>   - Float arith tests (ADDF/SUBF/MULF/DIVF): PASS via hand-rolled
>     bytecode in `self/test_bc_vm_float.hexa` — the **emitter has not
>     yet been taught to lower** float `BinOp` to the `F` variants.

---

## 1. Coverage summary table

Count of opcodes by class × dispatch status.

| Class                  | Total | Done | TODO |
|------------------------|-------|------|------|
| Loads / stores         |  10   |   9  |   1  |
| Locals / upvals / glbl |   6   |   4  |   2  |
| Int arith              |   5   |   5  |   0  |
| Float arith            |   4   |   4  |   0  |
| Comparisons            |   6   |   6  |   0  |
| Logic                  |   3   |   1  |   2  |
| Control flow           |   7   |   6  |   1  |
| Arrays                 |   5   |   0  |   5  |
| Maps                   |   3   |   0  |   3  |
| Structs                |   3   |   0  |   3  |
| Closures               |   2   |   0  |   2  |
| Exceptions             |   3   |   0  |   3  |
| Profiling              |   1   |   0  |   1  |
| Misc                   |   5   |   3  |   2  |
| **TOTAL**              | **63**| **38**| **25** |

(Note: the `Done` column counts VM dispatch implementation. Three float
opcodes — `ADDF/SUBF/MULF/DIVF` — execute correctly in the VM, but the
emitter does not lower float-typed `BinOp` to them. Track A below
closes that emitter gap.)

---

## 2. Full opcode inventory

Legend:

- `S` — dispatch status in `bc_vm.hexa`: `OK` / `TODO`
- `E` — does `bc_emitter.hexa` ever emit it? `Y` / `N` / `N*` (naming
  table only, not an actual emit)
- `Diff` — implementation difficulty: `triv` (3-5 lines), `med`
  (10-20 lines), `hard` (needs side-table / runtime helper)
- `Block` — benches that are currently blocked by this opcode being
  missing (or by the emitter not producing it)

### 2.1 Loads / stores (0x00..0x09)

| Hex  | Name       | S   | E  | Diff | Deps / Notes                                | Block              |
|------|------------|-----|----|------|---------------------------------------------|--------------------|
| 0x00 | LOADK      | OK  | Y  |   -  | const-pool string → int/string coerce       |  -                 |
| 0x01 | LOADI      | OK  | Y  |   -  | inline sBx small-int                         |  -                 |
| 0x02 | LOADF      | OK  | Y  |   -  | const-pool string → float (`to_float`)       |  -                 |
| 0x03 | LOADNIL    | OK  | Y  |   -  | pushes `BC_NIL` sentinel                     |  -                 |
| 0x04 | LOADTRUE   | OK  | Y  |   -  |  -                                           |  -                 |
| 0x05 | LOADFALSE  | OK  | Y  |   -  |  -                                           |  -                 |
| 0x06 | MOVE       | TODO| N* | triv | `locals[A] = locals[B]`                      |   -                |
| 0x07 | DUP        | TODO| N* | triv | `push(peek())`                               |   -                |
| 0x08 | POP        | OK  | Y  |   -  |                                              |  -                 |
| 0x09 | POPN       | TODO| N* | triv | `for _ in 0..A: pop()`                       |   -                |

### 2.2 Locals / upvals / globals (0x0A..0x0F)

| Hex  | Name          | S   | E  | Diff | Deps / Notes                           | Block                 |
|------|---------------|-----|----|------|----------------------------------------|-----------------------|
| 0x0A | LOAD_LOCAL    | OK  | Y  |  -   | `push(locals[fp+A])`                   | -                     |
| 0x0B | STORE_LOCAL   | OK  | Y  |  -   | `locals[fp+A] = pop()`                 | -                     |
| 0x0C | LOAD_UPVAL    | TODO| N* | med  | needs closure struct w/ upval array     | closure benches       |
| 0x0D | STORE_UPVAL   | TODO| N* | med  | needs closure struct w/ upval array     | closure benches       |
| 0x0E | LOAD_GLOBAL   | OK  | Y  |  -   | linear scan on name (MVP)              | -                     |
| 0x0F | STORE_GLOBAL  | OK  | Y  |  -   | ditto                                  | -                     |

### 2.3 Int arithmetic (0x10..0x14)

| Hex  | Name | S  | E | Diff | Deps | Block |
|------|------|----|---|------|------|-------|
| 0x10 | ADD  | OK | Y |  -   |  -   |  -    |
| 0x11 | SUB  | OK | Y |  -   |  -   |  -    |
| 0x12 | MUL  | OK | Y |  -   |  -   |  -    |
| 0x13 | DIV  | OK | Y |  -   | divzero trap | -  |
| 0x14 | MOD  | OK | Y |  -   | modzero trap | -  |

### 2.4 Float arithmetic (0x15..0x18)

| Hex  | Name | S  | E | Diff | Deps / Notes                              | Block          |
|------|------|----|---|------|-------------------------------------------|----------------|
| 0x15 | ADDF | OK | N | triv | **VM done, emitter lowering missing**      | float benches  |
| 0x16 | SUBF | OK | N | triv | ditto                                     | float benches  |
| 0x17 | MULF | OK | N | triv | ditto                                     | float benches  |
| 0x18 | DIVF | OK | N | triv | ditto, divzero→float-trap                 | float benches  |

Dispatch logic already present at `self/bc_vm.hexa:578..595`. The
missing piece is a type-driven emit site in `bc_emit_binop` that
chooses `BC_OP_ADDF` when both operand types are float. Covered by
Track A below.

### 2.5 Comparisons (0x19..0x1E)

| Hex  | Name | S  | E | Diff | Deps | Block |
|------|------|----|---|------|------|-------|
| 0x19 | EQ   | OK | Y |  -   |  -   |  -    |
| 0x1A | NEQ  | OK | Y |  -   |  -   |  -    |
| 0x1B | LT   | OK | Y |  -   |  -   |  -    |
| 0x1C | LE   | OK | Y |  -   |  -   |  -    |
| 0x1D | GT   | OK | Y |  -   |  -   |  -    |
| 0x1E | GE   | OK | Y |  -   |  -   |  -    |

### 2.6 Logic (0x1F..0x21)

| Hex  | Name | S   | E | Diff | Deps / Notes                                                    | Block           |
|------|------|-----|---|------|-----------------------------------------------------------------|-----------------|
| 0x1F | AND  | TODO| Y | triv | **emitter emits it**, VM must evaluate as short-circuit-aware   | any `&&` expr   |
| 0x20 | OR   | TODO| Y | triv | **emitter emits it**, VM must evaluate as short-circuit-aware   | any `||` expr   |
| 0x21 | NOT  | OK  | Y |  -   | `push(!truthy(pop()))`                                          |  -              |

> Historical note: the design doc lists AND/OR as "reserved — usually
> lowered to JMPF/JMPT". However the current emitter path in
> `bc_emit_binop` emits them literally (see `bc_emitter.hexa:733-734`).
> Two options: implement them as plain eager `pop/pop/push(and-result)`
> (triv, 6 lines, matches emitter's current eager push of both args),
> or rewrite emitter to lower to JMPF/JMPT chains (short-circuit, med).
> **Track A picks the triv path** — matches what emitter already does
> and unblocks all existing `&&/||` code immediately.

### 2.7 Control flow (0x22..0x28)

| Hex  | Name     | S   | E  | Diff | Deps / Notes                              | Block              |
|------|----------|-----|----|------|-------------------------------------------|--------------------|
| 0x22 | JMP      | OK  | Y  |  -   |  -                                        |  -                 |
| 0x23 | JMPF     | OK  | Y  |  -   |  -                                        |  -                 |
| 0x24 | JMPT     | OK  | Y  |  -   |  -                                        |  -                 |
| 0x25 | CALL     | OK  | Y  |  -   | `bc_vm_push_frame`                        |  -                 |
| 0x26 | TAILCALL | TODO| N* | med  | pop caller frame before callee push-frame | fib deep recursion |
| 0x27 | RETURN   | OK  | Y  |  -   |  -                                        |  -                 |
| 0x28 | RETURN0  | OK  | Y  |  -   |  -                                        |  -                 |

### 2.8 Arrays (0x29..0x2D)

| Hex  | Name      | S   | E  | Diff | Deps / Notes                                       | Block           |
|------|-----------|-----|----|------|----------------------------------------------------|-----------------|
| 0x29 | NEW_ARRAY | TODO| N* | med  | A=count; pop A items, push new hexa array           | array benches   |
| 0x2A | ARR_GET   | TODO| N* | med  | pop idx, pop arr, push arr[idx] + bounds trap       | array benches   |
| 0x2B | ARR_SET   | TODO| N* | med  | pop v, pop idx, pop arr, rebuild arr                | array benches   |
| 0x2C | ARR_PUSH  | TODO| N* | med  | pop v, pop arr, push arr.push(v) back               | array benches   |
| 0x2D | ARR_LEN   | TODO| N* | triv | pop arr, push len(arr)                              | array benches   |

### 2.9 Maps (0x2E..0x30)

| Hex  | Name    | S   | E  | Diff | Deps / Notes                         | Block       |
|------|---------|-----|----|------|--------------------------------------|-------------|
| 0x2E | NEW_MAP | TODO| N* | med  | A=pairs (k0,v0,...) on stack          | map benches |
| 0x2F | MAP_GET | TODO| N* | med  | pop key, pop map, push lookup         | map benches |
| 0x30 | MAP_SET | TODO| N* | med  | pop v, pop key, pop map, rebuild map  | map benches |

Stage1 hexa's built-in dict literal uses parallel-array dicts (see
runtime.c `hexa_dict_*` helpers). MVP VM can box a dict into the
operand stack as a single hexa value.

### 2.10 Structs (0x31..0x33)

| Hex  | Name         | S   | E  | Diff | Deps / Notes                                      | Block                  |
|------|--------------|-----|----|------|---------------------------------------------------|------------------------|
| 0x31 | NEW_STRUCT   | TODO| N* | hard | needs type-const idx → field layout + side IC     | all struct benches     |
| 0x32 | LOAD_FIELD   | TODO| N* | hard | Layout B: hint(8) + field_const_idx(16); needs rt#37 IC | all struct benches     |
| 0x33 | STORE_FIELD  | TODO| N* | hard | ditto + version bump                              | all struct benches     |

These three are **hard** because they interact with the rt#37 inline
cache: the hint byte is supposed to carry a monomorphic-shape cache
offset, and the `HexaFnProto.ic_slots` / `ic_shape` side arrays must
be populated by the emitter. A v0 path is to ignore the IC entirely
and do a linear field-name scan on a hexa-side "struct" value
(parallel arrays of field-name → value, same pattern as globals).
That keeps rt#37 decoupled.

### 2.11 Closures (0x34..0x35)

| Hex  | Name              | S   | E  | Diff | Deps / Notes                                                | Block            |
|------|-------------------|-----|----|------|-------------------------------------------------------------|------------------|
| 0x34 | CLOSURE           | TODO| N* | hard | Bx=proto idx; followed by n_upvals × NEW_CLOSURE_UPVAL      | closure benches  |
| 0x35 | NEW_CLOSURE_UPVAL | TODO| N* | hard | A=source (0=local / 1=outer upval), B16=slot idx             | closure benches  |

Needs: a hexa-side closure object (proto_idx + upvals array), plus
open/closed upval discipline. The VM currently has no upval concept
at all — the entire LOAD_UPVAL / STORE_UPVAL / CLOSURE /
NEW_CLOSURE_UPVAL family lands in one block. See Track B.

### 2.12 Exceptions (0x36..0x38)

| Hex  | Name   | S   | E  | Diff | Deps / Notes                                           | Block             |
|------|--------|-----|----|------|--------------------------------------------------------|-------------------|
| 0x36 | TRY    | TODO| N* | hard | sBx=handler offset; push handler PC onto handler stack | try/catch benches |
| 0x37 | THROW  | TODO| N* | hard | pop exn val; unwind frames to top handler; jump to it  | try/catch benches |
| 0x38 | ENDTRY | TODO| N* | hard | pop handler entry                                       | try/catch benches |

Requires a per-VM handler stack (parallel to `vm_frame_*`) recording
`(frame_depth, handler_pc, operand_stack_mark)`. Unwinding must
truncate both the frame stack and the operand stack back to the
saved marks, then push the exn value and jump.

### 2.13 Profiling / JIT (0x39)

| Hex  | Name         | S   | E  | Diff | Deps / Notes                                           | Block |
|------|--------------|-----|----|------|--------------------------------------------------------|-------|
| 0x39 | PROFILE_HOOK | TODO| N* | hard | Bx=profile slot; increment `hot_counts[slot]`, if >= threshold → trace-record hook | rt#39 |

Not blocking any current bench — deferred until rt#39 trace JIT
lands. MVP path: ignore it (treat as NOP) until the trace recorder
is wired.

### 2.14 Misc (0x3A..0x3E)

| Hex  | Name         | S  | E | Diff | Deps / Notes | Block |
|------|--------------|----|---|------|--------------|-------|
| 0x3A | NOP          | OK | Y |  -   |  -           |  -    |
| 0x3B | HALT         | OK | N | triv | sets `vm_halted`, exits loop | - |
| 0x3C | ASSERT_INT   | OK | N | triv | peek TOS, trap if `type_of != "int"` | - |
| 0x3D | ASSERT_FLOAT | OK | N | triv | peek TOS, trap if `type_of != "float"` | - |
| 0x3E | PRINT        | OK | Y |  -   |  -           |  -    |

---

## 3. Priority tracks

Three tracks, each intended as a **single session's work**, landing
in the order A → B → C. Each closes a distinct class of benches.

### Track A — rt#36-C leftover: fib + array + float

**Goal**: unblock `bench/fib_35.hexa`, `bench/array_sum.hexa`,
`bench/float_dot.hexa`, and all AND/OR-bearing conditionals, without
touching the closure / struct / exception worlds.

**Scope**:
1. **Emitter**: teach `bc_emit_binop` to lower to `ADDF/SUBF/MULF/DIVF`
   when both operand ExprTypes are float (`expr.ty == "float"`).
   Roughly a 10-line patch mirroring the existing int ladder.
2. **VM**: implement `AND/OR` as eager pop-pop-push (matches what
   emitter already emits — 6 lines).
3. **VM**: implement `NEW_ARRAY` / `ARR_GET` / `ARR_SET` / `ARR_LEN`
   using stage1 hexa's native array as the boxed value. Reuse
   `bc_vm_setat` for `ARR_SET`.
4. **VM**: implement `TAILCALL` as a frame-splice — pop caller frame
   *before* pushing callee frame, so deep recursion (fib 35+) does
   not blow the `vm_frame_*` arrays.
5. **Emitter**: emit `TAILCALL` when a `Return(Call(same-fn))`
   terminator is detected in a pure tail position (exists as a
   pattern in existing RETURN lowering — add the detection branch).
6. Smoke test: add `test_bc_vm_track_a.hexa` covering each.

**Estimated LOC**: VM **~90 lines**, emitter **~35 lines**, tests
**~80 lines**. Total **~205 LOC**. Time: **2-3 hours**.

**Benches unblocked**:

- `bench/fib_35.hexa`               (CALL + TAILCALL + int arith)
- `bench/array_sum.hexa`            (NEW_ARRAY + ARR_GET + ARR_LEN)
- `bench/float_dot.hexa`            (NEW_ARRAY + ARR_GET + ADDF/MULF)
- `bench/collatz_1000.hexa` (existing PASS, regression check)
- any `.hexa` source containing `&&` / `||` literals

**Checklist**:

- [ ] emitter: `bc_emit_binop` float lowering branch
- [ ] VM: `BC_OP_AND` / `BC_OP_OR` eager dispatch arms
- [ ] VM: `BC_OP_NEW_ARRAY` / `ARR_GET` / `ARR_SET` / `ARR_LEN` /
      `ARR_PUSH` arms
- [ ] VM: `BC_OP_TAILCALL` arm with frame-splice
- [ ] emitter: `bc_emit_return` tail-position detection
- [ ] tests: `self/test_bc_vm_track_a.hexa` (≥6 cases)
- [ ] smoke suite: 22 / 22 still PASS
- [ ] new bench: `bench/fib_35_vm.hexa` PASS

### Track B — closures

**Goal**: support hexa's first-class function literals (anonymous
`fn` expressions and captures from enclosing scope).

**Scope**:
1. **VM**: introduce a closure object `vm_closures = [{proto_idx,
   upvals_array}]` parallel-array style. Add two module-level arrays
   `vm_closure_proto_idx[]` and `vm_closure_upvals[]`.
2. **VM**: implement `LOAD_UPVAL` / `STORE_UPVAL` using
   `vm_frame_closure_idx[]` (parallel to `vm_frame_proto`), so each
   frame knows which closure is executing. Open vs closed upvals in
   v0 can collapse to always-closed (copy on capture), losing the
   ref-semantics of rebindable outer `let mut` — document that in
   a pitfall section.
3. **VM**: implement `CLOSURE` (read proto idx, start building
   closure upval array, expect next `n_upvals` instructions to be
   `NEW_CLOSURE_UPVAL`) and `NEW_CLOSURE_UPVAL` (append to the
   currently-being-built closure's upvals).
4. **Emitter**: emit `CLOSURE` + `NEW_CLOSURE_UPVAL` sequence for
   anonymous fn exprs. This requires the emitter to track captured
   vars during AST walk — a new pass, or an extension to the
   existing `bc_compile_fn` pass.
5. **Proto layout**: extend `bc_protos` entries with an upval count
   field (currently: `name`, `n_params`, `n_locals`).

**Estimated LOC**: VM **~120 lines**, emitter **~150 lines** (capture
analysis is the bulk), tests **~100 lines**. Total **~370 LOC**.
Time: **4-5 hours** (capture analysis is the risky part).

**Benches unblocked**:

- `bench/map_closure.hexa` (anonymous fn + captured upval)
- `bench/counter_closure.hexa` (STORE_UPVAL on `let mut` counter)
- `examples/hof_pipeline.hexa` (higher-order map/filter/reduce)

**Checklist**:

- [ ] proto layout: add `n_upvals` field
- [ ] VM: `vm_closures` parallel arrays + `vm_frame_closure_idx`
- [ ] VM: `BC_OP_CLOSURE` arm — allocate partial closure
- [ ] VM: `BC_OP_NEW_CLOSURE_UPVAL` arm — fill upval[i]
- [ ] VM: `BC_OP_LOAD_UPVAL` / `BC_OP_STORE_UPVAL` arms
- [ ] emitter: capture analysis — identify vars live across fn boundary
- [ ] emitter: close over captured vars → `CLOSURE` + N ×
      `NEW_CLOSURE_UPVAL`
- [ ] pitfall doc: open vs closed upval semantics (v0 = closed-only)
- [ ] tests: `self/test_bc_vm_track_b.hexa` (≥5 cases)

### Track C — exceptions + structs

**Goal**: support `try`/`catch`/`throw` and first-class structs with
field access — completes the "everyday" hexa surface area, leaving
only rt#37 IC and rt#39 JIT as deferred.

**Scope**:
1. **VM**: add parallel-array handler stack `vm_handler_frame[]`,
   `vm_handler_pc[]`, `vm_handler_sp_mark[]`, `vm_handler_locals_mark[]`.
2. **VM**: implement `TRY` (push handler entry with `pc + sBx`),
   `ENDTRY` (pop handler entry), `THROW` (pop exn, find top handler,
   unwind frame+stack+locals back to marks, jump to handler pc,
   push exn).
3. **VM**: implement `NEW_STRUCT` using a hexa-side struct value
   (parallel arrays of field name → value). Pop n_fields args off
   the stack; look up the type-const idx to get field names.
4. **VM**: implement `LOAD_FIELD` / `STORE_FIELD` as linear field-name
   scan on the boxed struct (ignoring the Layout B hint byte for
   now — rt#37 IC is a later concern).
5. **Emitter**: emit `NEW_STRUCT` on struct-literal nodes,
   `LOAD_FIELD` on `.field` access, `STORE_FIELD` on assignment.
6. **Emitter**: emit `TRY` / `ENDTRY` around `try { ... }` blocks
   and `THROW` on `throw exn`. Note: return-in-try is a known
   stage1 VM bug (see memory `b5_resolved_all_blockers_done`) —
   audit that the VM path preserves the fix.
7. **Const pool**: extend emitter to stash type descriptors (struct
   name + field names list) in a per-proto side table, since the
   current const pool is `string[]` only.

**Estimated LOC**: VM **~180 lines**, emitter **~200 lines**
(const-pool extension + all three emit sites), tests **~150 lines**.
Total **~530 LOC**. Time: **5-6 hours** (const-pool extension is the
intrusive part).

**Benches unblocked**:

- `bench/error_propagation.hexa`    (throw across 3 frames)
- `bench/struct_field_sum.hexa`     (NEW_STRUCT + LOAD_FIELD loop)
- `examples/linked_list.hexa`       (struct + field mutation)
- `bench/vec3_dot.hexa`             (struct with 3 float fields)

**Checklist**:

- [ ] const pool: side table `bc_proto_typeinfo[i]` with field lists
- [ ] VM: `vm_handler_*` parallel arrays + helpers
- [ ] VM: `BC_OP_TRY` / `BC_OP_THROW` / `BC_OP_ENDTRY` arms
- [ ] VM: `BC_OP_NEW_STRUCT` arm — allocate boxed struct
- [ ] VM: `BC_OP_LOAD_FIELD` / `BC_OP_STORE_FIELD` arms (no IC)
- [ ] emitter: struct-literal lowering → `NEW_STRUCT`
- [ ] emitter: `.field` / `.field = ...` lowering
- [ ] emitter: `try` / `catch` / `throw` lowering
- [ ] regression: return-in-try (memory `b5_resolved_all_blockers_done`)
      still returns correctly
- [ ] tests: `self/test_bc_vm_track_c.hexa` (≥8 cases)

---

## 4. Deferred beyond the tracks

Opcodes left un-tracked (explicitly postponed, not forgotten):

- `MOVE` / `DUP` / `POPN` — not emitted by any current emitter path
  and not on the stage1 source surface. Add when a pass that needs
  them is introduced (e.g. DUP for the `x ?? y` nil-coalesce when
  that lands). Trivial implementations when needed.
- `PROFILE_HOOK` — blocked on rt#39 (trace JIT). MVP stub = NOP.
- rt#37 **inline cache** side tables (`ic_slots` / `ic_shape` on
  `HexaFnProto`). Track C implements `LOAD_FIELD` / `STORE_FIELD` as
  linear-scan paths; the IC is a later optimization. The Layout B
  hint byte is ignored until rt#37-B lands.
- rt#38 **NaN-box HexaV**. All value types in the VM are currently
  hexa-dynamic (int / float / bool / string / `BC_NIL` sentinel);
  once rt#38 flips the runtime, the entire dispatch ladder will need
  to route through the HexaV decode macros. Out of scope for rt#36.

---

## 5. Cross-references

- Design doc: `docs/rt-36-bytecode-design.md` §6 (v1 ISA table)
- Header: `self/bytecode.h:94..190` (HexaOp enum)
- Emitter: `self/bc_emitter.hexa:54..131` (constants), `:1261..1323`
  (opname table), `:720..750` (BinOp lowering)
- VM: `self/bc_vm.hexa:420..749` (dispatch loop)
- VM tests (hand-rolled bytecode, covers ADDF/SUBF/MULF/DIVF):
  `self/test_bc_vm_float.hexa`

---

## 6. Status legend recap

- `OK`   — dispatch arm exists and a smoke test exercises it
- `TODO` — dispatch arm missing; VM will trap with
  `"unimplemented opcode: <name>"`
- `Y`    — emitter actively generates this opcode from user-level
  AST nodes
- `N`    — emitter does NOT currently generate it, but VM accepts
  hand-rolled bytecode
- `N*`   — emitter has the constant defined and the opname entry
  but no emit site anywhere
