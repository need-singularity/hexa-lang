# stage0 arr.push O(N²) — Fix Plan (ROI #155 follow-up)

**Date:** 2026-04-19
**Ref:** `self/test_push_perf.hexa`, `self/hexa_full.hexa:16208`,
`doc/roi.json #155`, `doc/seed_unlock_runbook.md`, memory
`project_fib_rss_array_dominated`.
**Current status:** seed-freeze holds; plan is source-level only.

---

## 1. Symptom

User-level loop `arr = arr.push(i)` in stage0-interpreted Hexa consumes
~10.8 GB RSS at N=4000, watchdog kill. Curve doubles quadratically
(ROI #155 benchmark). Host C runtime `hexa_array_push` is amortized
O(1) (2× realloc, `self/runtime.c:968`), so the O(N²) lives at the
**interpreter Hexa-source layer**, not in runtime.c.

## 2. Current implementation (self/hexa_full.hexa:16208–16223)

```hexa
fn array_push_inplace(arr_val, item) {
    let idx = arr_val.int_val
    if array_is_sole_owner(idx) {
        let items = array_store[idx]            // ← SUSPECT A
        array_store[idx] = items.push(item)     // ← SUSPECT B
        return arr_val
    }
    let items = array_store[idx]
    let new_items = items.push(item)
    array_decref(idx)
    return val_array(new_items)                 // COW path
}
```

`array_store` is a Hexa-level array of host item-buffers. `items` binds
to slot content; `items.push(item)` dispatches on a host-array (not a
TAG_ARRAY Val), so it hits runtime.c amortized path.

## 3. Root cause candidates (ranked)

| # | Cause | Evidence | Fix difficulty |
|---|-------|----------|----------------|
| A | `let items = array_store[idx]` forces a PBV snapshot each iter (items buffer copied O(N) at every push) | test_push_perf.hexa lines 52–58; rt#32-N whitelist in runtime.c:1039 does not cover `array_store` global | medium |
| B | Double `env_set` on Assign-with-method-RHS re-increments refcount → sole-owner=false → COW path fires | hexa_full.hexa:7129 (NK_ASSIGN Ident) + 7820 (AssignStmt Ident); both call `env_set(node.left.name, val)`; only one should fire per assign | easy |
| C | PBV-snapshot arena at Call boundary bumps refcount on receiver without matching decref | hexa_full.hexa:9299,12885 | hard |

Hypothesis: **A is dominant**. The stage0 codegen emits a host
`hexa_val_snapshot_array` wrapper around `let items = array_store[idx]`
because `array_store` is in the rt#32-N hot-global whitelist (partial
list in runtime.c:1039). If the whitelist triggers for `array_store`,
every iteration deep-copies the entire slot buffer → O(N) per push →
O(N²) total. Needs codegen check to confirm.

## 4. Short-term workaround — direct slot replace (no `items` local)

Single-line source edit, eliminates the intermediate binding that
triggers snapshot-copy. Safe because the slot write and the method
receiver are the same expression:

```hexa
fn array_push_inplace(arr_val, item) {
    let idx = arr_val.int_val
    if array_is_sole_owner(idx) {
        array_store[idx] = array_store[idx].push(item)    // NEW
        return arr_val
    }
    // COW path unchanged
    let items = array_store[idx]
    let new_items = items.push(item)
    array_decref(idx)
    return val_array(new_items)
}
```

Pros:
- 1-line change, no ABI/semantic delta
- No new runtime primitive
- Works with any seed (no stage0 rebuild needed to unlock — only
  propagate after unfreeze)

Cons:
- Stage0 might still snapshot `array_store[idx]` on the read-half of
  IndexAssign RHS. Needs codegen probe on emitted C.
- Doesn't fix the underlying snapshot-whitelist overreach (same bug
  would reappear for any other `let x = big_global` pattern).

## 5. Long-term fix — `push_mut` builtin + whitelist audit

Add an FFI primitive `array_push_mut(slot_idx, item)` exposed via
intrinsic, bypassing both env_set double-incref and snapshot-wrap:

```hexa
@intrinsic fn array_push_mut(idx: int, item: Val) { /* host */ }
// use inside array_push_inplace:
if array_is_sole_owner(idx) {
    array_push_mut(idx, item)    // writes array_store[idx] host-side
    return arr_val
}
```

Pros:
- Eliminates **all three** candidate causes (no let binding, no second
  env_set, no call-boundary snapshot since intrinsics skip PBV wrap)
- Permanent fix, generalises to other hot-loop mutators
  (`map_set_mut`, `string_append_mut` in future)

Cons:
- Requires parser keyword + codegen intrinsic table edit in
  `self/native/hexa_cc.c` and `self/codegen_c2.hexa`
- Needs stage0 rebuild to propagate → **blocked by seed-freeze**
- Companion audit: narrow rt#32-N snapshot whitelist in runtime.c:1039
  to only fn-return aliases, exclude module-level mutables

## 6. Comparison

| Dimension          | Short-term (Option A) | Long-term (push_mut) |
|--------------------|-----------------------|----------------------|
| LoC delta          | 1 line                | ~40 lines (3 files)  |
| Seed-freeze needed | No (source-only, unblocks after #152) | Yes |
| Fixes cause A      | Likely (probe needed) | Yes (definitive)     |
| Fixes cause B      | No                    | Yes                  |
| Fixes cause C      | No                    | Yes                  |
| Risk               | Low                   | Medium               |
| Reversibility      | `git revert` one line | Intrinsic tombstone  |

## 7. Seed-freeze interaction

Per `doc/seed_unlock_runbook.md`, `build/hexa_stage0` and
`self/native/hexa_v2` are frozen pre-IC-slot fix (commit `fcf275f4`).
**Any edit to `self/hexa_full.hexa` is dormant** until seed unlock via
runbook Path (a)/(b)/(c). Therefore:

1. **Land source edit now** (Option A 1-liner) — diff reviewable,
   test `self/test_push_perf.hexa` documents expected linear curve
2. **Defer actual fix activation** until seed unlock
3. **Re-bench after unlock** — expect `ratio 2000/1000 ≈ 2` (linear);
   if still quadratic, escalate to Option B (`push_mut` intrinsic)

## 8. Verification

- Run `self/test_push_perf.hexa` pre/post; require the terminator line
  `PUSH_PERF_LINEAR` (test line 163)
- Add N=4000 regression after confirming sub-GB RSS
- Cross-check against memory feedback `project_fib_rss_array_dominated`
  — fib RSS is array-arena dominated (rt#32-M/N path); this fix is
  complementary (interpreter-layer), not redundant

## 9. Open questions

- [ ] Confirm which of A/B/C is dominant via codegen inspection of
      `self/hexa_full.hexa` line 16214 emitted C
- [ ] Decide whether `array_store` belongs in the rt#32-N snapshot
      whitelist at all — it is module-level mutable, arena safety is
      already guaranteed by refcount discipline
- [ ] Check whether `truncate` / `pop` paths exhibit the same pattern
      (hexa_full.hexa:15177, 15191) — likely yes, identical fix applies

## 10. Action checklist

- [x] Document root cause in test harness (commit `e05b566c`)
- [x] ROI #155 status = done (plan-level)
- [ ] Apply Option A 1-liner at hexa_full.hexa:16214
- [ ] Apply same pattern to truncate (15177) + pop (15191) sole-owner
      branches
- [ ] Codegen probe — dump emitted C for `let items = array_store[idx]`
      and verify absence of `hexa_val_snapshot_array` wrap
- [ ] Wait for seed unlock (ROI #152)
- [ ] Re-bench, promote test to CI, optionally escalate to Option B
