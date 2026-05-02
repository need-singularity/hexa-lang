# A1 — First-class function pointers

- **Status**: spec proposed (additive, no migration)
- **Date**: 2026-05-02
- **Bundle**: composability A1+A5+B2+B5 (BG-HX2)
- **Cross-refs**:
  - precedent: `example/test_closures.hexa` (closures already first-class for `|x| ...`)
  - precedent: `test/regression/type_of_fn.hexa` (TAG_FN / TAG_CLOSURE classified)
  - precedent: `tool/parallel_scheduler.hexa` (switch-style emulated dispatch baseline)

## §0 TL;DR

`let cb: fn(int) -> str = my_handler` exists today only as a *type annotation* — under the
hood, "fn-pointer" call sites in current tooling are emulated via switch-style dispatch
(see `parallel_scheduler.hexa` selftest harness). Closures (`|x| ...`) are already
first-class and TAG_FN / TAG_CLOSURE are correctly classified by `type_of()` post commit
0adac579. The remaining gap: **named top-level `fn` values** assigned to a typed slot
must be directly invokable through that slot without a switch trampoline.

This RFC pins the contract surface so consumer code can rely on the form even before the
runtime sheds its emulation path. The interpreter already does the right thing today
(spec lock); the AOT path needs the value-form lowering to land before the contract
becomes universal. This is **additive only** — no existing closure call site changes.

## §1 Current behavior (baseline)

```hexa
fn my_handler(n: int) -> str { return "got " + to_str(n) }

// Path 1: named call (works in both interp + AOT)
println(my_handler(42))                       // "got 42"

// Path 2: closure binding (works in both)
let cb_lambda = |n: int| my_handler(n)
println(cb_lambda(42))                        // "got 42"

// Path 3: typed fn-pointer slot (TARGET)
let cb: fn(int) -> str = my_handler           // type annotation parsed
println(cb(42))                               // interp: works; AOT: switch-trampoline today
```

Path 3 currently parses but the **AOT lowering** routes the call through a
generated switch over candidate fn ids when `cb` is escaped to a heap collection
(observed in `tool/parallel_scheduler.hexa` task dispatch table). Inline / monomorphic
sites compile fine. The contract gap is the polymorphic / data-driven case.

## §2 Proposed contract (additive)

A typed slot `let|var <name>: fn(<params>) -> <ret> = <expr>` MUST behave identically
under interpreter and AOT for the following operations:

| Op | Form | Required |
|----|------|----------|
| Direct invoke | `cb(args...)` | byte-identical stdout vs interp |
| Pass as arg | `apply(cb, x)` | byte-identical |
| Return from fn | `fn factory() -> fn(int) -> str { return my_handler }` | byte-identical |
| Store in array | `let cbs: [fn(int) -> str] = [my_handler, other_handler]` | byte-identical |
| Store in struct | `struct Slot { cb: fn(int) -> str }` | byte-identical |
| Compare to `nil` | `if cb != nil { cb(x) }` | required |
| `type_of(cb)` | `"fn"` (interp) OR `"closure"` (AOT) | non-`"unknown"` (locked by 0adac579) |

**Non-goals** (deferred):
- Function reference syntax `&my_handler` — current `my_handler` (bare ident in value
  position) already converts to fn-value; no new sigil needed.
- Method references `&Type::method` — out of scope (no method system yet).
- Higher-rank types `for<T> fn(T) -> T` — out of scope.

## §3 Compatibility

- Closures (`|x| ...`) — unchanged. They already satisfy contract today.
- Existing switch-style emulation in `parallel_scheduler.hexa` and similar tools — left
  in place (additive policy). They MAY migrate to direct fn-ptr in a later cycle but
  are not required to.
- AOT calling convention — fn-value already carries `(fnptr, env*)` pair under
  `runtime.c`'s closure ABI; this RFC requires that named-fn lowering produce the same
  pair shape with `env* = NULL`, so the call site doesn't need to know it's not a closure.

## §4 Impl plan (additive, no migration)

**Parser side** (`self/parser.hexa`):
- Accept `fn(<types>) -> <type>` as a type expression in `let`/`var` annotation, struct
  field type, array element type, fn param type, fn return type. Already parsed for
  param/return positions; verify the additional positions (let/struct/array) don't error.
- Effort: ~30 LoC if any positions reject; 0 LoC if already accepted (verify by selftest).

**Type checker side** (`self/typecheck.hexa` if exists, else inferred):
- When RHS of `let cb: fn(...) -> ... = expr`, accept either:
  - Bare named fn ident → coerce to fn-value (env=NULL).
  - Closure literal `|...| ...` → already fn-value.
  - Other `fn`-typed expression → identity.
- Reject incompatible signatures with a clear error.
- Effort: ~50 LoC (signature compare + coercion).

**Codegen side** (`self/codegen_c2.hexa`):
- Lowering rule: bare named fn in value position emits `(struct hexa_closure){.fn = &<name>, .env = NULL}` instead of a fn-id integer that needs switch dispatch.
- Call site `cb(args)` MUST dispatch via `cb.fn(cb.env, args...)` regardless of source
  form (closure or named-fn coerced).
- Effort: ~80 LoC (single new lowering case + call-site uniformization).

**Runtime side** (`runtime.c`):
- No change required if closure ABI already takes `env=NULL`. Verify by reading the
  call helper.
- Effort: 0 LoC expected; ~20 LoC contingency.

**Total estimated impl scope**: ~160 LoC across 3 files.

## §5 Falsifiers (raw#71)

1. `cb(42)` in AOT produces a different stdout byte than `my_handler(42)` → fail.
2. `[my_handler, other][0](x)` in AOT crashes or returns wrong value → fail.
3. `type_of(cb) == "unknown"` (regression of 0adac579) → fail.
4. Passing a `fn(int) -> str` where `fn(str) -> int` is expected compiles → fail (typecheck silent-wrong).

## §6 Selftest fixture

See `proposals/composability_a1_a5_b2_b5/fixtures/a1_fn_pointer.hexa` — 7 cases
covering each row of §2 contract table (direct invoke, pass-as-arg, return-from-fn,
store-in-array, store-in-struct, nil-compare, type_of).

## §7 raw#10 caveats

1. Current AOT switch-trampoline emulation in `parallel_scheduler.hexa` is NOT a bug —
   it predates the typed slot contract and works correctly. This RFC is about enabling
   the cleaner form, not deprecating the old one.
2. Struct field `cb: fn(...) -> ...` requires struct field types to accept `fn` — if
   the parser rejects, that is a separate parse extension (count toward A1 scope).
3. `nil`-compare requires `nil` to be a representable fn-value. If `nil` is purely a
   typeless sentinel today, contract may need `Option<fn(...)>` or similar — OUT OF
   SCOPE for this RFC; defer to a sister proposal.
4. Closure env capture across an fn-ptr slot (assigning a `|x| outer` to a slot typed
   `fn(int) -> int`) requires the env to survive the slot's lifetime — already guaranteed
   by closure ABI, but a fixture case is included to lock it.
5. Mac-local strict — selftest must run under `hexa` resolver darwin-bypass.

## §8 Effort + retire

- Effort: spec ~3h (this doc). Impl ~1 day (parser verify + codegen single case + runtime nil-check).
- Retire when: §6 fixture passes byte-identical interp+AOT for 4 consecutive sessions OR
  this RFC is superseded by a higher-level type-system overhaul.
