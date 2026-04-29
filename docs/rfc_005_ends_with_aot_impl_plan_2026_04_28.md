# RFC-005 Implementation Plan — ends_with()/starts_with() & slice-equality AOT codegen fix

- **Date**: 2026-04-28
- **Status**: plan (impl deferred — codegen change → raw#18 self-host fixpoint risk)
- **Source RFC**: `proposals/rfc_005_ends_with_aot_codegen.md` (commit `2cb2b95c`)
- **Author scope**: plan-only agent (per parent task) — does not modify codegen
- **Constraints**: raw#9 hexa-only · raw#10 honest C3 · raw#18 self-host fixpoint · raw#159 hexa-lang upstream-proposal-mandate · raw#71 falsifier
- **Empirical seed**: anima-eeg argv parsers (5 files), all already migrated to method-form

## 1. Codegen Analysis — Verified Root Cause

### 1.1 Slice expression lowering (`self/codegen_c2.hexa:4280-4293`)

```hexa
// ── Slice expr: arr[s:e], arr[:e], arr[s:], arr[:] ─────
if k == "Slice" {
    let arr_c = gen2_expr(node.left)
    let mut s_c = "hexa_int(0)"
    if type_of(node.right) != "string" { s_c = gen2_expr(node.right) }
    let mut e_c = "hexa_int(hexa_len(" + arr_c + "))"
    if type_of(node.cond) != "string" { e_c = gen2_expr(node.cond) }
    return "hexa_array_slice(" + arr_c + ", " + s_c + ", " + e_c + ")"
}
```

**Type-blind dispatch.** Every `Slice` node lowers to `hexa_array_slice(...)`
regardless of operand type. There is no operand-type test (no `is_string_expr(node.left)`
helper, no fallthrough to `hexa_str_slice`).

### 1.2 Runtime — `hexa_array_slice` on a string (`self/runtime.c:5502`)

```c
HexaVal hexa_array_slice(HexaVal arr, HexaVal start, HexaVal end) {
    if (!HX_IS_ARRAY(arr)) return hexa_array_new();   // ← string falls through here
    ...
}
```

A string operand is NOT an array → early-return `hexa_array_new()` (empty array,
TAG_ARRAY).

### 1.3 Equality dispatch (`self/codegen_c2.hexa:3008`)

```hexa
if op == "==" { return "hexa_eq(" + l + ", " + r + ")" }
```

`hexa_eq` (runtime.c:3709) requires `HX_TAG(a) == HX_TAG(b)` else returns `hexa_bool(0)`.

### 1.4 Failure trace — `s[(n-5)..n] == ".hexa"` under AOT

1. `gen2_expr(s[(n-5)..n])` → `hexa_array_slice(s, n-5, n)`
2. At runtime: `HX_IS_ARRAY(s)` is false → returns `hexa_array_new()` (TAG_ARRAY, len=0)
3. RHS `".hexa"` is TAG_STR
4. `hexa_eq(empty_array, str)`: TAG mismatch → `hexa_bool(0)` → silent false

**This matches the symptom exactly.** Interpreter path takes a different route
(it inspects operand type via `Val.tag` at runtime) and dispatches to byte-slice +
byte-eq, hence interpreter=true / AOT=false divergence.

### 1.5 Method form `s.ends_with(".hexa")` — why it works

`self/codegen_c2.hexa:2501-2502, 4005-4006` lower the method to
`rt_str_ends_with(s, suffix)` directly — no slice intermediate, no TAG_ARRAY/TAG_STR
mismatch. Same applies to `starts_with`.

### 1.6 `Index` (single-byte) parity (`self/codegen_c2.hexa:4184` + `runtime.c:1928`)

`hexa_index_get` is **polymorphic**: it has an explicit `HX_IS_STR(container)` branch
that dispatches to `hexa_str_char_at`. **Slice has no such polymorphic runtime.**
A polymorphic `hexa_slice` was never written.

**Conclusion**: bug is two-layered — (a) codegen always emits `hexa_array_slice`,
(b) runtime has no string-aware `hexa_slice` shim. Either layer could fix it.

## 2. Three Fix Paths — Trade-off Matrix

| Aspect | Path A: codegen + runtime | Path B: parser rewrite | Path C: lint warning |
|---|---|---|---|
| **Where change lives** | `self/codegen_c2.hexa:4282` + `self/runtime.c` (new `hexa_slice`) | `self/parser.hexa` + new normalize pass | `self/linter.hexa` |
| **LoC** | ~30 (codegen) + ~20 (runtime polymorphic shim) + ~30 (regression test) = **~80** | ~50 (AST pattern match `BinOp(==, Slice(s,a,b), StringLit)` → method) + ~30 test = **~80** | ~25 (rule) + ~15 docs = **~40** |
| **Coverage** | Full — every `s[a..b] == t` works in AOT identically to interp | Partial — only literal RHS; `s[a..b] == other_var` still fails | Zero runtime fix; coverage = whatever user grep / lint reads |
| **raw#18 fixpoint impact** | **HIGH** — codegen byte-output changes for any program using slice; v3→v4→v3 cycle requires byte-identical re-attainment proof | **MEDIUM** — AST shape change for matched patterns; v3 build depends on whether self-host emits this pattern (grep: 0 occurrences in `self/`) | **NONE** — linter is opt-in; codegen unchanged |
| **raw#71 falsifier** | Direct hit: 1k-fuzzer corpus interp/AOT parity | Partial — fuzzer only on literal-RHS subset; non-literal RHS still falsifies | Doesn't satisfy falsifier (silent-wrong still ships) |
| **raw#10 honesty** | Honest fix | Honest within scope; must document non-literal-RHS gap | Mitigation only; landmine remains |
| **Implementation risk** | Medium-high — `hexa_slice` polymorphic shim needs str/array discrimination; slice on `argv()[0]` (TAG_STR via str_concat result) edge cases | Medium — parser pass ordering with type-inference unknown; parser change usually triggers AST consumer churn | Low — additive, isolated |
| **Breaks existing AOT?** | Should not — array-slice semantics preserved when operand is array | Should not — rewrite preserves semantics for matched pattern | Cannot break anything (warning only) |
| **Time-to-land** | ~6-10h (RFC estimate) + fixpoint re-cert (~1-2 cycles) | ~4-6h + fixpoint re-cert | ~1-2h, no fixpoint impact |
| **Empirical satisfaction** | Eliminates entire bug class | Eliminates anima-eeg-style usages | Surfaces them; user still patches by hand |

## 3. Recommendation — Hybrid (C now, A as separate cycle)

**Immediate (this cycle, no codegen risk)**
- **Path C** lint rule in `self/linter.hexa`:
  - Pattern: `BinOp(op="==", left=Slice(...), right=*)` → warning `slice-eq-aot-unsafe`
  - Suggestion: `prefer s.ends_with(t) / s.starts_with(t) / s.substring(a,b) == t under AOT`
  - Reason text cites RFC-005 + `project_aot_push_gotcha.md` family of "free-function vs method-form AOT divergence"

**Follow-up cycle (codegen change, requires fixpoint re-cert)**
- **Path A** is the only path that fully retires the falsifier:
  1. Add `hexa_slice(HexaVal v, HexaVal a, HexaVal b)` polymorphic shim in `runtime.c`
     (str → `hexa_str_slice`; else → `hexa_array_slice`)
  2. Change `codegen_c2.hexa:4292` to emit `hexa_slice(...)` instead of `hexa_array_slice(...)`
  3. Run raw#18 fixpoint: v3 build → emit v4 → emit v3' → compare bytes (`tool/fixpoint_compare.hexa`)
  4. If v3 != v3' (expected, since `hexa_array_slice` strings change to `hexa_slice` strings),
     iterate v3'→v4'→v3'' until banach-fixpoint (per `.roadmap` SH-Ω convention)
- **Path B is rejected** (partial coverage, no clear advantage over A; parser pass ordering risk)

**Why C-first is safe** — linter is non-load-bearing; it never participates in the
self-host build, so v3 byte identity is preserved trivially.

**Why A is not immediate** — even though +30 LoC codegen looks small, the byte-output
of every slice-using `.hexa` file changes, which mechanically invalidates the cached
v3 build artifact and forces a fresh fixpoint iteration. The user's `.roadmap` SH-Ω
machinery is sensitive to this; the parent task explicitly orders plan-only.

## 4. raw#18 Self-Host Fixpoint Impact — Per Path

| Path | v3 codegen byte change | runtime.c byte change | fixpoint re-cert needed |
|---|---|---|---|
| A | YES (every `s[a..b]` site) | YES (new `hexa_slice` symbol) | YES — full v3→v4→v3' iteration |
| B | YES (AST emit for matched patterns) | NO | YES — full v3→v4→v3' iteration |
| C | NO | NO | NO — linter is build-orthogonal |

Cross-reference: `.roadmap:104-110, 556-815` document existing fixpoint-iteration
slots and the byte-identity contract. Both A and B must occupy a new fixpoint slot
or piggy-back on an open one.

## 5. Test Cases (regression — Path A or B)

`test/regression/aot_string_slice_eq.hexa` — must pass under both `hexa run` and AOT:

```hexa
fn main() {
    // Suffix
    let s1 = "hello.hexa"
    if s1[5..10] != ".hexa" { eprintln("FAIL suffix"); exit(1) }

    // Prefix
    let s2 = "ABCDEF"
    if s2[0..3] != "ABC" { eprintln("FAIL prefix"); exit(1) }

    // Empty slice
    let s3 = "x"
    if s3[0..0] != "" { eprintln("FAIL empty"); exit(1) }

    // Full slice
    if s3[0..1] != "x" { eprintln("FAIL full"); exit(1) }

    // Off-by-one (slice past end is clamped)
    let s4 = "ab"
    if s4[0..5] != "ab" { eprintln("FAIL clamp"); exit(1) }

    // Variable RHS (defeats Path B)
    let needle = ".hexa"
    let argv0 = "/x/y/z.hexa"
    let nn = argv0.len()
    if argv0[(nn-5)..nn] != needle { eprintln("FAIL var-rhs"); exit(1) }

    // Method-form parity baseline
    if !argv0.ends_with(".hexa") { eprintln("FAIL method"); exit(1) }
    if !argv0.starts_with("/x") { eprintln("FAIL method2"); exit(1) }

    println("PASS")
}
```

Falsifier driver: 1k random-fuzz corpus generator
(`tool/fuzz_slice_eq.hexa`, ~40 LoC) compares interp vs AOT booleans. Single
divergence = anti-falsifier. (raw#71)

## 6. Empirical Fail-Case Inventory — anima-eeg

`grep` audit of `/Users/ghost/core/anima/anima-eeg/*.hexa` (2026-04-28):

| File | slice-eq usage | method-form patch | status |
|---|---|---|---|
| `calibrate.hexa` | (was) `argv0[(n-5)..n] == ".hexa"` | line 426 cite + line 430-432 method | **patched** |
| `experiment.hexa` | (was) same pattern | line 503 cite + line 507-509 method | **patched** |
| `eeg_setup.hexa` | — | line 102-104 method | **patched** |
| `board_health_check.hexa` | — | line 392-394 method | **patched** |
| `electrode_adjustment_helper.hexa` | — | line 887-889 method | **patched** |
| `full_helmet_view.hexa` | — | line 571-574 method | **patched** |
| `eeg_brainflow_sanity.hexa`, `eeg_ftdi_latency_fix.hexa`, `collect.hexa`, `experiment.hexa` | (only `starts_with(prefix)` for line parsing) | uses method | clean |

**Conclusion**: anima-eeg is fully migrated. Lint rule (Path C) immediately catches
any future regression in this repo and any new caller (raw#9 hexa-only constraint
makes this the only language path forward).

Wider audit recommended (separate cycle): `grep -rn "\[.*\.\..*\] ==" $WS/**/*.hexa`
across `airgenome`, `hexa-lang/self`, `nexus` to find other latent silent-wrong
sites.

## 7. Plan-Only Limits (raw#10 / raw#91 honest C3)

1. **Plan agent did not modify codegen.** Path C lint rule is implementable here
   in principle (no codegen impact) but parent task explicitly says plan-only —
   defer to the next cycle owner.
2. **Path A LoC estimate (~80) assumes no parser-side type info** — if AST
   carries a string-type annotation we could keep `hexa_array_slice` for arrays
   and emit `hexa_str_slice` directly when known. Cursory search did not find
   such annotation; assumption is "no static type info, polymorphic runtime
   shim required".
3. **Fixpoint cost is not free.** Even +30 LoC codegen forces a fresh v3→v4→v3'
   iteration (potentially 1-2 build cycles). Cannot be done incrementally without
   breaking SH-Ω banach-fixpoint contract; parent should budget for this.
4. **Path C does not satisfy RFC-005 falsifier.** Lint surfaces the bug; it does
   not retire the silent-wrong class. RFC-005 cannot move from `proposed` to
   `done` until Path A lands.
5. **Interpreter side not analyzed** — this plan assumes interpreter
   slice/equality is correct (RFC-005 problem statement); a defensive verification
   pass on `hexa_full.hexa` slice-eq path is recommended before Path A lands, to
   ensure the AOT fix matches the actually-correct interpreter behavior.

## 8. Suggested Cycle Sequencing

1. **Now (no codegen)**: Path C lint rule + commit + RFC-005 status updated
   to `mitigation-shipped, root-fix-deferred`
2. **Next cycle (codegen)**: Path A — `hexa_slice` polymorphic runtime shim +
   codegen swap + regression test + fuzzer + raw#18 fixpoint re-cert
3. **After A lands**: RFC-005 status → `done`; lint rule downgraded from
   warn-by-default to opt-in (still useful for older AOT toolchains)

## 9. References

- RFC-005: `proposals/rfc_005_ends_with_aot_codegen.md`
- Codegen Slice site: `self/codegen_c2.hexa:4280-4293`
- Codegen `==` site: `self/codegen_c2.hexa:3008`
- Codegen ends_with/starts_with method sites: `self/codegen_c2.hexa:2498-2502, 3729-3735, 4002-4006`
- Runtime str_slice: `self/runtime.c:5491-5500`
- Runtime array_slice: `self/runtime.c:5502-5512`
- Runtime hexa_eq: `self/runtime.c:3709-3757`
- Runtime polymorphic index_get (template for `hexa_slice`): `self/runtime.c:1906-1932`
- Linter scaffold: `self/linter.hexa:304-420`
- Self-host fixpoint contract: `.roadmap:104-110, 556-815`
- Empirical seed: `/Users/ghost/core/anima/anima-eeg/{calibrate,experiment,eeg_setup,board_health_check,electrode_adjustment_helper,full_helmet_view}.hexa`
