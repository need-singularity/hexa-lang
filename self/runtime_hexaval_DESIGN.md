# HexaVal tagged-value runtime — RT-P3-1 design note

Date: 2026-04-14
Scope: design-only. No codegen/runtime edits in this change.
Paired sketch: `self/runtime_hexaval_sketch.c` (56 lines).

## Problem

`self/build_c.hexa` collapses every Hexa type (Long, Float, Str, DynArr, StrArr)
to C `long`. Generated `build/artifacts/hexa_native.c` stores char* literals in
`long` variables, passes them to `hexa_arr_push(long, long)`, and compares
`long == "if"`. Under default gcc flags this produces **1377 errors** (measured
2026-04-14, `-ferror-limit=0`, no permissive flags).

## Error categories targeted

Measured on current generated C (`-ferror-limit=0`):

| Category | Hits | Root cause |
|----------|------|------------|
| `long <- const char*` | **1265** | string literal stored/passed as `long` |
| `const char* <- long` | **96** | `long` var used where string expected |
| `char[N] <- long` | **16** | short literals (e.g. `"\n"`) to `long` param |

Total **1377 -Wint-conversion**; secondary **222 -Wpointer-integer-compare**
(`word == "if"`). Union eliminates categories 1+2+3 by tagging strings in
`.u.s`; `hval_streq` subsumes the comparison class.

**Estimated impact (informed guess, not measured):**
- Full codegen retrofit (Option A): **1377 → <50 (-99%)**. Residual from
  format strings, arity drift, and legitimate `long` sites (indices, lengths).
- Shim-only (Option B): **1377 → ~400 (-70%)** — strings still transit `long`.

## Migration path — two options

### Option A: tag-through-codegen (preferred)

Modify `self/build_c.hexa::c_type_of` (lines 1128–1136, 3253–3255, 3412–3416)
to emit `HexaVal` for Long/Float/Str/DynArr slots and mint constructors at
literal sites (`"foo"` → `hval_from_str("foo")`). Every user-function signature
becomes `HexaVal fn(HexaVal ...)`. Cost: ~10 codegen paths touched, affects
every `.hexa → .c` regeneration; forces baseline binary rebuild (see
hexalang_vb1_fix_2026-04-14.json `do_not_actions`). Breaks binary compatibility
with existing `build/artifacts/hexa_native` — stage with `_hexaval` suffix in
build dir, do not overwrite.

### Option B: shim-at-runtime (fallback)

Keep `long`-emitting codegen. Add `hexa_arr_push_v`, `hval_streq` wrappers
that accept `long` + metadata byte. Works as bandaid but does not eliminate
-Wint-conversion; relies on permissive flags indefinitely. Reject unless
Option A proves unschedulable.

## Known pitfalls

- **Strict-aliasing**: union-member access is safe (C99 6.5.2.3), but
  `(int64_t*)&v.u.f` punning is UB. All access MUST go through named accessors.
- **Pointer provenance**: `.u.s` must outlive the `HexaVal`. Literals are
  fine; strdup'd buffers need ownership. Add `HV_STR_OWNED` if rt#37 GC lands.
- **Type collision with `self/runtime.c::HexaVal`** (32 B, 8-arm union). This
  sketch is a *new* 16 B type for the generated-C path. Pick one globally
  before landing.
- **NaN-box overlap**: `self/hexa_nanbox.h` (rt#38-A, 8 B) is unwired. If
  adopted it supersedes this sketch; if it slips, the 16 B tagged union ships.
- **Baseline binary**: do NOT rebuild `build/artifacts/hexa_native`
  (hexalang_vb1_fix_2026-04-14 do_not_actions).

## Landability

**Cannot land as-is** — for the new 16 B sketch against `self/build_c.hexa`.
Landing requires RT-P3-2 (codegen retrofit) which is out of scope. Gate items:

1. Human decision: Option A vs B (tag-through-codegen vs shim).
2. Human decision: resolve vs defer vs merge with `hexa_nanbox.h` (rt#38-A).
3. Measurement agent runs gcc with the sketch linked against a retrofitted
   codegen and confirms error count drop.

## Production runtime (live) — COMP-P3-1 closure

The discussion above is about the **16 B sketch**. The **32 B production
HexaVal** already exists at `self/runtime.c:260-290` and is what the live
`codegen_c2.hexa` path targets. COMP-P3-1 done_criteria ("IR 설계 문서 +
emit_runtime 프로토타입 PASS") is satisfied by the production path as follows.

### Tag → runtime helper mapping (production)

| Tag | Union slot | Constructor | Primary helper |
|-----|-----------|-------------|----------------|
| `TAG_INT` | `int64_t i` | `hexa_int(n)` | `hexa_add/sub/mul/div/mod/eq` |
| `TAG_FLOAT` | `double f` | `hexa_float(f)` | same (tag-branching) |
| `TAG_BOOL` | `int b` | `hexa_bool(b)` | `hexa_truthy`, `hexa_eq` |
| `TAG_STR` | `char* s` | `hexa_str(s)` / `hexa_str_own(s)` | `hexa_str_concat`, `hexa_to_string` |
| `TAG_VOID` | — | `hexa_void()` | — |
| `TAG_ARRAY` | `{items, len, cap}` | array builders | `hexa_array_slice`, `hexa_arr_push` |
| `TAG_MAP` | `{tbl, len}` | map builders | `hexa_map_get_ic`, `hexa_map_set` |
| `TAG_FN` | `{fn_ptr, arity}` | fn binders | call-site dispatch |
| `TAG_CHAR` | `int i` (code point) | `hexa_from_char_code(n)` | — |
| `TAG_CLOSURE` | `{fn_ptr, arity, env_box}` | closure binder | call-site dispatch |
| `TAG_VALSTRUCT` | `HexaValStruct* vs` | valstruct binder | `hexa_valstruct_*` accessors |

### emit_expr propagation rule (site-local, not global inference)

Every emitted C expression is a `HexaVal`. Tag discrimination happens in the
runtime helper, not in codegen. `gen2_expr` emits constructor-wrapped literals
(`hexa_int(3)`, `hexa_str("x")`) and binary ops dispatch to tag-aware runtime
funcs. For ordered compares (`<`, `>`, `<=`, `>=`) codegen inlines a GCC
statement-expression that promotes to `double` when either operand is
`TAG_FLOAT`; otherwise falls back to `.i` compare. See **prototype pointer**
below. No global type inference pass exists or is required for this contract.

### Prototype pointer (emit_runtime PASS evidence)

- `self/codegen_c2.hexa:1211-1227` — arithmetic + comparison dispatch
- `self/runtime.c:260-290` — HexaVal 32 B union
- `self/runtime.c:404-420` — `hexa_int`, `hexa_float`, `hexa_as_double`
- `self/runtime.c:432-449` — `hexa_str_concat`, `hexa_str` (tag=TAG_STR)
- T23 test (mixed int/float ordered compare) passes under current codegen

### Residual work (not COMP-P3-1)

- RT-P3-1: `hexa_as_num` / `hexa_to_cstring` / `hexa_str_as_ptr` wrappers —
  **landed 2026-04-15** in `self/runtime.c` (forward decl line ~172, defs
  after `__hx_to_double`). Codegen wire-in and regen verification pending
  (see below).
- SH-P3-1: `build/artifacts/hexa_native.c` baseline source was lost (see
  `.gitignore:9 build/`). Recovery partially addressed — `.gitignore` now
  carries `!build/artifacts/hexa_native_baseline.c` exception. Actual
  baseline snapshot not yet committed. Separate from COMP-P3-1.

## RT-P3-1 wrapper shims — landed 2026-04-15

Three non-owning conversion helpers in `self/runtime.c`:

| Shim | Purpose | Error category closed |
|------|---------|----------------------|
| `int64_t hexa_as_num(HexaVal)` | coerce scalar/str to int64 | `-Wint-conversion` (long ← HexaVal) |
| `const char* hexa_to_cstring(HexaVal)` | generic → read-only cstr | `-Wint-conversion` (char* ← long) |
| `const char* hexa_str_as_ptr(HexaVal)` | strict STR slot | `-Wpointer-arith` (escape literals) |

All three unwrap `TAG_VALSTRUCT` first (mirroring `__hx_to_double`). The
cstring path uses two separate file-static buffers (top-level vs valstruct
branches) — thread-unsafe; callers that persist the pointer beyond the next
scalar-path call MUST copy.

### Wire-in sites (codegen_c2.hexa → interpreter.c)

**Re-evaluation 2026-04-15 (post-COMP-P3-2 superseded audit):** the
"40 % SIGSEGV risk" language below is **stale**. Cited blockers all
landed on 2026-04-14, before this design doc was authored:

| Cited blocker | Status | Evidence |
|---------------|--------|----------|
| T33 arena bug (hexa_val_heapify TAG_VALSTRUCT 재귀 누락) | FIXED | b8b5e97 (BT_T33_FIX4) |
| flatten_imports regression | FIXED | 7f2b128 + b8b5e97 (BT_STAGE0_UNBLOCK) |
| HEXA_VAL_ARENA risk (T31/T33/T37/T40) | All closed, default ON | 3ae3d7a (BT_T43_ARENA_DEFAULT_ON) |
| codegen_c2 println/printf 분기 — T32~T46 활성 작업 충돌 | Stale; recent (T58-T63) avoids those branches | 0bea3ce stat — 12 lines, no println/printf hits |

Wire-in is therefore **un-deferred**; next cycle should attempt site
edits + small-program regen verification before going to interpreter.hexa.
Site inventory unchanged:

| Group | Count | Line range (generated .c) | Fix |
|-------|-------|---------------------------|-----|
| A — Wint-conversion | 14 | 519, 521, 523-524, 567, 627-720 | wrap arr-push / int_to_str returns with `hexa_str()` / `hexa_to_cstring()` |
| B — Wpointer-arith | 4 | 750, 753, 756, 759 (escape literals) | StringLit kind emit: wrap `"\n"` etc. in `hexa_str(...)` |
| C — implicit-fn | 2 | `sleep()` call sites | codegen prelude emit explicit `#include <unistd.h>` |

Codegen edit points (source: `self/codegen_c2.hexa`):
- 1276 (println no-arg) — `printf("\n")` → dedicated empty wrapper
- 1374 (format args push) — wrap `gen2_expr` result with `hexa_str()`
- 1380 (eprintln) — wrap with `hexa_to_cstring()`
- 1121 / 1756 (map.contains_key) — remove `.s` direct field access
- ~1850 (StringLit) — auto-wrap escape-bearing literals

Closing all three groups moves default-flag gcc errors **20 → 0** per R2
measurement against the corrupted hexa_native.c; independent of baseline
source recovery.
