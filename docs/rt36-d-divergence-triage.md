# rt36-d VM/Interpreter Divergence Triage

Date: 2026-04-15
Scope: Triage only — no fixes in this note.
Handoff target: Follow-up session(s) fixing each divergence in order below.

## Baseline

Confirmed on HEAD `4acc68c` (T53 perf landing). rt36-d regression suite has
8 tests; 5 match between interpreter (default) and VM (`HEXA_VM=1`) modes,
3 diverge. Divergences predate rt#36-E closure work — they are bugs in
interpreter/VM semantics, not closure machinery.

| Test | Interpreter Output | VM Output | "Correct" Mode |
|------|--------------------|-----------|----------------|
| `mut_mvp.hexa` | `2 5 1 2 void v x` | `99 5 1 2 7 v x` | VM (per test L4) |
| `try_mvp.hexa` | `caught: {message: bang, trace: ...}` | `caught: bang` | VM (per test L5-14) |
| `try_uncaught.hexa` | `error: oops` | `error: bc_vm trap: uncaught throw: oops` | interpreter (cleaner) |

## Per-divergence findings

### 1. `mut_mvp.hexa` — interpreter drops `arr[i] = v` assignments

**Root cause**: `self/interpreter.hexa:2927-2944` `AssignStmt` branch handles
only `Ident` and `Field` LHS. `Index` LHS silently no-ops. The `NK_ASSIGN`
*expression* branch at `self/interpreter.hexa:2104-2134` DOES handle `Index`
(for both arrays and maps). So the statement-path lags the expression-path.

**VM side (correct)**: `self/bc_emitter.hexa:2881-2890` lowers
`AssignStmt{Index}` into `IndexAssignStmt` and emits `OP_ARR_SET` / map-set.

**Fix**: port the `Index` sub-block from `interpreter.hexa:2109-2133` into the
`AssignStmt` branch at line 2927. Same fix must land in `self/hexa_full.hexa`
(dual-SSOT parity).

**Scope**: trivial, ~1h. No opcode changes. Two files.

### 2. `try_mvp.hexa` — interpreter over-wraps thrown strings

**Root cause**: `self/interpreter.hexa:2762-2790` `exec_throw` wraps any
`TAG_STR` thrown value into a `{message, trace}` map with
`format_stack_trace()`. When the catch block does `"caught: " + e`, the map
stringifies as `{message: bang, trace: Stack trace:}`.

**VM side (correct per test header)**: `self/bc_vm.hexa:1546-1578`
`OP_THROW` pushes the raw value; `STORE_LOCAL` in the catch binds `e = "bang"`
directly. Test header at `try_mvp.hexa:5-14` explicitly declares VM output
as the expected form.

**Fix options**:
- **(b) recommended** — strip interpreter's string-wrapping in `exec_throw`.
  Preserve struct-wrap only for `throw Error { ... }` patterns (structured
  exceptions). Simpler. Must audit existing tests that may rely on
  `e.message` access on caught *string* exception.
- (a) rejected — teach VM to wrap. Requires `format_stack_trace` + map
  construction in bytecode. Heavy and duplicates interpreter logic.

**Scope**: medium, ~2-3h with option (b). Add a grep sweep for tests that
access `.message` on caught values to scope regressions.

### 3. `try_uncaught.hexa` — VM adds redundant `bc_vm trap:` prefix

**Root cause**: Two layers stack a prefix:
- `self/bc_vm.hexa:1552` sets reason to `"uncaught throw: " + to_string(th_val)`
- `self/bc_emitter.hexa:3119-3121` top-level handler unconditionally prefixes
  `"error: bc_vm trap: "` around the reason. Result: `error: bc_vm trap: uncaught throw: oops`.

Interpreter at `self/interpreter.hexa:10867-10882` just prints `"error: " + msg`.

**"Correct" mode**: interpreter (no explicit expected output in test file, but
the interpreter's `error: <msg>` form matches the "visible diagnostic" intent
documented at `interpreter.hexa:10862-10866`).

**Fix**: single-line change in `self/bc_emitter.hexa:3120`. Special-case the
`uncaught throw:` reason class — drop the `bc_vm trap: ` prefix only for that
class. Keep `bc_vm trap:` for other reasons (unimplemented opcode, stack
corruption, etc.) where the label is useful.

**Scope**: trivial, ~30min. One-file, one-line.

## Recommended fix order

1. **try_uncaught** (30min) — smallest, zero-risk, one-line.
2. **mut_mvp** (1h) — port existing expr-path logic into stmt-path, dual-file.
3. **try_mvp** (2-3h) — needs test audit first to scope the break-surface.

Combined: ~4h for all three with option (b) on try_mvp.

## Landing requirements

**CRITICAL — stage0 rebuild**: `build/hexa_stage0` is a pre-compiled Mach-O
that bundles `.hexa` sources at build time (`scripts/build_stage0.sh`). Source
edits have NO runtime effect without rebuild. Any fix session must include
the rebuild, and the rebuild must not break other regressions (see
hexalang_vb1_fix_2026-04-14 `do_not_actions` for the baseline-binary
protection).

After stage0 rebuild, re-run:
```bash
for f in examples/regressions/rt36-d/*.hexa; do
  i=$(./build/hexa_stage0 "$f" 2>/dev/null | tr -d '\n')
  v=$(HEXA_VM=1 ./build/hexa_stage0 "$f" 2>/dev/null | tr -d '\n')
  [ "$i" = "$v" ] && echo "✓ $(basename $f)" || echo "✗ $(basename $f) DIVERGE"
done
```
Target: 8/8 match.
