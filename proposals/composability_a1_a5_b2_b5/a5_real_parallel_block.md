# A5 — Real `parallel { }` block + worker pool

- **Status**: spec proposed (additive, no migration)
- **Date**: 2026-05-02
- **Bundle**: composability A1+A5+B2+B5 (BG-HX2)
- **Cross-refs**:
  - precedent (fn-attr): `build/__test_par.hexa`, `build/__test_par_red.hexa`, `build/__test_par_nest.hexa` — `@parallel fn ...`
  - precedent (DAG scheduler): `tool/parallel_scheduler.hexa` — sequential exec emulation w/ slot semantics
  - precedent (closure_v3_composable): no on-disk artifact yet; user-provided baseline name for the existing pattern

## §0 TL;DR

`@parallel fn <name>()` exists today as a **fn-level attribute** (see `build/__test_par.hexa`)
that under the current runtime executes **sequentially** (declarative-only marker, no
worker pool). The DAG scheduler `tool/parallel_scheduler.hexa` provides real concurrency
but at the *task* (process spawn) granularity, not the *expression* granularity.

This RFC introduces a **block-level** `parallel { ... }` form that:
1. Schedules each top-level statement in the block as an independent unit on a worker
   pool.
2. Joins at block exit (deterministic completion before any subsequent statement).
3. Reduces to sequential exec on platforms / configs without a worker pool (graceful
   degradation, byte-identical observable behavior except wall-time).

The fn-attr `@parallel fn` is **NOT migrated** — it stays as a hint for whole-fn
parallelization (loop-level), which is a separate axis.

## §1 Current behavior (baseline)

```hexa
// Form 1: fn-attr (sequential exec emulation today)
@parallel fn par_sum() {
    let mut total = 0
    for i in 0..100 { total = total + i }
    return total
}
println(par_sum())                       // sequential, ~instant

// Form 2: DAG scheduler (real concurrency at process granularity)
//   $ hexa tool/parallel_scheduler.hexa run manifest.jsonl
//   ↳ spawns N child hexa processes per ready frontier
```

No expression-level parallel form exists.

## §2 Proposed contract (additive)

```hexa
parallel {
    let a = expensive_a()
    let b = expensive_b()
    let c = expensive_c()
}
println(a + b + c)
```

**Semantics**:
- Each top-level let-binding (or expression statement) inside the block is a
  parallel **unit**.
- Units execute concurrently on a worker pool of size `HEXA_PARALLEL_WORKERS`
  (default: detected core count, capped at 8; `1` forces sequential degradation).
- Block exit is a join barrier — no statement after the closing `}` executes until
  every unit completes.
- Bindings (`let`) inside the block ARE visible after the block (lexical scope of
  the surrounding fn), so `a + b + c` outside the block is well-defined.
- Order of side-effects within the block (println, file write, etc.) is
  **non-deterministic** — caller responsibility.
- Order of side-effects relative to before/after the block is deterministic
  (happens-before via join barrier).

**Reductions** (optional sugar, deferred to v1.1):

```hexa
let total = parallel reduce(+, 0) {
    for i in 0..1000 { yield i * i }
}
```

OUT OF SCOPE for v1; tracked as future extension.

**Worker pool ABI**:
- Single global pool per process, lazy-initialized on first `parallel { }` entry.
- Pool size from env `HEXA_PARALLEL_WORKERS` at first init (cached for process lifetime).
- Threads are OS threads (pthread on darwin/linux); no green-thread runtime required.

## §3 Compatibility

- `@parallel fn` — unchanged. Stays as fn-level hint.
- DAG scheduler (`tool/parallel_scheduler.hexa`) — unchanged. Stays as inter-process orchestration.
- Existing single-thread programs — unchanged (no `parallel { }` keyword usage = no pool init).
- Worker pool init failure (e.g. pthread_create returns ENOMEM) → graceful fallback to
  sequential exec + warn on stderr. **Never aborts the program.**

## §4 Impl plan (additive, no migration)

**Lexer side** (`self/lexer.hexa`):
- Add keyword `parallel` (currently might be a soft-keyword via `@parallel`; promote
  to block-position keyword while keeping `@parallel` as attr-form).
- Effort: ~10 LoC.

**Parser side** (`self/parser.hexa`):
- Recognize `parallel { <stmts> }` as a block expression (similar to `do { ... }` if any, else novel form).
- Each top-level stmt inside becomes a `ParallelUnit` AST node.
- Effort: ~60 LoC.

**Codegen side** (`self/codegen_c2.hexa`):
- Lowering rule: `parallel { s1; s2; s3 }` →
  ```
  hexa_parallel_pool_t *p = hexa_pool_get_global();
  hexa_task_t t1 = hexa_pool_submit(p, lambda_for_s1);
  hexa_task_t t2 = hexa_pool_submit(p, lambda_for_s2);
  hexa_task_t t3 = hexa_pool_submit(p, lambda_for_s3);
  hexa_pool_join(p, t1); hexa_pool_join(p, t2); hexa_pool_join(p, t3);
  // s1/s2/s3 bindings (let a / let b / let c) hoisted to outer scope
  ```
- Each lambda captures the surrounding scope by closure (already supported by closure ABI).
- Effort: ~120 LoC.

**Runtime side** (`runtime.c` + new `runtime_pool.c`):
- New file `runtime_pool.c` implementing:
  - `hexa_pool_get_global()` — lazy-init; reads `HEXA_PARALLEL_WORKERS`.
  - `hexa_pool_submit(pool, fn, env)` — push to work-queue; returns task handle.
  - `hexa_pool_join(pool, task)` — block until task done; returns task result.
  - Worker loop: pop work, exec, signal completion.
- pthread + condvar only; no external dependency.
- Fallback: if `HEXA_PARALLEL_WORKERS=1` or pthread_create fails, all submit/join calls
  exec inline.
- Effort: ~250 LoC for runtime_pool.c.

**Interpreter side** (`self/interp.hexa`):
- Sequential degradation: `parallel { s1; s2; s3 }` executes s1, s2, s3 in order
  (no worker pool in interpreter to keep determinism). Observable behavior identical
  to AOT minus wall-time.
- Effort: ~30 LoC (parse + sequential dispatch).

**Total estimated impl scope**: ~470 LoC across 5 files.

## §5 Falsifiers (raw#71)

1. After `parallel { let a = f1(); let b = f2() }`, reading `a` or `b` returns wrong
   value (e.g. uninitialized) → fail (join barrier broken).
2. `parallel { for i in 0..3 { exec("sleep 1") } }` wall-time ≥ 3s with `HEXA_PARALLEL_WORKERS=4`
   → fail (sequential exec, no real parallelism).
3. With `HEXA_PARALLEL_WORKERS=1`, output of any `parallel { }` block differs from
   sequential exec of the same statements → fail (sequential degradation broken).
4. Pool init failure crashes program instead of warn+fallback → fail (robustness gate).
5. `parallel { let a = 1; let a = 2 }` (duplicate binding inside block) silently picks
   one → fail (must error at parse/typecheck time).

## §6 Selftest fixture

See `proposals/composability_a1_a5_b2_b5/fixtures/a5_parallel_block.hexa` —
6 cases covering join barrier, scope hoisting, sequential degradation, worker pool
init, side-effect non-determinism (within), determinism (across).

## §7 raw#10 caveats

1. Worker pool is **per-process** — multiple `parallel { }` blocks in one program
   share one pool. Nested `parallel { parallel { ... } }` is allowed but inner block
   competes with outer for workers (no work-stealing in v1).
2. `parallel reduce(...)` form is NOT in v1; v1 = unordered statement-level only.
3. Side-effects within a parallel block (println, file writes) interleave
   non-deterministically — fixture asserts only on join semantics, not output order.
4. Lazy pool init means the first `parallel { }` block has additional latency
   (~1ms for pthread_create × N workers). Acceptable; not a regression on
   programs that don't use `parallel { }`.
5. Interpreter executes sequentially — wall-time benefit only on AOT. This is
   intentional for selftest determinism; documented explicitly.
6. `HEXA_PARALLEL_WORKERS=0` is undefined; treat as `1` (sequential).
7. No cancellation semantics — once submitted, a task runs to completion. Errors
   inside a task surface at join time (re-raised on the calling thread).
8. Mac-local strict; pthread is darwin-native. No GPU offload.
9. The user-provided baseline name `closure_v3_composable parallel mode = sequential
   exec emulation` does NOT correspond to any currently disk-resident artifact;
   treated as conceptual baseline (the @parallel fn-attr in `build/__test_par*.hexa`
   plays the same role).

## §8 Effort + retire

- Effort: spec ~3h. Impl ~3-4 days (runtime_pool.c is the gate).
- Retire when: §6 fixture passes interp+AOT for 4 sessions AND a real-world consumer
  (candidate: `tool/parallel_scheduler.hexa` orchestrator stage 3) opts in to the new
  block form.
