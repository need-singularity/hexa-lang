# S-5 — FFI Marshalling Plan (skeleton)

**Status:** PLAN_DRAFTED (skeleton only — no marshalling logic yet)
**Roadmap item:** `shared/hexa-lang/ai_native_s_level.json` → S-5 / S5-1
**Owner:** hexa-lang
**Drafted:** 2026-04-17

## Goal

Minimise the per-call cost of the `extern fn` boundary (hexa ↔ C) so that
string- and array-typed args no longer force a defensive copy on every
call — without breaking the existing Accelerate / BLIS bindings.

## Current-state snapshot (from grep of `self/`)

- `self/parser.hexa` (L3040) already parses `extern fn name(params) -> ret`
  with optional `@symbol("c_name")` / `@link("lib")` attributes into an
  `ExternFnDecl` AST node — **no marshalling metadata attached**.
- `self/runtime.c` L3494 `hexa_ffi_marshal_arg(HexaVal)` performs a
  per-arg switch on `HX_TAG(v)` and returns a raw `int64_t`. Strings are
  handed off as `(int64_t)(uintptr_t)HX_STR(v)` — i.e. **the underlying
  C `const char*` pointer is reused with no copy on the hexa side**, but
  there is also no lifetime contract, no length pair, and no encoding tag.
- `self/runtime.c` L3509 `hexa_call_extern_raw` dispatches 0..14 args via a
  `switch (nargs)` over 15 typedef'd function-pointer shapes. Arity cap = 14.
- `self/runtime.c` L3571 `hexa_extern_call_typed` adds a `float_mask`
  bitmask so doubles can be routed to SIMD registers under the AArch64
  / SysV ABIs — again, **no struct/array path**; those still fall through
  the int64 lane as opaque pointers.
- `self/ml/accelerate_ffi.hexa` — 40+ `extern fn` declarations targeting
  CBLAS / vDSP / BNNS. Every pointer arg is typed `*Void`, which means
  the caller already passes a pre-allocated `alloc_raw(…)` buffer. No
  hexa `string` or `[T]` ever reaches these entry points today.

Net: marshalling is **pointer-passthrough for strings**, **explicit
hand-allocation for arrays** (`alloc_raw`), and **int64 transmute for
doubles**. There is no typed array / slice marshalling path.

## 3-step rollout

### (a) Marshal spec

- Enumerate the supported `target_repr` values as string constants in
  `self/opt/ffi_marshal.hexa` (`c_i64`, `c_f64`, `c_bool`, `c_cstr`,
  `c_ptr`, `c_void`). This matches the 5-way switch already in
  `hexa_ffi_marshal_arg`, so the spec is grounded in the runtime.c reality.
- Document per-`target_repr`:
  - in-shape (hexa value tag it accepts),
  - out-shape (C representation emitted),
  - copy vs. borrow (string/array = borrow pointer; int/bool = value),
  - lifetime (tied to the hexa Val arena epoch when borrowed).
- Defer structs, tuples, and fixed-size arrays to S-5.2.

### (b) Stub + tests

- `self/opt/ffi_marshal.hexa` exposes `marshal_in(val, target_repr)` →
  bytes and `marshal_out(bytes, target_repr)` → val, both returning a
  sentinel for now. The module's job in (b) is **surface discovery**:
  make the exported names and constants compile under the current stage0
  interpreter so any consumer (parser attr, codegen, docs generator) can
  start referring to them.
- `tests/t_ffi_marshal_skeleton.hexa` asserts the surface exists and that
  the constants round-trip as strings. No ABI is touched.

### (c) Wiring

- Plumb `target_repr` through `ExternFnDecl` (one new `param_repr` field
  on each param) in a follow-up commit. Once wired, `hexa_extern_call`
  can consult the per-arg tag and skip `hexa_ffi_marshal_arg` entirely
  for the homogeneous-int case (the fast path that covers `~80%` of
  observed Accelerate calls).
- Benchmark: add one `bench/bench_ffi_loop.hexa` calling `cblas_sdot`
  1e6× to measure the marshal-path-removal delta. Gate on ≥5% wall-time
  reduction on htz before promoting the fast path to default.

## Non-goals (this quarter)

- Struct/tuple marshalling (S-5.2, after the basic spec hardens).
- Variadic C functions (`printf` family) — rejected upstream at parser.
- Owned/transfer semantics for buffers returned from C (e.g.
  `strdup`-style APIs). Deferred until a GC story exists.
- Full lifetime proofs on borrowed pointers. The spec will state the
  invariant; enforcement stays at the programmer's discretion for now.

## Risk register

- **R1** — Any change to `hexa_ffi_marshal_arg` risks regressing the
  Accelerate path, which is the single largest FFI consumer today.
  Mitigation: keep the current switch as the default; only the typed
  fast path short-circuits it.
- **R2** — String borrow vs. copy: interpreter-mode HexaVal strings may
  be arena-allocated. A borrowed pointer that outlives its arena epoch
  is a use-after-free. Mitigation: spec step (a) forbids C→hexa pointer
  retention across `extern fn` returns; add an explicit `c_cstr_copy`
  repr in the next iteration if a consumer needs it.
- **R3** — Float ABI drift: `hexa_extern_call_typed` already handles the
  SIMD-register routing for up to 14 args. Extending the spec without
  touching that function leaves the ABI invariant.
- **R4** — Parser scope creep: attaching `param_repr` to `ExternFnDecl`
  pulls in `self/parser.hexa`, which is a sibling-agent file. Must
  coordinate via the roadmap before step (c).
- **R5** — Stage0 rebuild cost: per HX8, any `.hexa` touching the
  codegen path requires `scripts/rebuild_stage0.hexa`. This skeleton
  avoids that by staying outside the stage0 footprint — only `self/opt/`
  and `tests/` additions, no parser / codegen wiring.
