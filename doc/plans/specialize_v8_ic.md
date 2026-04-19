# @specialize V8-style Hidden Class IC — Design (ROI #167 scaffold)

Status: scaffold (mono-only) 2026-04-19
Scope: data structures + 1 mono dispatch path. poly/mega = follow-up ROI.
Semantic: 타입 단상화 (V8 Hidden Class) — ai-native.json stub_registered → semantics_impl.

## 1. Problem

HEXA-lang struct field access currently lowers to a generic field lookup:

    obj.x   →   field_by_name(obj, "x")

Every call path walks string-name lookup against the struct's field table. For
hot paths (ML inner loops, large struct arrays, any tight object-traversal
pattern), this is O(n_fields) name-compare per access — a catastrophic vs.
V8/JSC/SpiderMonkey class-based IC dispatch which is O(1) hit + rare miss.

V8 solves this with Hidden Classes ("Shapes" in JSC): objects with the same
field layout share a HiddenClass id. Per-call-site Inline Caches (IC) record
the first observed `hc_id` + cached `offset`, and subsequent accesses are
guarded by `if obj.hc == expected_hc` — single integer compare + direct
offset load. Miss triggers state transition (mono → poly → mega).

## 2. Hidden Class Model

### 2.1 Definition

```
struct HiddenClass {
    id: int                           // globally unique, monotonic
    parent_id: int                    // -1 for root ("empty object")
    fields: list<(name, offset, tag)> // insertion order, frozen per class
    transitions: map<string, int>     // field_name → next_hc_id
}
```

Key invariants:
- A HiddenClass is immutable once created. Adding a new field = transition to
  a new (derived) class.
- `offset` is 0-based index into the object's inline-slot array.
- `tag` is the narrowed runtime type tag observed (int, float, string,
  struct, array, map, …) at the moment of field definition.
- `transitions` memoizes "parent + add field X → child hc". Two objects that
  add the same fields in the same order converge on the same hc.

### 2.2 Transition Chain (example)

    empty (id=0)
        │
        ├── +x:int  ──► {x:int} (id=1)
        │                    │
        │                    └── +y:int ──► {x:int, y:int} (id=2)
        │
        └── +x:float ──► {x:float} (id=3)

Two objects `Point{x:1,y:2}` and `Point{x:3,y:4}` both land at hc=2 → mono.
An object `Pt{x:1.0,y:2}` diverges at root (hc=3) → at the call site that saw
hc=2 first, this is a miss → poly transition.

### 2.3 Registry

A process-global `class_registry` (list<HiddenClass>) holds all classes by id.
Lookup is O(1) (indexed list). Classes are never GC'd in scaffold scope
(V8 does; follow-up).

## 3. Inline Cache (IC) Model

### 3.1 Slot

```
struct ICSlot {
    site_id: int            // call-site id (from AST lower)
    state: int              // 0=UNINIT, 1=MONO, 2=POLY, 3=MEGA
    expected_hc: int        // MONO: the one hc; POLY: -1 (see poly_cache)
    cached_offset: int      // MONO: direct offset into slots
    cached_tag: string      // MONO: expected field tag
    poly_cache: list        // POLY: list<(hc_id, offset, tag)> (≤4 entries)
    miss_count: int         // diagnostics; increments on init + transitions
    hit_count: int          // diagnostics; increments on each mono hit
}
```

### 3.2 State Machine (scaffold: mono only)

    UNINIT ──first access──► MONO  (record hc_id + offset)
    MONO   ──same hc ─────► MONO  (HIT — offset load)
    MONO   ──different hc──► POLY (follow-up ROI)
    POLY   ──>4 shapes ───► MEGA (follow-up ROI)
    MEGA   ──any access ──► generic name lookup (no specialization)

In this ROI we implement ONLY the first two edges (UNINIT → MONO, MONO HIT).
The diverge edge (MONO → POLY) records `miss_count++` and degrades to a
generic path (same as today's behavior), so correctness is preserved and
poly/mega can be added incrementally without breaking tests.

## 4. Applicable AST Sites

This ROI targets exactly one AST kind:

    Expr.kind == "field"      // obj.field_name

Follow-up sites (out of scope this ROI):
- `Expr.kind == "call"` (method dispatch)
- `Expr.kind == "index"` with string literal (obj["x"])
- `Expr.kind == "binary"` with op="." (legacy form)

Each field-access AST node gets a unique `site_id` during lower; each site_id
has exactly one ICSlot in a parallel `ic_table`.

## 5. Mono Dispatch Flow (1-path scaffold)

```
// Pseudo-code for ic_lookup(slot, obj) — the hot path
fn ic_lookup(slot, obj, field_name) {
    let obj_hc = obj.hc_id
    if slot.state == MONO && slot.expected_hc == obj_hc {
        slot.hit_count += 1
        return obj.slots[slot.cached_offset]     // O(1) hit
    }
    // miss / uninit
    return ic_miss(slot, obj, field_name)
}

fn ic_miss(slot, obj, field_name) {
    let hc = class_registry[obj.hc_id]
    let (off, tag) = hc.find_field(field_name)   // O(n_fields), rare
    if slot.state == UNINIT {
        slot.state = MONO
        slot.expected_hc = obj.hc_id
        slot.cached_offset = off
        slot.cached_tag = tag
        slot.miss_count = 1                       // init = 1 miss
    } else {
        // MONO miss → POLY transition (follow-up). For scaffold:
        // degrade to generic path, record miss.
        slot.miss_count += 1
    }
    return obj.slots[off]
}
```

Test contract (self/test_specialize_ic.hexa):
- 100 accesses on the same hc:
  - `slot.state == MONO` after call 1
  - `slot.miss_count == 1` after all 100 calls (only the init miss)
  - `slot.hit_count == 99`
  - `slot.expected_hc` equals the object's hc
  - every returned value matches the expected field value

## 6. File Map

| Role               | Path                                       | Role    |
|--------------------|--------------------------------------------|---------|
| Design (this)      | docs/plans/specialize_v8_ic.md             | plan    |
| Data structs       | self/vm/ic.hexa                            | runtime |
| @specialize impl   | self/attrs/_own/specialize.hexa            | attr    |
| @specialize meta   | self/attrs/specialize.hexa                 | attr    |
| Registry           | self/attrs/_registry.hexa                  | SSOT    |
| Scaffold tests     | self/test_specialize_ic.hexa               | test    |

## 7. Non-goals (explicit)

- No real AST rewrite. Existing `@specialize` attr remains hint-only; this
  ROI introduces pure-Hexa runtime structures + a call-style API that can be
  plugged into the field-access lower in a follow-up ROI without changing
  this scaffold's shape.
- No poly/mega dispatch. `miss_count` increments but state stays MONO (next
  ROI may flip to POLY on second miss).
- No codegen integration. `self/codegen/` / `self/codegen_c2.hexa` untouched.
- No GC / class-registry compaction.
- No deopt on type narrowing violation (we only observe the tag, don't
  enforce it yet).

## 8. Follow-up ROIs (sketch)

1. **MONO → POLY transition** — second miss flips state to POLY with a 4-slot
   linear-probe cache. Keep POLY HIT O(n_entries ≤ 4).
2. **POLY → MEGA** — >4 distinct hcs degrades to generic path but marks the
   site as mega for profile-guided recompile.
3. **AST lower integration** — field-access AST node allocates a site_id +
   wires `ic_lookup` call. This is the first change that actually accelerates
   real programs.
4. **Type tag check + deopt** — enforce `cached_tag` on HIT; mismatch =
   deopt back to generic.
5. **HiddenClass compaction** — registry GC + class-id reuse.
6. **Method IC** — same model applied to `Expr.kind == "call"`.

## 9. Why this scaffold now

- Unlocks "V8 IC" semantic label for the @specialize stub (ai-native.json).
- Shippable today with no compiler-core risk: purely new files, no touched
  codegen/parser/runtime. Stage0 rebuild (HX8) not required.
- Tests the data-structure shape before committing to AST-level integration.
  If we learn hc_id layout needs a change, it's one-file edit.

## 10. Cost / Impact

- impact_x: 7.5 (ai-native.json) — O(1) field hit vs. O(n_fields) today.
- est_hours: 40h — scaffold ≤ 6h (this ROI), rest = follow-ups.
- Risk: low (additive only). Existing `@specialize` tests continue to pass
  because the hint path is unchanged.
