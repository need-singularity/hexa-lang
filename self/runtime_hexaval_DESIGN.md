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

**Cannot land as-is.** This is a *design sketch* plus documentation. Landing
requires RT-P3-2 (codegen retrofit in `self/build_c.hexa`) which is explicitly
out of scope per the agent brief. Gate items before landing:

1. Human decision: Option A vs B (tag-through-codegen vs shim).
2. Human decision: resolve vs defer vs merge with `hexa_nanbox.h` (rt#38-A).
3. Measurement agent runs gcc with the sketch linked against a retrofitted
   codegen and confirms error count drop.
