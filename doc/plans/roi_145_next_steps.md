# ROI #145 — P7-6 `hexa_full.hexa` native scale-up — next steps

Session date: 2026-04-19
Scope landed this run: **step 3f — payload-free `EnumPath` lowering**

## What landed (step 3f)

- **`self/ir/instr.hexa`** — `IrModule` extended with parallel enum
  registry: `enum_names: array<string>` + `enum_variants: array<array<string>>`.
  New helpers: `module_add_enum` (idempotent upsert) +
  `module_find_variant_idx` (positional tag lookup, -1 on miss).
- **`tool/native_build.hexa`** —
  - Pass 1 walker handles `EnumDecl` nodes (alongside `StructDecl` /
    `ImplBlock`), collecting `node.variants[i].name` into
    `module_add_enum`.
  - `nb_lower_expr` grows an `EnumPath` branch that emits
    `bld_const_i64(tag)` where `tag = module_find_variant_idx(...)`.
    Returns `-1` for unregistered enums so the match-arm fallback
    (existing "< 0 → wildcard" path) stays intact.
  - Match-expr comment updated: payload-free EnumPath patterns now
    resolve through the shared `nb_lower_expr` path + `cmp_eq`.
- **`self/test_enum_path.hexa`** — 26 asserts. Covers:
  (T1) basic tag lookup, (T2) miss → -1, (T3) multi-enum
  independence, (T4) idempotent re-register, (T5) Pass 1 AST-walk
  simulation, (T6) EnumPath shape invariants, (T7) match-arm
  equality dispatch parity. `ALL PASS — 26/26` under
  `build/hexa_stage0.real`.

## What this unlocks

- Every `match x { Color::Red => …, Color::Green => …, _ => … }`
  form compiled through `tool/native_build.hexa` now dispatches
  correctly (tag-integer compare), where previously the patterns
  returned `-1` and collapsed to wildcard.
- `hexa_full.hexa` uses bare `EnumKind::Variant` values as integer
  tags in multiple places (`CmpKind`, token tags). Those expressions
  now lower to stable IR constants rather than failing back to the
  C-era symbol-literal path.

## What's still gated (next sessions)

1. **Enum-variant field registry** — payload-carrying patterns
   (`Result::Ok(x)`, `Expr::BinOp(a, op, b)`) still cannot
   destructure. Requires a per-variant field-type table
   (`enum_variant_field_types: array<array<array<string>>>` or a
   flatter `(enum, variant) → [types]` map) plus an arm-scope env
   rebind for each destructured sub-ident. Estimate: ~3h.
2. **Guard clauses on match arms** — `Color::Red if expr` is still
   a nb_lower_expr wildcard fall-through. Needs an extra branch pair
   per guarded arm. Estimate: ~1h.
3. **`if let Pattern = expr { … }` lowering** — parser already emits
   it; `nb_lower_expr` has no case. One branch, sugar for
   single-arm match. Estimate: ~2h.
4. **String literal emission audit** — `cg_string_sym("str_concat")`
   routing after 3b99e34c needs a round-trip test (lower `"a" + "b"`
   through native_build, verify the emitted IR references the correct
   rt vs legacy symbol per `cg_rt_target()`). Estimate: ~2h.
5. **Closure / lambda lowering** — `Lambda` kind in `nb_lower_expr`.
   Required for `.map(|x| x + 1)` style expressions common in
   `hexa_full.hexa`. Biggest remaining frontend gap. Estimate: ~8h.
6. **Tuple destructure in `let`** — `let (a, b) = pair`. Parser
   emits `Tuple` / `LetStmt{name="Tuple"}` — no lowering case.
   Estimate: ~2h.
7. **P7-6 end-to-end scale test** — once 1+5 land, attempt
   `hexa tool/native_build.hexa self/hexa_full.hexa -o build/hexa_new`
   and diff the emitted IR kind distribution vs `codegen_c2` golden
   to identify the next coverage gap. Will surface whatever's left
   after the above list.

## Hardest remaining piece

**Closure / lambda lowering (#5)** — it requires environment capture,
indirect call emission, and a heap-allocated closure record ABI —
all of which the current native_build assumes away. Everything else
on the list is a straight pattern-match extension.

## Commit trail

- `feat(ir/instr): module enum registry (add_enum / find_variant_idx)` — part of this step
- `feat(native_build): P7-6 EnumPath payload-free lowering (ROI #145 step 3f)` — this run

Rolled together into a single commit for bisect clarity.
