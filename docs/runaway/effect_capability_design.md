# Effect & Capability System for hexa-lang — Runaway Containment Design

Status: DRAFT (design only — no implementation in this commit)
Author: agent-a7186b2fbf0368bc3 worktree
Date: 2026-05-06
Scope: type-system extension proposal. Implementation tracked under a future
branch (`feat/effect-system-bootstrap`).

---

## 1. Goal & Non-Goal

### 1.1 Goal

Make every form of unbounded resource consumption (a "runaway") visible in
the type system so that:

1. A function that **can** loop forever, recurse without a base case,
   allocate without bound, perform I/O, spawn processes, or open network
   sockets must say so in its signature.
2. A caller that wishes to invoke such a function must hold a **capability
   handle** (`Fuel`, `Heap`, `Time`, `Procs`, …) that meters the resource
   and forces a budget decision at each effect site.
3. The default for an un-annotated function is **total/pure** — silent
   acquisition of a runaway effect is a type error, not a warning.
4. Existing hexa-lang code (the self-host pipeline + downstream apps in
   hive/anima/nexus) can be migrated incrementally with inferred effect
   sets and a `@legacy_effects` escape hatch.

### 1.2 Non-Goal

- **Not** a full algebraic-effect handlers system (Koka / Eff / Frank
  style with delimited continuations). Effects here are **labels**
  with a row-polymorphic membership test, not handlers. Future work may
  layer handlers on top.
- **Not** a substructural / linear type system across the whole language.
  Linearity applies **only** to capability handles; ordinary values stay
  unrestricted.
- **Not** a security boundary. The capability handle is a budgeting and
  documentation tool, not a sandbox. A malicious module that holds `Heap`
  can still exhaust it; the goal is *visibility and accounting*, not
  enforcement against an adversary inside the trust boundary.
- **Not** retroactive on the runtime. The C runtime (`runtime.c`,
  `bytecode_interp.c.stub`) is unchanged. The system is a static
  pre-pass; capability handles erase to nothing at AOT codegen time.

---

## 2. Effect Tag Catalogue

Effects are a closed (non-extensible-by-user, in v1) set of tags. They are
flat (no nesting, no parametrisation) and form a row in function types.

| Tag      | Meaning                                          | Triggering construct                                    |
|----------|--------------------------------------------------|---------------------------------------------------------|
| `!loop`  | Function contains a loop whose iteration count is not statically bounded by a `const` literal or a non-cyclic structural recursion on a finite data structure. | `while cond {}` / `loop {}` / `while true {}` / `for x in dynamic_array {}` where `len(dynamic_array)` is not a compile-time constant. |
| `!recurse` | Function calls itself (directly or via SCC) and the recursion is not provably structural (= decreasing on an algebraic data type). | Self-call detected by the existing `detect_unbounded_recursion` pass at `self/ai_native_pass.hexa:3539`. |
| `!alloc` | Function allocates on the heap an amount not bounded by a compile-time constant. | `[]`, `array_push`, `map[k] = v` resize, string concat in a loop. |
| `!io`   | Function reads or writes the filesystem or stdio. | `fs_read`, `fs_write`, `io_stdin`, `println`, `eprintln`, `io_pipe`, `read_file`, `write_file`, `__builtin_io_*`. |
| `!spawn` | Function executes a subprocess (`fork+exec` family). | `exec`, `exec_with_status`, `popen_lines`, `popen_lines_with_status`, `proc_run_with_stdin`, `process_spawn` (RFC-001). |
| `!net`  | Function opens a socket or sends/receives bytes over a transport not local to the process. | `net_listen`, `net_accept`, `net_connect`, `net_read`, `net_write`, `http_get`, `http_serve`, `__builtin_net_*`. |

Notes:

- `!loop` and `!recurse` are independent; a function can have both
  (`while true { recurse() }`).
- `!io` does **not** subsume `!alloc`; reading a file allocates the
  buffer but the latency profile and capability are different. They are
  recorded separately so a `Heap`-only caller can still reject a function
  that wants to allocate but does no I/O.
- `!spawn` implies `!io` (the spawned process inherits stdio) but the
  reverse is not true.
- A pure function has the empty effect row `{}` written `total` for
  brevity in signatures.

### 2.1 Effect ordering & subset semantics

A function with declared effect set `E` may be called only in a context
that admits a superset `E'` ⊇ `E`. This is the standard row-polymorphic
inclusion rule. There is no implicit promotion; missing capability is a
type error at the call site, not at the declaration site.

---

## 3. Capability Handles

Capabilities are **affine** values — they may be used at most once per
syntactic occurrence and are consumed by the effectful operation.
Re-binding (`let new_fuel = fuel.split(1000)`) produces a fresh handle and
invalidates the original (compile-time check, like Rust move).

| Handle  | Carried budget                                  | Consumed by                        | Refresh source                       |
|---------|-------------------------------------------------|------------------------------------|--------------------------------------|
| `Fuel`  | A non-negative integer count of "loop ticks" or "recursion frames". | every `!loop` iteration / `!recurse` call. | runtime root; `main` receives `Fuel::infinite()` if invoked under `--no-fuel`. |
| `Heap`  | A non-negative byte count.                      | every `!alloc` site (size approximated by allocator hint or actual size for `array_push`, `map_insert`, string concat). | runtime root; `Heap::from_rss_limit(env_var)`. |
| `Time`  | A monotonic-clock deadline (epoch_ns).           | every `!io`, `!spawn`, `!net` site reads `Time` and aborts if `now() > deadline`. | runtime root; can be subdivided (`time.split(ms: 500)`). |
| `Procs` | A non-negative integer count of subprocess slots and a non-negative integer count of socket slots. | `!spawn` consumes one process slot; `!net` consumes one socket slot. | runtime root; `Procs::default()` reads `ulimit -n / ulimit -u`. |

### 3.1 Linearity rule

For a handle `c: Cap`:

- `c.consume(n)` produces a fresh handle with `budget - n` and invalidates
  the input. The compiler tracks the latest live binding name in scope.
- `c.split(n)` produces `(c_left: Cap, c_right: Cap)` with disjoint
  budgets. Both are usable once each; the original is gone.
- `c.merge(c_other)` adds budgets, invalidating both inputs.
- A handle that escapes its lexical scope without being consumed (passed
  to a callee, returned, or merged) is a compile-time error
  ("dropped capability"). Exception: top-level `main` may drop its root
  handles (the runtime reclaims them on exit).

### 3.2 No reflection

Capability handles are not first-class values: they cannot be stored in a
heterogeneous container, serialised, or compared for equality. They exist
only as named bindings in the local scope. This keeps the analysis
intra-procedural plus an inter-procedural signature row, which is
tractable for the existing self-host typechecker.

---

## 4. Type System Changes

### 4.1 Function signatures

Old:

```hexa
fn http_get(url) -> string { ... }
```

New:

```hexa
fn http_get(url, t: &Time, p: &Procs) -> string !{io, net} { ... }
```

Reading rules:

- The effect row `!{io, net}` is a comma-separated set of tags after
  `->`. `!{}` or omission means **total**.
- `&Cap` denotes a borrowed capability handle. The borrow must live for
  the duration of the call. The callee may consume bytes/fuel from it
  but does not take ownership.
- `&mut Cap` (future extension) would allow the callee to call
  `split`/`merge` on the handle.

### 4.2 Caller obligation

At each call site the compiler:

1. Computes the union of effect tags reachable through that call.
2. Checks each tag is admitted by an in-scope capability of the
   correct kind (`!io` ⇒ requires `Time` in scope; `!alloc` ⇒ requires
   `Heap`; `!loop`/`!recurse` ⇒ requires `Fuel`; `!spawn`/`!net` ⇒
   requires `Procs`; `!io` always also requires `Time` so a stuck I/O
   has a deadline).
3. Inserts an implicit capability-pass for each required handle, sourced
   from the nearest in-scope binding of that type.

Mapping between effects and required handles:

| Effect    | Required handles |
|-----------|------------------|
| `!loop`   | `Fuel` |
| `!recurse`| `Fuel` |
| `!alloc`  | `Heap` |
| `!io`     | `Heap`, `Time` (read-buffer + deadline) |
| `!spawn`  | `Heap`, `Time`, `Procs` |
| `!net`    | `Heap`, `Time`, `Procs` |

### 4.3 Effect-row variables

For higher-order functions (e.g., `http_serve(addr, handler)`) the
handler's effect row must be expressible. Use a row variable `?E`:

```hexa
fn http_serve(addr, handler: fn(req) -> string !?E,
              t: &Time, p: &Procs) -> () !{io, net, loop, ?E} { ... }
```

The caller's effect row absorbs `?E` at unification. This is the only
place row polymorphism is needed in v1.

---

## 5. Inference Policy

### 5.1 Default: total

A function with no annotated effect row, no observed effectful call,
and no loop/recursion is inferred `total` (effect row `{}`). This is
the strongest claim; a single allocation in the body invalidates it.

### 5.2 Bottom-up SCC inference

The typechecker (`self/type_checker.hexa`) already does a fn-decl
walk. Effect inference adds:

1. Compute SCCs over the call graph (the existing
   `detect_unbounded_recursion` pass at `ai_native_pass.hexa:3539`
   already builds a self-call lookup; extend to a full SCC pass).
2. For each SCC, fixpoint-iterate the union of effects of all members.
3. Annotate the AST node in place. Output is consumed by
   `codegen_c2.hexa` (which strips capability args at AOT time — they
   are zero-cost — and by the interpreter for runtime budget tracking).

### 5.3 Loop / recursion detection

Reuse the existing analyses where possible:

- `while true { … }` → `!loop` unless the body provably `return`s on
  every path within ≤K iterations. The existing M274 single-iter
  elimination (`ai_native_pass.hexa:14912`) already detects the K=1
  case; raise the bound to K=`@bounded(N)` and treat anything else as
  `!loop`.
- `for x in expr { … }` → `!loop` unless `expr` is a literal array,
  range, or a value flowing from a `@bounded(N)` source.
- Self-recursion → `!recurse` unless the call argument is provably a
  structural-decreasing expression (single field projection on an ADT
  + variant exhaustiveness on the recursion site). This is a deliberate
  simplification of full termination checking — Coq/Agda-grade
  guard checking is out of scope for v1.

### 5.4 Allocation detection

Each AST node that returns a fresh array, map, or string can be
annotated by the parser. The typechecker reads this annotation. A
function whose body contains any such node, **unless** that node is
inside a branch that returns a constant (size-bounded by literal),
gets `!alloc`. String concat in a loop is the dominant trigger.

### 5.5 Builtin effect table

Builtin functions (`__builtin_*`, `exec`, `fs_read`, `net_*`, `printf`,
…) get effects from a hard-coded table maintained alongside
`self/runtime.hexa`. The table is the source of truth and is the
**only** place an effect can enter a fresh hexa program — every
user-level `!io` ultimately flows from a builtin.

---

## 6. Migration Strategy

The hexa self-host is ~50K lines of un-annotated code. Big-bang
migration is infeasible. Plan:

### 6.1 Phase M0 — silent inference (1 cycle)

- Land the inference pass with output to a `.effects.json` sidecar per
  module. No type errors raised. Goal: surface the population
  distribution.
- Acceptance: full `self/` corpus emits effects; CI artifact diffs
  effect rows across commits to catch surprise widening.

### 6.2 Phase M1 — annotation lint (1 cycle)

- For each fn whose inferred effect row is non-empty, the linter emits
  a fix-it that rewrites the signature. No errors yet.
- The user runs `hexa lint --fix-effects` and reviews the diff.
- Acceptance: stdlib (`self/std_*.hexa`) and self-host pipeline
  (`parser.hexa`, `type_checker.hexa`, `codegen*.hexa`) carry explicit
  annotations.

### 6.3 Phase M2 — capability obligation (2 cycles)

- Calls to effectful fns from a context that lacks the capability
  handle become a hard error.
- A `@legacy_effects` attribute on a fn opts out of the obligation
  check (capabilities passed implicitly from a process-wide root).
  This is the escape hatch for code that hasn't been threaded yet.
- Acceptance: `main` in every binary acquires root capabilities and
  threads them to top-level callees; `@legacy_effects` budget shrinks
  toward zero across cycles.

### 6.4 Phase M3 — strict mode (open-ended)

- `@legacy_effects` becomes a warning, then an error.
- Capabilities are mandatory, including in test files (a
  `Fuel::for_test(n)` constructor is provided).

### 6.5 Tooling

- `hexa effects show <fn>` prints the inferred row.
- `hexa effects diff <commit>` compares two rows for regression
  detection (used in CI).
- The LSP (`self/lsp.hexa`) surfaces effect rows on hover.

---

## 7. Performance Impact

Goal: zero runtime cost for code that compiles AOT with non-trivial
budgets known at compile time, near-zero for the interpreter.

### 7.1 AOT codegen

Capability handles are **erased** at codegen. They are static evidence,
not runtime values. Concretely (`codegen_c2.hexa`):

- `&Fuel`, `&Heap`, `&Time`, `&Procs` parameters: stripped from C
  signatures.
- `consume`/`split`/`merge` calls: emit nothing.
- The **only** runtime cost is when the user explicitly enables the
  `--meter` flag, which reinstates a thread-local counter
  decremented at each `!loop` iteration and `!recurse` call.

This matches Haskell's `IO` (zero cost; the type-level distinction
disappears at runtime) and Rust's `Send`/`Sync` (compile-time only).

### 7.2 Interpreter

The interpreter (`self/interpreter.hexa` is small but
`hexa_full.hexa` does the real work) already has per-step bookkeeping.
Adding a budget decrement is one integer operation per dispatched
opcode in the `!loop` / `!recurse` paths. Expected overhead: <2%
based on the existing `bench_nested_if_scale.hexa` measurements
(those benches already do ~1 instruction of bookkeeping per step).

### 7.3 Compile-time cost

The new pass is one bottom-up SCC traversal over the existing AST. The
typechecker is currently O(N) per fn after the 64-bucket cache fix
(`type_checker.hexa:78–98`); effect inference adds a constant per fn
plus the SCC computation, dominated by the existing call-graph build
in `ai_native_pass.hexa`. Expected overhead on typecheck wall time:
<5%.

### 7.4 Zero-cost claim caveats

- Capability **dropping** generates no runtime code but does require a
  compile-time data-flow analysis. For very large modules this could
  matter. Mitigation: the analysis is intra-procedural after fn
  signatures are known.
- The `?E` row variable on higher-order functions adds a constraint to
  the unifier. The current typechecker uses string-tag types
  (`type_checker.hexa:38`), so adding row variables means widening the
  type representation. This is the largest single risk to the "zero
  AOT cost" promise.

---

## 8. Prior Art

| System | Idea borrowed | Idea diverged |
|--------|---------------|---------------|
| **Koka** (Leijen, MSR) | Effect rows on function types; default-total inference; row polymorphism. | Koka has full algebraic-effect handlers with delimited continuations. We use plain labels (no handlers in v1). |
| **Eff** (Bauer & Pretnar) | Affine "resources" that mediate effect operations. | Eff resources are runtime-introspectable. Our capabilities are static evidence only. |
| **Haskell IO** | The "world token" model — one giant capability passed through every effectful fn. | Haskell collapses everything into `IO`. We split into `!io / !net / !spawn` so a network-only caller can reject filesystem access. |
| **Rust** `?Send`, `?Sync`, `unsafe fn` | Capability is a marker trait, erased at codegen. Auto-traits propagate bottom-up. | Rust has no first-class effect rows; "asyncness" is encoded via trait but recursive analysis is famously bad. We make recursion explicit. |
| **Frank** (Convent et al.) | "Ability" as a row of operations callable in the dynamic extent. | Frank ties abilities to handlers; we keep them as labels. |
| **WebAssembly Component Model** | Handles as resource references; linear/affine usage. | The component model is for cross-module isolation; our handles are intra-program budgeting. |
| **Ada/SPARK** Ravenscar profile | Static budget verification on tasks (`pragma Priority`, `pragma Storage_Size`). | Ravenscar bakes budgets into the schedule; we keep them as ordinary values. |

---

## 9. Examples — before / after

### 9.1 Pure leaf function — no change

Before:

```hexa
fn add(a: int, b: int) -> int {
    return a + b
}
```

After:

```hexa
fn add(a: int, b: int) -> int {
    return a + b
}
```

Inferred row: `total`. No capability needed.

### 9.2 Loop on dynamic input → `!loop` + `Fuel`

Before (`self/std_io.hexa` `io_write_bytes`, lines 35–45):

```hexa
fn io_write_bytes(path, bytes) {
    let s = ""
    let i = 0
    while i < len(bytes) {
        s = s + chr(bytes[i])
        i = i + 1
    }
    fs_write(path, s)
}
```

After:

```hexa
fn io_write_bytes(path, bytes, fuel: &Fuel, heap: &Heap, time: &Time)
        -> () !{loop, alloc, io} {
    let s = ""
    let i = 0
    while i < len(bytes) {
        // implicit: fuel.consume(1) per iteration
        // implicit: heap.consume(string-grow-cost) per concat
        s = s + chr(bytes[i])
        i = i + 1
    }
    fs_write(path, s)  // implicit: time.check_deadline(); heap.consume(buf)
}
```

### 9.3 HTTP serve loop → `!loop !io !net`

Before (`self/std_net.hexa:90`):

```hexa
fn http_serve(addr, handler) {
    let listener = net_listen(addr)
    while true {
        let conn = net_accept(listener)
        ...
    }
}
```

After:

```hexa
fn http_serve(addr, handler: fn(req) -> string !?E,
              fuel: &Fuel, heap: &Heap, time: &Time, procs: &Procs)
        -> () !{loop, io, net, alloc, ?E} {
    let listener = net_listen(addr, procs)
    while true {  // !loop, fuel.consume(1)
        let conn = net_accept(listener, procs)
        ...
    }
}
```

The handler's effect row `?E` flows up to the caller — a handler that
itself does I/O propagates that to whoever calls `http_serve`.

### 9.4 Subprocess spawn → `!spawn`

Before (`self/stdlib/proc.hexa:77`):

```hexa
fn popen_lines(cmd: string) -> [string] {
    let pair = popen_lines_with_status(cmd)
    let lines = pair[0]
    return lines
}
```

After:

```hexa
fn popen_lines(cmd: string,
               heap: &Heap, time: &Time, procs: &Procs)
        -> [string] !{spawn, io, alloc} {
    let pair = popen_lines_with_status(cmd, heap, time, procs)
    let lines = pair[0]
    return lines
}
```

### 9.5 Unbounded recursion → `!recurse`

Before (sketch — common shape in `self/parser.hexa`):

```hexa
fn parse_expr(state) {
    if peek(state) == "(" {
        consume(state)
        let inner = parse_expr(state)  // self-call
        ...
    }
    ...
}
```

After:

```hexa
fn parse_expr(state, fuel: &Fuel) -> AstNode !{recurse, alloc} {
    if peek(state) == "(" {
        consume(state)
        let inner = parse_expr(state, fuel)  // !recurse, fuel.consume(1)
        ...
    }
    ...
}
```

A pathological `((((...` input now fails with a typed `FuelExhausted`
instead of a stack overflow.

### 9.6 Total higher-order — preserved

Before (a hypothetical `map_array`):

```hexa
fn map_array(xs, f) {
    let mut out = []
    let i = 0
    while i < len(xs) {
        out.push(f(xs[i]))
        i = i + 1
    }
    return out
}
```

After:

```hexa
fn map_array(xs, f: fn(x) -> any !?E,
             fuel: &Fuel, heap: &Heap)
        -> [any] !{loop, alloc, ?E} {
    let mut out = []
    let i = 0
    while i < len(xs) {
        out.push(f(xs[i]))
        i = i + 1
    }
    return out
}
```

If `f` is total, `?E` collapses to `{}` and the call costs only the
loop and alloc effects. If `f` does I/O, the effect propagates.

### 9.7 Selftest entrypoint

Before (`self/stdlib/proc.hexa:198`):

```hexa
fn _selftest_main() {
    ...
    let r1 = popen_lines("echo a; echo b; echo c")
    ...
}
```

After:

```hexa
@entrypoint
fn _selftest_main() -> () !{loop, alloc, io, spawn} {
    let fuel = Fuel::for_test(10000)
    let heap = Heap::for_test(64 * 1024 * 1024)
    let time = Time::deadline_in_ms(5000)
    let procs = Procs::for_test(processes: 8, sockets: 0)
    let r1 = popen_lines("echo a; echo b; echo c",
                         &heap, &time, &procs)
    ...
}
```

The `@entrypoint` attribute exempts the fn from "drop capability" check
because the runtime reclaims root handles on exit.

---

## 10. Five existing self-host effect cases

Concrete sites in the current codebase that the new system flags. These
serve as the migration plan's first batch.

### 10.1 `http_serve` — `!loop !io !net !alloc`

File: `self/std_net.hexa:90` (`while true { net_accept(listener); ... }`).
The unbounded `while true` plus the network call is the canonical
runaway. Today nothing limits this; `Ctrl-C` is the only termination.
After migration: requires `Fuel + Time + Procs` and a deadline.

### 10.2 `popen_lines` family — `!spawn !io !alloc`

File: `self/stdlib/proc.hexa:50-185`.
Multiple wrappers around `exec_with_status` that spill to `/tmp`,
spawn `/bin/sh -c`, and merge stderr. Today these are unbudgeted and
have produced runaway `find` / `grep` invocations during indexing
(observed in hive coding-agent regressions). After migration: each
call costs one process slot from `Procs` and a deadline check on
`Time`; the temp-file write costs `Heap`.

### 10.3 `parse_expr` recursive descent — `!recurse !alloc`

File: `self/parser.hexa` (4610 lines, recursive descent over the AST).
The existing `detect_unbounded_recursion` (`ai_native_pass.hexa:3539`)
already flags this as recursive. Today the only guard is the OS stack;
deeply nested input crashes the parser. After migration: `Fuel`
budget enforced at each self-call.

### 10.4 Type checker scope walks — `!loop`

File: `self/type_checker.hexa:194-245` (the `tc_lookup` fallback walk
plus the `scope_cache` rebuild walks). These are O(N) loops on
dynamic-size arrays. Pre-cache fix they were the N² blowup root cause
(the comment at lines 78-98 documents this). After migration: `Fuel`
budget catches the regression statically — any caller without enough
fuel can't invoke a deeply-nested-scope program through the checker.

### 10.5 `proc_run_with_stdin` temp-file dance — `!spawn !io !alloc`

File: `self/stdlib/proc.hexa:133-140`.
Writes payload to `/tmp/hexa_proc_stdin_<nonce>.txt`, execs `cmd <
tmpfile`, unlinks. Each call: 1 fs write, 1 process spawn, 1 fs
unlink. Today this is invisible in the type signature. After
migration: `&Heap`, `&Time`, `&Procs` are all required, and the
caller's budget makes the cost explicit (callers in qmirror that
loop over QASM circuits can no longer accidentally spawn 10K Python
subprocesses without budgeting).

---

## 11. Open Questions / Risks

### 11.1 Open

- **Q1 — String type representation.** The current typechecker uses
  string tags (`type_checker.hexa:38`) for types. Effect rows are
  multi-element, so storing them as `"!{io,net,alloc}"` strings is
  feasible but lossy for unification. **TODO**: decide whether to
  widen the type representation to a struct with a `tags: [string]`
  field, or pun on a sorted-string canonical form.

- **Q2 — `Time` granularity.** Should `Time` be a deadline (epoch_ns)
  or a duration budget? Deadline composes worse across nested calls
  (the inner deadline is min(outer.deadline, now + budget)); duration
  is harder to enforce statically. **TODO**: prototype both on
  `popen_lines`.

- **Q3 — `Heap` accounting unit.** Bytes are honest but require the
  allocator to report sizes. The current runtime (`runtime.c`) uses
  refcounted arenas; reporting per-call bytes is non-trivial. Coarser
  unit (allocations, not bytes) is cheaper but lets a single huge
  string burn the budget for free. **TODO**: design a
  `@alloc_size_hint(n)` attribute for builtins.

- **Q4 — Exception interaction.** `throw` inside a `!loop` is
  currently the only way to break out without `return`. Does `throw`
  consume `Fuel`? **TODO**: spec semantics.

- **Q5 — Macros and comptime.** `@comptime` blocks (`self/comptime.hexa`)
  evaluate at compile time. Effects there are about *compile* runaway,
  not runtime runaway. **TODO**: design a separate `Fuel` for
  comptime evaluation, sourced from the build system.

- **Q6 — Effect row ordering.** Sets vs. sorted lists: the current
  proposal is unordered set, but printing and hashing want a
  canonical order. **TODO**: define lexicographic order
  (`!{alloc, io, loop, net, recurse, spawn}`) and bake into the
  formatter.

### 11.2 Risks

- **R1 — Migration tax.** Even with `@legacy_effects`, the corpus is
  ~50K lines and threading capabilities everywhere is invasive.
  Mitigation: ship M0 silent inference first; let the population
  shape reveal which fns are leaves vs. effect-bearing.

- **R2 — Higher-order effect plumbing.** `?E` row variables on
  callbacks (e.g., `http_serve(handler)`) cascade up the call graph.
  Without good error messages this becomes the next ergonomics crisis
  (cf. Rust async Send-bound errors, Haskell `MonadIO m =>` clutter).
  Mitigation: dedicated diagnostic that points at the deepest effect
  source, not just the unification site.

- **R3 — False positives on `!loop`.** A `for x in [1,2,3]` literal
  is statically bounded but the analysis must know that. Conservative
  inference will flag too many fns as `!loop`, requiring `Fuel`
  threading where none is needed. Mitigation: cheap "literal-array
  source" analysis as a precondition to the `!loop` decision.

- **R4 — Two parsers, three SSOTs.** Per project memory: hexa-lang has
  parser duplication across multiple files. Effect annotation
  syntax must land in **all** parsers simultaneously or migrations
  diverge. Mitigation: add a parser conformance test that round-trips
  effect-annotated signatures through every parser before M1 lands.

- **R5 — Interpreter vs. AOT collision detection.** Per project memory,
  AOT renames colliding identifiers but the interpreter silently
  uses last-def-wins. Capability handles are bound by name; if the
  interpreter ever shadows a capability binding the budget is silently
  reset. Mitigation: bake handle uniqueness into the typechecker
  (the affine check), not into runtime semantics — the interpreter
  doesn't need to enforce affinity if it has already been verified.

- **R6 — `@legacy_effects` becoming permanent.** Every escape hatch
  accumulates. Mitigation: M3 hard-errors `@legacy_effects` at a
  date-fenced compile time, no exceptions.

---

## 12. Next Steps Recommendation

1. **Land M0 silent inference** as the very next concrete milestone.
   Output a `.effects.json` per module; do not raise errors.
   Smallest possible PR; it gives us the population distribution
   without any migration cost. Worktree: `feat/effect-infer-silent`.

2. **Prototype `Time` budget on `popen_lines`** as a
   capability-handle proof-of-concept. Single fn, two callers
   (selftest + qmirror bridge), well-bounded scope. This validates
   Q2 (deadline vs. duration) before the spec freezes.

3. **Author a parser-conformance test** for effect-annotated
   signatures across every parser SSOT in the repo (per project
   memory: 3 parser locations). Without this, R4 fires on the first
   signature change.

4. **File RFCs for Q1, Q2, Q3 individually** under
   `proposals/rfc_016*` … `rfc_018*`. Each is a small, decidable
   design choice and benefits from pre-implementation review.

5. **Rebuild `detect_unbounded_recursion` into a full SCC pass** as
   prep for M0. The existing pass at `ai_native_pass.hexa:3539` only
   detects direct self-calls; the real graph has mutual-recursion
   chains (`parser.hexa` parse_expr ↔ parse_call ↔ parse_arglist).

Each item is independently mergeable and produces an artifact (a
sidecar file, an RFC, a test suite, a refactored pass). None of them
require the whole effect-system spec to be frozen first; they
progressively de-risk the design.

---

## Appendix A — File pointers

- Typechecker (target of effect inference):
  `self/type_checker.hexa` (1867 lines).
- Existing recursion detector to extend:
  `self/ai_native_pass.hexa:3539` (`detect_unbounded_recursion`).
- AOT codegen (target of capability erasure):
  `self/codegen_c2.hexa`, `self/codegen_c.hexa`,
  `self/codegen_native.hexa`.
- Builtin effect-table source-of-truth (to be created):
  `self/runtime.hexa` adjacent to existing builtin
  registration.
- Net stdlib: `self/std_net.hexa` (153 lines).
- Proc stdlib: `self/stdlib/proc.hexa` (267 lines).
- IO stdlib: `self/std_io.hexa` (91 lines).
- LSP surface for hover: `self/lsp.hexa`.

## Appendix B — Glossary

- **Total** — pure + terminating + non-allocating + no I/O. Empty
  effect row `!{}`.
- **Pure** — no externally-visible side effects, but may allocate or
  loop. Effect row ⊆ `!{loop, alloc, recurse}`.
- **Effect row** — a set of effect tags attached to a function type.
- **Capability handle** — an affine value that grants the right to
  perform an effect and meters its consumption.
- **Runaway** — any unbounded resource consumption: infinite loop,
  unbounded recursion, unbounded allocation, hung I/O, fork-bomb,
  socket flood.
- **Erased / zero-cost** — present in the type system, absent at
  runtime. Capability handles erase at AOT codegen.
