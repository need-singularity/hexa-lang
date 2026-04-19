# arr.push O(N²) — Fix Plan (ROI #155, T8b research)

**Date:** 2026-04-19
**Companion:** `doc/plans/stage0_push_on2_fix_20260419.md` (earlier draft)
**Refs:** `self/hexa_full.hexa:16208`, `self/test_push_perf.hexa`,
`self/runtime.c:968`, `build/artifacts/hexa_full.c:22071`,
`doc/roi.json` entry #155, memory `feedback_seed_freeze.md`.
**Status:** seed-freeze active → source-level change only.

---

## 1. Measured curve (stage0 `./hexa run`)

```
N=100    ms=0
N=500    ms=0
N=1000   ms=1000
N=2000   ms=5000   ← 5× for 2× N, quadratic confirmed
N=4000   OOM (watchdog 4GB, RSS peak ~10.8GB)
ratio 2000/1000 = 5  →  PUSH_PERF_QUADRATIC_CONFIRMED
```

Expected linear: `ratio ~= 2`. Host C `hexa_array_push` (runtime.c:968)
is genuine 2× amortized — the overhead lives above it.

## 2. Call-graph per user `arr = arr.push(i)`

```
AssignStmt (hexa_full.hexa:7819)
  └─ eval_expr(RHS) → NK_CALL (6856)
       ├─ eval_expr(callee.left)  ── Ident(arr) → env_lookup → Val{slot_X}
       ├─ call_method (15169)     ── dispatch "push"
       │    └─ array_push_inplace (16210)
       │         ├─ array_is_sole_owner(idx)   — expects rc==1
       │         ├─ let items = array_store[idx]
       │         └─ array_store[idx] = items.push(item)    ← host O(1) amortized
       └─ env_set(callee.left.name, result)    ← line 6957 (1st write-back)
  └─ env_set(node.left.name, val)              ← line 7821 (2nd write-back)
```

Two env_set calls per statement. Each pair does
`incref(new)` then `decref(old)` — when new==old slot this is a no-op,
rc stays 1. **Not the bug.**

## 3. Actual dominant cause — COW slot leak

Host `hexa_array_push` in runtime.c **is** O(1) amortized (line 968,
cap doubles from 4→8→16…). Compiled `array_push_inplace` in
`build/artifacts/hexa_full.c:22074-22076` is straight
`_AD(array_store)[idx] = hexa_arr_push(items, item)` — O(1) amortized
when sole-owner branch taken.

RSS math from N=4000 → 10.8 GB says ~2.7 MB/push. That is only
explicable if **every iteration falls into COW** (line 16218-16222):

1. `val_array(new_items)` → `array_store = array_store.push(items)`
2. New slot allocated, old slot decref'd to 0
3. **Freelist reuse disabled** (hexa_full.hexa:16146, "M4-fix")
4. `array_store` and `array_refcounts` grow monotonically each push
5. Each old slot retains its `items` buffer until process exit
6. Total retained = Σ i for i in 0..N ≈ N²/2 longs ⇒ ~64 MB logical,
   but arena-promotion + Val struct-field bloat (12 `__E` sentinels
   per Val at line 16159) inflates per-slot overhead ~40×

## 4. Why does sole-owner check fail every iteration?

Suspect ordering (to confirm with codegen probe):

| # | Candidate | Mechanism |
|---|-----------|-----------|
| A | rt#32-N snapshot-promote whitelist wraps `let items = array_store[idx]` | `hexa_val_snapshot_array` deep-copies → new Val, rc=2 on next read |
| B | Val struct construction in `val_array` allocates 12-field struct per push; `__E` sentinel allocation path bumps a shared counter visible through `array_refcounts` parallel-push | requires audit of val_struct path |
| C | Call-boundary arena mark (HEXA_VAL_ARENA) promotes receiver Val on each invocation, re-parenting rc ownership | default OFF (val_arena_active), unlikely unless test env sets it |

Most likely: **A**, because `array_store` is module-level `pub let
mut` and matches the "shared persistent global" pattern that rt#32-N
was designed to guard. Confirming needs `grep hexa_val_snapshot_array`
in `build/artifacts/hexa_full.c` around slot-read sites.

## 5. Recommended fixes (ordered by ROI)

### F1 — drop the `items` local (1-line)

```hexa
fn array_push_inplace(arr_val, item) {
    let idx = arr_val.int_val
    if array_is_sole_owner(idx) {
        array_store[idx] = array_store[idx].push(item)   // direct
        return arr_val
    }
    let items = array_store[idx]
    let new_items = items.push(item)
    array_decref(idx)
    return val_array(new_items)
}
```

Eliminates the snapshot-promote trigger site. Propagate to sibling
mutators at 15177 (`truncate`) and 15191 (`pop`).

### F2 — explicit capacity pre-grow (if F1 insufficient)

Surface `arr.reserve(n)` intrinsic → calls a new
`hexa_array_reserve(HexaVal, int64_t new_cap)` in runtime.c that
realloc-grows to the requested capacity once. User code `reserve(n)`
before tight push loops; also emit a
`codegen_c2` auto-hint when `while i < N { arr.push(…) }` pattern is
detected with N known at call-site.

### F3 — `push_mut` intrinsic (definitive)

```hexa
@intrinsic fn array_push_mut(idx: int, item: Val)
// inside sole-owner branch:
array_push_mut(idx, item); return arr_val
```

Bypasses Val snapshot, double env_set, AND call-boundary wrap.
Requires `self/native/hexa_cc.c` + `self/codegen_c2.hexa` edits.
Blocked by seed-freeze (needs stage0 rebuild).

### F4 — re-enable freelist reuse

Uncomment lines 16146-16154 once F1/F3 confirm rc discipline is
sound. Bounds `len(array_store)` even if COW re-fires.

## 6. Geometric-growth capacity tracking

Host layer already geometric (runtime.c:973 `new_cap = cap * 2`).
Hexa-level `array_store` and `array_refcounts` globals use the SAME
host `hexa_array_push` so they too geometric-grow — **no change
needed at the hexa level**. The fix is purely about preventing COW
from firing when it shouldn't.

## 7. Seed-freeze interaction

Per memory `feedback_seed_freeze.md` and `doc/seed_unlock_runbook.md`,
`build/hexa_stage0.real` + `self/native/hexa_v2` are pinned at commit
`fcf275f4`. Any `self/hexa_full.hexa` edit is **dormant** until
unlock. Land the F1 one-liner now for diff review; activation waits
on ROI #152 circular-rebuild regression.

## 8. Verification

1. Re-run `./hexa run self/test_push_perf.hexa` post-F1
2. Expect `ratio 2000/1000 ≈ 2`, terminator line `PUSH_PERF_LINEAR`
3. Extend to N=4000, require RSS < 512 MB
4. Promote to regression gate — add to `self/test_array_ops_suite.hexa`
5. Codegen probe: `rg hexa_val_snapshot_array build/artifacts/hexa_full.c`
   around line 22074; must be ZERO occurrences for `array_store` reads

## 9. Open questions

- [ ] Confirm snapshot-promote is the actual wrap (codegen inspection)
- [ ] Audit rt#32-N whitelist — narrow to fn-return aliases only
- [ ] Apply same fix pattern to `truncate` (15177), `pop` (15191),
      `map_set` (16165), `string_append` (if exists)
- [ ] Benchmark N=10⁵, N=10⁶ after unlock to pin amortization constant

## 10. Action checklist

- [x] Benchmark + root-cause doc in `test_push_perf.hexa` (commit `e05b566c`)
- [x] This plan lands (doc/plans/arr_push_on2_fix_20260419.md)
- [ ] Apply F1 1-liner at `hexa_full.hexa:16214`
- [ ] Mirror F1 to `truncate`/`pop` sole-owner branches
- [ ] Wait for seed unlock (ROI #152) to activate
- [ ] Re-bench, gate PUSH_PERF_LINEAR, escalate to F3 if still quadratic
