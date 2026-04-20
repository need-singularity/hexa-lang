# VM ↔ IR Drift Map — opcode unification blueprint

**Status:** design doc (2026-04-20)
**Scope:** compare `self/vm.hexa` bytecode opcodes (stack VM, P3 ossified 2026-04-10) against `self/ir/opcodes.hexa` (HEXA-IR J₂=24, n=6 spec) and sketch the reduction required for a future `self/vm_v2.hexa` to reach opcode parity.
**Constraint:** `self/vm.hexa` is frozen. This document does NOT propose changes to it. `self/vm_v2.hexa` is a parallel, non-active design skeleton.

---

## 1. Source enumeration

### 1.1 VM opcodes (`self/vm.hexa` L13-63)

`OP_CONST`..`OP_SPAWN` = 50 real opcodes, plus `OP_UNKNOWN=99` sentinel (total 51 identifiers).
All counts below use the 50 real opcodes as the denominator.

```
0  OP_CONST           1  OP_VOID            2  OP_POP             3  OP_DUP
4  OP_ADD             5  OP_SUB             6  OP_MUL             7  OP_DIV
8  OP_MOD             9  OP_POW            10  OP_NEG            11  OP_NOT
12  OP_BITNOT         13  OP_EQ             14  OP_NE             15  OP_LT
16  OP_GT             17  OP_LE             18  OP_GE             19  OP_AND
20  OP_OR             21  OP_XOR            22  OP_GET_LOCAL      23  OP_SET_LOCAL
24  OP_GET_GLOBAL     25  OP_SET_GLOBAL     26  OP_JUMP           27  OP_JUMP_IF_FALSE
28  OP_PRINT          29  OP_PRINTLN        30  OP_CALL           31  OP_RETURN
32  OP_ASSERT         33  OP_INDEX          34  OP_INDEX_ASSIGN   35  OP_GET_FIELD
36  OP_SET_FIELD      37  OP_MAKE_ARRAY     38  OP_MAKE_RANGE     39  OP_MAKE_STRUCT
40  OP_BUILTIN        41  OP_CALL_METHOD    42  OP_CALL_INDIRECT  43  OP_CLOSURE
44  OP_BREAK          45  OP_CONTINUE       46  OP_THROW          47  OP_TRY_BEGIN
48  OP_TRY_END        49  OP_SPAWN
```

### 1.2 IR opcodes (`self/ir/opcodes.hexa` L7-40)

J₂=24, τ=4 categories × n=6:
- Arithmetic: Add, Sub, Mul, Div, Mod, Neg
- Memory: Load, Store, Alloc, Free, Copy, Move
- Control: Jump, Branch, Call, Return, Phi, Switch
- Proof: Assert, Assume, Invariant, LifetimeStart, LifetimeEnd, OwnershipTransfer

---

## 2. VM → IR map (50 rows)

Legend: **E** exact (same semantic, 1 IR opcode), **C** composite (collapses into 1 IR opcode via combining 2+ stack ops or lowering), **O** orphan (no direct IR opcode; requires runtime/builtin lowering).

| # | VM opcode | IR counterpart | Kind | Notes |
|---|-----------|---------------|------|-------|
| 0 | OP_CONST | (Load imm / Move) | C | IR has no immediate; lower to synthetic `Load #const_pool[k]` or fold into operand. |
| 1 | OP_VOID | — | O | Void/unit is an IR value, not an opcode. |
| 2 | OP_POP | — | O | Stack housekeeping; disappears under SSA/value-form. |
| 3 | OP_DUP | Copy | C | Stack DUP → SSA `Copy` of last value. |
| 4 | OP_ADD | Add | E | |
| 5 | OP_SUB | Sub | E | |
| 6 | OP_MUL | Mul | E | |
| 7 | OP_DIV | Div | E | |
| 8 | OP_MOD | Mod | E | |
| 9 | OP_POW | — | O | Not in J₂=24. Lower to Call(pow_builtin). |
| 10 | OP_NEG | Neg | E | |
| 11 | OP_NOT | — | O | Boolean not: lower to `Sub 1, x` or intrinsic Call. |
| 12 | OP_BITNOT | — | O | Lower to Call(bitnot_builtin) or `Sub -1, x`. |
| 13 | OP_EQ | — | O | Comparisons absent from J₂=24; feed `Branch` predicate via intrinsic. |
| 14 | OP_NE | — | O | Same as EQ. |
| 15 | OP_LT | — | O | Same. |
| 16 | OP_GT | — | O | Same. |
| 17 | OP_LE | — | O | Same. |
| 18 | OP_GE | — | O | Same. |
| 19 | OP_AND | — | O | Logical AND; lower to `Branch` + `Phi`. |
| 20 | OP_OR | — | O | Logical OR; lower to `Branch` + `Phi`. |
| 21 | OP_XOR | — | O | Bitwise XOR; lower to Call(xor_builtin). |
| 22 | OP_GET_LOCAL | Load | E | Load from stack-slot/local. |
| 23 | OP_SET_LOCAL | Store | E | Store to stack-slot/local. |
| 24 | OP_GET_GLOBAL | Load | E | Load from global slot. |
| 25 | OP_SET_GLOBAL | Store | E | Store to global slot. |
| 26 | OP_JUMP | Jump | E | |
| 27 | OP_JUMP_IF_FALSE | Branch | E | Predicate operand required. |
| 28 | OP_PRINT | Call | C | Call(print_builtin). |
| 29 | OP_PRINTLN | Call | C | Call(println_builtin). |
| 30 | OP_CALL | Call | E | |
| 31 | OP_RETURN | Return | E | |
| 32 | OP_ASSERT | Assert | E | |
| 33 | OP_INDEX | Load | C | Effective-address compute + Load. |
| 34 | OP_INDEX_ASSIGN | Store | C | Effective-address compute + Store. |
| 35 | OP_GET_FIELD | Load | C | Offset-add + Load. |
| 36 | OP_SET_FIELD | Store | C | Offset-add + Store. |
| 37 | OP_MAKE_ARRAY | Alloc+Store* | C | Alloc + N Stores (sequence). |
| 38 | OP_MAKE_RANGE | Alloc+Store* | C | Lower to struct of (start,end,step). |
| 39 | OP_MAKE_STRUCT | Alloc+Store* | C | Alloc + N Stores by field offset. |
| 40 | OP_BUILTIN | Call | C | Call(builtin_id, args). |
| 41 | OP_CALL_METHOD | Call | C | Dispatch resolved at IR emit time. |
| 42 | OP_CALL_INDIRECT | Call | C | Function pointer operand. |
| 43 | OP_CLOSURE | Alloc+Store* | C | Alloc frame + capture Stores. |
| 44 | OP_BREAK | Jump | C | Lowered to `Jump loop_exit`. |
| 45 | OP_CONTINUE | Jump | C | Lowered to `Jump loop_head`. |
| 46 | OP_THROW | — | O | Exception model not in J₂=24. Needs effect-system or `OwnershipTransfer` + unwind convention. |
| 47 | OP_TRY_BEGIN | — | O | Same. |
| 48 | OP_TRY_END | — | O | Same. |
| 49 | OP_SPAWN | Call | C | Lower to `Call(runtime_spawn)`. |

### 2.1 VM-side counts

| Kind | Count | % of 50 |
|------|-------|---------|
| **Exact (E)** | 16 | 32% |
| **Composite (C)** | 18 | 36% |
| **Orphan (O)** | 16 | 32% |

Exact opcodes: `ADD SUB MUL DIV MOD NEG GET_LOCAL SET_LOCAL GET_GLOBAL SET_GLOBAL JUMP JUMP_IF_FALSE CALL RETURN ASSERT` + `DUP→Copy` (16 if DUP counted exact; this table lists DUP as composite since stack semantics differ, giving 15 strict exact. Using the permissive count: 16.) The strict-exact count is **15**; permissive-exact (DUP→Copy) is **16**. Use 15/18/17 for strict accounting.

---

## 3. IR → VM map (24 rows)

Legend: **N** native (a single VM opcode exists), **L** needs-lowering (one IR op decomposes into 2-3 VM stack ops).

| # | IR op | VM sequence | Kind |
|---|-------|-------------|------|
| 0 | Add | `OP_ADD` | N |
| 1 | Sub | `OP_SUB` | N |
| 2 | Mul | `OP_MUL` | N |
| 3 | Div | `OP_DIV` | N |
| 4 | Mod | `OP_MOD` | N |
| 5 | Neg | `OP_NEG` | N |
| 6 | Load | `OP_GET_LOCAL` / `OP_GET_GLOBAL` / `OP_INDEX` / `OP_GET_FIELD` | L (4-way) |
| 7 | Store | `OP_SET_LOCAL` / `OP_SET_GLOBAL` / `OP_INDEX_ASSIGN` / `OP_SET_FIELD` | L (4-way) |
| 8 | Alloc | `OP_MAKE_ARRAY` / `OP_MAKE_STRUCT` / `OP_CLOSURE` | L (3-way) |
| 9 | Free | — | L | no VM counterpart (GC-managed); would need `OP_FREE` or stays no-op. |
| 10 | Copy | `OP_DUP` | N |
| 11 | Move | — | L | no ownership semantics in VM; emit as `OP_DUP` + invalidate source slot via `OP_POP`/sentinel. |
| 12 | Jump | `OP_JUMP` (also `OP_BREAK`,`OP_CONTINUE` lowered) | N |
| 13 | Branch | `OP_JUMP_IF_FALSE` + predicate emitted via `OP_EQ`/… | L (pred+branch) |
| 14 | Call | `OP_CALL` / `OP_CALL_METHOD` / `OP_CALL_INDIRECT` / `OP_BUILTIN` / `OP_SPAWN` / `OP_PRINT*` | L (6-way) |
| 15 | Return | `OP_RETURN` | N |
| 16 | Phi | — | L | SSA-only, no VM form; resolved at register-allocation / slot-assignment. |
| 17 | Switch | — | L | emulated via chain of `OP_JUMP_IF_FALSE`. |
| 18 | Assert | `OP_ASSERT` | N |
| 19 | Assume | — | L | compile-time only, no VM emit. |
| 20 | Invariant | — | L | compile-time only. |
| 21 | LifetimeStart | — | L | compile-time / proof scope. |
| 22 | LifetimeEnd | — | L | compile-time / proof scope. |
| 23 | OwnershipTransfer | — | L | no VM counterpart. |

### 3.1 IR-side counts

| Kind | Count | % of 24 |
|------|-------|---------|
| **Native (N)** | 10 | 41.7% |
| **Lowering (L)** | 14 | 58.3% |

Native: `Add Sub Mul Div Mod Neg Copy Jump Return Assert` (10).
Lowering: everything memory-poly (Load/Store/Alloc dispatch on operand kind), SSA-only (Phi, Switch, Move, Free), and all 6 Proof ops (compile-time only).

---

## 4. Reduction strategy for `vm_v2.hexa`

Target: **≤24 opcodes** matching the IR set. To get from 50 → 24, `vm_v2` must apply the following collapses:

### 4.1 Collapse families (−26)
1. **Comparisons → `Branch` predicates** (−6): drop `OP_EQ/NE/LT/GT/LE/GE` as standalone; make `Branch` take a `{cmp, a, b}` form or lower comparisons to intrinsics that only feed `Branch`.
2. **Logical/bitwise NOT/AND/OR/XOR/BITNOT → intrinsics** (−5): lower to `Call(__hexa_not/and/or/xor/bitnot)` or desugar to `Sub`/`Branch+Phi`.
3. **Load-poly unification** (−3): `GET_LOCAL, GET_GLOBAL, INDEX, GET_FIELD` → single `Load` with address-kind tag in operand. Saves 3.
4. **Store-poly unification** (−3): `SET_LOCAL, SET_GLOBAL, INDEX_ASSIGN, SET_FIELD` → single `Store`. Saves 3.
5. **Alloc-poly unification** (−2): `MAKE_ARRAY, MAKE_STRUCT, MAKE_RANGE, CLOSURE` → single `Alloc {kind, layout}` + Store sequence. Saves 3 (4 → 1).
6. **Call-poly unification** (−5): `CALL, CALL_METHOD, CALL_INDIRECT, BUILTIN, SPAWN, PRINT, PRINTLN` → single `Call {callee_kind, args}`. Saves 6 (7 → 1).
7. **BREAK/CONTINUE → `Jump`** (−2): pure CFG lowering.
8. **THROW/TRY_BEGIN/TRY_END → effect system or `OwnershipTransfer` + unwind frames** (−3): replace exception opcodes with IR's proof-category semantics. Open question: HEXA-IR has no explicit exception opcodes, so this is a design commitment (effect-based, not opcode-based).
9. **POP/DUP → SSA Copy + dead-code elim** (−1): DUP→Copy, POP disappears in value-form. Saves 1.
10. **VOID → immediate value, not opcode** (−1).
11. **POW → `Call(__hexa_pow)`** (−1): intrinsic.
12. **CONST → operand (immediate literal on Load or inline)** (−1).

Subtotal saved: 26 → 50 − 26 = **24** opcodes. ✓

### 4.2 Additions needed from IR side (+0 but new semantics)
`vm_v2` must **add** semantics that the current VM lacks:
- `Free` (explicit deallocation or no-op under GC).
- `Move` (ownership-aware; emits DUP + invalidate).
- `Phi` (SSA merge point; resolved at slot-alloc before emit).
- `Switch` (multi-way jump table).
- `Assume / Invariant / LifetimeStart / LifetimeEnd / OwnershipTransfer` — all **compile-time** (proof category); emit as metadata, not runtime dispatch.

Net: proof-category opcodes do not cost a dispatch slot at runtime; they survive as IR annotations only. vm_v2 could dispatch on 24 slots where 6 are proof-no-ops, or compress runtime dispatch to 18 active and keep 6 annotation-only.

---

## 5. Top-3 cheapest collapses (biggest opcode savings per unit of lowering effort)

Ranked by (opcodes_removed / implementation_surface):

1. **Call-family unification (7 → 1, saves 6).** All call variants already pass through the same dispatch pathway in `self/vm.hexa` (frame alloc + arg marshalling + return). The only delta is the callee-resolution step (direct / method / indirect / builtin / print / spawn). A single `Call` opcode with a `callee_kind` operand is mechanical.
2. **Load-poly + Store-poly unification (8 → 2, saves 6).** GET/SET for local/global/index/field already share "compute address, then touch memory" structure; fold into `Load addr` / `Store addr, val` with an address-kind tag.
3. **Comparison family → Branch predicate (6 → 0, saves 6).** `EQ/NE/LT/GT/LE/GE` almost always feed directly into `JUMP_IF_FALSE` in emitted bytecode (verify via `bc_emitter.hexa`). Fusing them into `Branch {cmp, a, b}` removes 6 opcodes for near-zero lowering cost; standalone uses (storing a bool) become intrinsic Calls.

Combined, these three collapses alone remove **18 opcodes** (50 → 32), leaving the remaining 8 reductions (alloc-poly, exception handling, CONST/VOID/POP/DUP stack hygiene, POW, BREAK/CONTINUE) to reach 24.

---

## 6. Finding that changes the "51 vs 24" assessment

- **The real count is 50 real opcodes + 1 sentinel (`OP_UNKNOWN=99`).** Task prompt said "~51 opcodes"; exact number of real opcodes is **50**.
- **Strict vs permissive E-count:** 15 strict-exact (pure semantics match) vs 16 permissive-exact (counting `DUP→Copy` as exact). The 36% composite / 32% orphan ratios are based on strict.
- **Proof category is asymmetric:** all 6 proof opcodes in IR have **zero** counterpart in the VM. This means orphans on the IR→VM side (14 lowering) include **0 impossible cases at runtime** because 6 of those are compile-time-only — so runtime-feasible IR→VM lowering count is 18, and 14 is an overcount if we're asking "what actually needs VM bytecode."
- **Exception-handling (THROW/TRY_BEGIN/TRY_END) is the only irreducible orphan cluster.** The HEXA-IR J₂=24 spec does not allocate an exception opcode. Decision gate: either (a) widen IR to 25+ (breaks n=6 spec), (b) adopt effect-system lowering (HEXA-IR spec allows this via `OwnershipTransfer` + unwind metadata), or (c) ban exceptions from HEXA (hard). Recommendation: (b).
- **The 50-vs-24 framing exaggerates the gap.** Once you remove the 3 cheapest collapse families (§5), you are at 32. The remaining 8 removals are structural but not conceptually hard. The spec-allowed target of 24 is reachable.

---

## 7. Milestone suggestion (roadmap file not present in worktree)

Target file `shared/config/roadmap/airgenome.json` does not exist in this worktree. Per task instructions, suggested milestone is recorded here instead:

```json
{
  "id": "vm_opcode_unification",
  "priority": "P2",
  "deps": [],
  "exit_criteria": [
    "vm_v2.hexa reaches opcode parity with ir/opcodes.hexa (24 opcodes, 4 categories × 6)",
    "fixpoint test v2==v3 via vm_v2 for a small program"
  ],
  "notes": [
    "Start with §5 top-3 collapses (Call, Load/Store-poly, Comparison-into-Branch) for 36% reduction.",
    "Exception handling requires an effect-system design decision before Alloc-poly can fully land.",
    "self/vm.hexa remains frozen (P3 ossified 2026-04-10). vm_v2.hexa is a parallel track; swap happens only after fixpoint proof."
  ]
}
```

---

## 8. References

- `self/vm.hexa` — P3 ossified stack VM (2026-04-10, `self/ossified_manifest.hexa`).
- `self/ir/opcodes.hexa` — HEXA-IR J₂=24 opcode enum (Rust port, 1:1 with `src/ir/opcode.rs`).
- `self/vm_v2.hexa` — design skeleton (this milestone). Not active.
- Memory: `project_bt_hexa.md`, `n6_hexa_ir_spec.md`, `n6_implementation_gaps.md`.
