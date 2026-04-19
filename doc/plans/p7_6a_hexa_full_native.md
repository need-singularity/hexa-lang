# ROI #162 — P7-6a: hexa_full Native Compilation Gap Analysis

**Target:** Compile `self/hexa_full.hexa` (17,273 LOC, 80+ AST node kinds) through the
native ARM64 backend **without clang**, closing the stage-up from the P7-1~P7-5
encoders/regalloc/peephole to end-to-end program compilation (HX10 milestone).

**Status (2026-04-19):** analysis + 3 seed patches landed. Production path unblocked
for boolean/bit-op/comparison-bearing programs; structural AST→IR driver remains
the next-largest chunk.

## Current pipeline

```
.hexa source
  ↓ hexa_full.hexa parser (80+ AST node kinds)
AST tree
  ↓ self/ir/lowering.hexa   ← GAP: leaf-only, no recursive walk
HEXA-IR (24 opcodes + extensions)
  ↓ self/codegen/mod.hexa emit_function_arm64   ← GAP: missing 5 bitops
ARM64 bytes
  ↓ self/codegen/peephole.hexa + macho.hexa
Mach-O executable
```

Adjacent POC already verified (`codegen_native.hexa` → `codegen_native_fib.hexa`
→ `test_enum_path.hexa`): hello-world / multi-fn add / IR lowering 43 ops.

## Gap matrix (AST kind × backend state)

Kinds listed in **hexa_full.hexa constructor frequency order**.

| # | AST kind           | Freq | Parser→AST | lower_expr (IR) | codegen (ARM64) | Verdict |
|---|--------------------|------|:----------:|:---------------:|:---------------:|---------|
| 1 | Ident              | 49   | OK         | OK              | OK (mov/load)   | complete |
| 2 | FnDecl             | 20   | OK         | skeleton only¹  | OK              | stmt-driver gap |
| 3 | StringLit          | 19   | OK         | OK (intern)     | partial (const table) | ok |
| 4 | If                 | 17   | OK         | **missing²**    | OK (branch)     | **GAP-1** |
| 5 | Array / ArrayLit   | 17   | OK         | helper-only     | partial         | stmt-driver gap |
| 6 | Index              | 13   | OK         | OK              | OK              | complete |
| 7 | For / ForStmt      | 13   | OK         | skeleton only¹  | OK              | stmt-driver gap |
| 8 | Field              | 13   | OK         | OK (struct_idx) | OK              | complete |
| 9 | Lambda             | 10   | OK         | **missing²**    | —               | P7-6b |
|10 | IntLit             | 10   | OK         | OK              | OK (MOVZ+MOVK)  | complete |
|11 | Call               | 10   | OK         | OK              | OK (BL fixup)   | complete |
|12 | Return / ReturnStmt| 12   | OK         | OK              | OK              | complete |
|13 | Tuple              | 8    | OK         | **missing**     | —               | P7-6b |
|14 | Range              | 8    | OK         | handled via for | partial         | for-only |
|15 | BoolLit            | 8    | OK         | OK              | OK              | complete |
|16 | TryCatch           | 7    | OK         | **missing**     | —               | P7-6c (effect) |
|17 | Spread             | 7    | OK         | **missing**     | —               | P7-6b |
|18 | Let / LetStmt / LetMutStmt | 11 | OK | helper-only   | OK              | stmt-driver gap |
|19 | FloatLit           | 7    | OK         | placeholder     | scalar-only     | FP track |
|20 | UnaryOp            | 6    | OK         | OK (neg/not/~)  | OK (neg)        | complete |
|21 | Slice              | 6    | OK         | **missing**     | —               | P7-6b |
|22 | EnumDecl / EnumPath | 11  | OK         | partial         | —               | P7-6b |
|23 | Block              | 6    | OK         | **missing²**    | linear-only     | **GAP-1** |
|24 | While / WhileStmt  | 6    | OK         | skeleton only¹  | OK              | stmt-driver gap |
|25 | StructDecl/Init    | 7    | OK         | partial         | OK              | partial |
|26 | Own/Move/Borrow    | 15   | OK         | **missing**     | move=copy stub  | ownership track |
|27 | Match / MatchExpr  | 4    | OK         | skeleton only¹  | OK (switch)     | stmt-driver gap |
|28 | Assign / AssignStmt| 2    | OK         | OK              | OK (store)      | complete |
|29 | BinOp **arith** (add/sub/mul/div/rem/mod)     | 13 total | OK | OK | OK | complete |
|29a| BinOp **cmp** (eq/ne/lt/gt/le/ge)             |   (13) | OK | **missing²** (cmp_from_vids exists but unreachable) | OK via ira_enc_* | **GAP-2** |
|29b| BinOp **bitwise/shift** (bitand/bitor/bitxor/shl/shr) | (13) | OK | **missing** | partial (ir_to_arm64 OK, mod.hexa missing) | **GAP-3** |
|29c| BinOp **logical** (and/or/xor)                |   (13) | OK | synthesized (mul/sum-prod) | OK | complete (suboptimal) |
|30 | Spawn / Async / Await / Yield / Resume | 11 | OK | **missing** | — | async track |
|31 | Proof / Assert / Assume / Verify / Invariant | 16 | OK | no-op | OK (runtime check) | ok |
|32 | Comptime* (5 kinds)| 5    | OK         | eval then drop  | —               | compile-time |
|33 | Import / Mod / Use | 3    | resolved via flatten | —         | —               | pre-IR |
|34 | Macro / Derive / Generate | 4 | expanded pre-IR | —          | —               | pre-IR |
|35 | Handle / Effect / Select / Scope / Drop / Defer | 7 | OK | **missing** | — | P7-6c |
|36 | IfExpr / IfLetExpr / WalrusExpr | 3 | OK | skeleton (IfExpr) | OK | stmt-driver gap |
|37 | MapLit / MapEntry  | 2    | OK         | **missing**     | —               | P7-6b |
|38 | ListComp           | 1    | OK         | **missing**     | —               | P7-6b |
|39 | Syscall / ExternFn | 5    | OK         | OK              | OK              | complete |

¹ "skeleton only" = IR-level helper exists (`lower_if_skeleton`,
`lower_while_skeleton`, `lower_for_range_skeleton`, `lower_match_skeleton`,
`lower_fn_decl_skeleton`), but no driver calls them from a walked AST tree.

² Directly testable in a 200-LOC harness: a comparison, a block, an if/while body.

## Three highest-leverage gaps (selected for seed patches)

### **GAP-1 — lower_expr leaf-only; no recursive AST walker**
`lower_expr(m, b, e, env)` dispatches on `e.kind` but for `If`, `Match`, `Block`,
`Array`, `IfExpr`, `Lambda`, `Tuple`, `MapLit`, `ListComp`, `StructInit`, `Range`
it returns `-1`. The helpers it would call (`lower_if_skeleton`, etc.) require
a driver that (a) splits AST statement children, (b) feeds each through
`lower_expr`/`lower_stmt`, (c) threads `env`, (d) closes blocks with
`lower_if_close` / `lower_while_close`.

**Impact:** nearly every non-trivial program (including hexa_full itself)
produces `-1` at the first `if`/`while`/`block`, so no bytes emit.

**Minimum seed (this PR):** add block-level dispatch that delegates to existing
skeletons — see `self/ir/lowering.hexa` additions.

### **GAP-2 — BinOp comparisons return -1**
`lower_binop_mnemonic(op_str)` handles only `add/sub/mul/div/rem/mod`. Every
parsed `a < b`, `a == b`, `a >= b` reaches `lower_binop_from_vids` → mnemonic
fails → `-1`. The fix is 6 lines: route `eq/ne/lt/le/gt/ge` to
`lower_cmp_from_vids` (already present, well-tested).

**Impact:** all of `if x < y`, `while i < n`, every `assert cond` gate.

### **GAP-3 — BinOp bitwise / shift missing in lower_binop + mod.hexa dispatch**
`lower_binop_mnemonic` also misses `bitand/bitor/bitxor/shl/shr`. `ir_to_arm64`
already implements them (`band/bor/bxor/shl/shr` opcodes) but
`self/codegen/mod.hexa emit_function_arm64` dispatch switch does NOT list them
— so even if IR contained them, bytes wouldn't emit. Two-file fix.

**Impact:** bit-exact math (hash, crc32, parse_pure hex), SIMD emul, serdes.

## Roadmap (sequenced)

| Phase | Scope | Est. LOC | Unlocks |
|-------|-------|---------:|---------|
| **P7-6a.1 (this PR)** | GAP-1/2/3 seeds + tests | +150 | bitwise + cmp + if-block driver |
| P7-6a.2 | AST stmt walker (Let/Assign/If/While/For/Return/Block) | +400 | ~60% of hexa_full |
| P7-6a.3 | FnDecl/StructDecl/EnumDecl top-level driver | +250 | multi-fn programs |
| P7-6a.4 | Array/Tuple/StructInit/Range/MapLit | +300 | data literals |
| P7-6a.5 | Match/MatchExpr/IfExpr phi + Break/Continue wiring | +200 | control-flow |
| P7-6a.6 | Lambda + closure capture lowering | +350 | higher-order |
| P7-6a.7 | Effects/Handle/Try/Spawn (minimal) | +400 | async subset |
| P7-6a.8 | End-to-end: `hexa_full.hexa → hexa_stage0.native` fixpoint | — | P7-6 complete |

## Acceptance after P7-6a.1

- `self/test_cmp.hexa`: a<b / a==b / !a return correct bool via IR lowering.
- `self/test_bitops.hexa`: a&b / a|b / a^b / a<<n / a>>n round-trip.
- `self/test_ifblock.hexa`: `if c { 7 } else { 3 }` lowers to 3 BBs, emits
  ARM64 branch + phi, returns 7 or 3.

## References

- `self/codegen_native.hexa` — mini-AST POC (pure emitter, 15 kinds)
- `self/codegen/ir_to_arm64.hexa` — full ARM64 IR lowering (bitops present)
- `self/codegen/mod.hexa` — production backend (bitops MISSING in dispatch)
- `self/ir/lowering.hexa` — AST → IR (leaf-only at current head)
- `self/ir/builder.hexa` — SSA builder (bld_band/bor/bxor/shl/shr present)
- MEMORY: `project_phase7_ir43_charpure40.md`, `project_self_hosting_track.md`
