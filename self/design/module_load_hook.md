# Module Load Hook — Layer 2 of 3-layer law enforcement (#29)

**Status**: design only (stage0 seed-freeze active). Wire-up deferred until
`project_hexa_v2_circular_rebuild_20260417` is resolved. DO NOT patch
`self/codegen_c2.hexa` in this session.

## Goal

When the compiler sees `use "foo"` / `import "foo"`, run
`module_gate_check(foo.hexa)` **before** reading / parsing the file.
Violations → `eprintln` + `exit(1)` (fail-closed).

## Layering

| Layer | File                              | Scope                 |
| ----- | --------------------------------- | --------------------- |
| L0    | `.git/hooks/pre-commit`           | VCS safety net (live) |
| L1    | `self/core/law_io.hexa`         | runtime write gate    |
| **L2** | `self/core/module_gate.hexa`   | **load-time gate**    |

L2 calls into L1's `law_check(path, content)` on the *source bytes* the
compiler is about to consume — blocking forbidden extensions, temp-suffix
paths, and (once scanners are wired) raw rule violations before a single
AST node is built.

## Pinpoint — where to wire, down to file:line

### 1. `self/codegen_c2.hexa` — two sites, mirror-identical

Both `gen2_program` and its partner dispatcher handle `ImportStmt`/`UseStmt`
today via `_resolve_use_emit(name)`. Hook goes in `_resolve_use_emit`
itself so *every* caller inherits it.

- **self/codegen_c2.hexa:410** `fn _resolve_use_emit(name) {`
  - after `let path = _resolve_use_path(name)` (line 415)
  - before `let src = read_file(path)` (line 420)
  - Insert:

    ```
    if len(path) > 0 {
        if !module_gate_check(path) {
            eprintln("[codegen_c2] module_gate refused: " + path)
            exit(1)
        }
    }
    ```

- **self/codegen_c2.hexa:599** `else if k == "ImportStmt" || k == "UseStmt" {`
- **self/codegen_c2.hexa:4443** second branch (mirror)
  Both already funnel through `_resolve_use_emit` — no edit needed here
  once the hook is inside the resolver. Listed for audit trail only.

### 2. `self/interpreter.hexa` — no current UseStmt arm

`grep UseStmt self/interpreter.hexa` → 0 matches. The interpreter relies on
`tool/flatten_imports.hexa` as a preprocessing step (all `use` lines are
inlined before interpretation). Two options when L2 goes live:

- **Option A** (minimal): leave interpreter alone — flatten_imports is
  already the load gate; add `module_gate_check` inside
  `tool/flatten_imports.hexa` at its file-read site.
- **Option B** (true runtime gate): add a dedicated UseStmt arm to the
  interpreter's stmt dispatcher that calls `module_gate_check` + dynamic
  parse. Requires parser/AST reentrancy — larger change.

Option A is recommended for Layer 2 v1.

### 3. `self/native/hexa_v2` / `hexa_cc.c`

**DO NOT TOUCH** (HX8 seed-freeze + `.c` purge policy). Once
`codegen_c2.hexa` is re-bootable, rebuilding stage0 via
`tool/rebuild_stage0.hexa` regenerates the native path automatically.

## Dependency

- `self/core/law_io.hexa::law_check(path, content) -> [bool, reason]`
  (Layer 1, already live)
- `self/core/module_gate.hexa::module_gate_check(path) -> bool`
  (this patch — reads file, delegates to `law_check`, eprintln+exit(1)
  on violation)

## Next session checklist

1. Verify `hexa_v2 circular rebuild regression` is closed
   (see `project_hexa_v2_circular_rebuild_20260417`).
2. Add the 5-line hook block at `codegen_c2.hexa:~420`.
3. Add `use "stdlib/module_gate"` at codegen_c2's top-level imports block.
4. Run `tool/rebuild_stage0.hexa` (normal, not `FORCE=1`).
5. Exercise `self/test/test_module_gate.hexa` → expect clean pass + fail-closed on violation case.
