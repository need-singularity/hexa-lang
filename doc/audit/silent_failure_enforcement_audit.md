# Silent-failure enforcement audit (2026-04-26)

Survey of existing enforcement against the 8 silent-failure classes surfaced by today's perf-32 + 7-agent cluster session. Each class is rated against the layered enforcement ladder (parser/compile-time → codegen → runtime → repo-lint → none).

## Summary table

| # | Class | Existing enforcement | State | Gap | Proposed |
|---|---|---|---|---|---|
| 1 | `fn main()` + explicit `main()` → double-invoke | `codegen_c2.hexa` T34 (line 848-858, 940) suppresses auto-call when ExprStmt `main()` detected. AOT path correct. **Interp `hexa_full.hexa::T49` (line 18289-18304) deliberately double-calls — comment says "user's responsibility".** | PARTIAL | Interp + bridge silently runs body twice (witness: `hexa_full self/_bug1_main_double.hexa` prints `X` twice; `./hexa` prints once). Codegen suppresses; interp doesn't. No diagnostic in either path. | **Stage B SHIPPED** — Parser hard-fail when `fn main` decl + top-level `main()` call coexist (without `@manual_main` attr opt-out). Catches both AOT and interp at the front. |
| 2 | `use "X"` symbol referenced but body never linked → silent extern + interp fallback | `module_loader.hexa::ml_iter_walk` flattens transitively when AOT triggered (commit 72eb8367). On clang link failure, `aot_build_slot()` returns `""` → `cmd_run` (main.hexa line 1815-1825) falls through to interp. **No diagnostic; no exit non-zero.** | NOT ENFORCED | clang `Undefined symbols for architecture` text is in `compile` capture (main.hexa line 1715) but never inspected — discarded. Build succeeds-with-different-binary or silently slows by 5-10x via interp fallback. | DEFERRED — coordinate with hexa_v2/arena agent (commit 02af1622). Mechanism: scan `compile` stderr for `Undefined symbols` in `aot_build_slot` and propagate fatal sentinel. |
| 3 | `#!hexa` shebang mid-file (after `module_loader` inlines) → transpiler chokes | `module_loader.hexa::ml_strip_and_clean` (line 333-348) strips first-line `#!` from inlined sub-files. **Lexer (line 138-145) only skips shebangs at file-start.** Mid-file `#!hexa strict` lexes as `#` (skipped) + `!` + `hexa` ident → cryptic parse error. | PARTIAL | Loader path safe. Hand-written `.hexa` with stray mid-file `#!hexa` (e.g. from bad `sed`) errors with "unexpected token" in random place. No location-aware diagnostic. | **Stage B SHIPPED** — Lexer: when seeing `#!` mid-file, emit explicit error "shebang must be on line 1; remove or move to top of file". Single-pass detection (we already track `line` counter). |
| 4 | `json_parse` throws on non-JSON instead of `Result/Option` | `runtime.c::hexa_json_parse` (line 6855) returns `hexa_void()` on non-string input but the inner `_jp_parse_value` falls through `to_int` → throws on bare tokens. `core/session/store.hexa` workaround = `_safe_parse_line` try/catch wrapper. raw#10 silent_error lint catches empty `catch` blocks but doesn't require Result wrappers. | NOT ENFORCED | Type system has no Result/Option discipline; runtime API is throw-by-default. Per-callsite try/catch is the only correct usage; nothing checks for bare `json_parse(...)` calls. | Medium-term: parser/lint warn on bare `json_parse(...)` not wrapped in try/catch, OR runtime API rename `json_parse → json_parse_strict` + `json_try_parse` returning `void` on failure. Defer to Stage D — type-system change. |
| 5 | Tag-mismatch runtime errors (e.g. `to_int` on string) instead of compile-time tag inference | `runtime.c::__hx_to_int` throws `not an integer` at runtime. Codegen has STRUCTURAL-2/3 (codegen_c2 line 814-823) tracks `_known_int`/`_known_float` per global — partial inference. No per-callsite tag check. | PARTIAL | Tag tracking exists for `_is_int_init_expr`/`_is_float_init_expr` constants but doesn't propagate through expressions or function returns. `to_int(s)` where `s` is a known-string variable still compiles. | Hard. Defer — needs proper ANF + tag-flow analysis pass. Tracked in `project_hexa_perf32_aot_use_resolution.md` follow-ups. |
| 6 | Substring length off-by-one vs literal | None. Runtime `substring(start, end)` returns truncated string silently if `end > len`. Test failure manifests as wrong assertion. | NOT ENFORCED | Compile-time string-length proof would need const-fold + length tracking on `String` type. | Hard — tracked but deferred. Could add a runtime warning when substring(end) reaches len (might over-fire on intentional clamping). |
| 7 | Cross-file rename leaves stale references (stage0→interp) | `tool/audit_shim_ssot.hexa` (hexa-lang) catches stage0 mutex restoration (line 73-74). `raw 8 stage0/stage1 retire` (project_raw8_stage_token_retire.md) is a CLI-lint policy. | PARTIAL | After-the-fact lint, not parser/compile-time. Detects only the specific known-bad pattern. | Generic "unknown symbol referenced through `use`" lint or compile-time strict mode. Defer — already covered by `audit_shim_ssot` for the recurring case. |
| 8 | Gitignored build artifact drifts from SSOT generator | `tool/audit_shim_ssot.hexa` (same as 7) catches the design pattern. raw#65 operation-idempotency requires re-runnable tools. | PARTIAL | Per-tool idempotence check via raw#65; no automatic SSOT↔artifact diff. | Ship `tool/check_ssot_sync.hexa` (already exists in hexa-lang/tool — verify wired to staged commits). Same defer reasoning as 7. |

## Coverage map

```
Class | parser | codegen | runtime | repo-lint | OS-level
1     |   ✓ NEW (parser+interp)  |   ✓ AOT (T34) |   .    |    .   |    .
2     |   .    |   .     |   .     |     .     |    .       (silent fallback to interp)
3     |   ✓ NEW (lexer+interp lexer) |   .   |   .   |   .   |    .
4     |   .    |   .     |   ⚠ throws (T34 stderr msg) |   raw#10 partial (catch only)  |    .
5     |   .    |   ⚠ partial (consts only) |   ⚠ throws |   .   |    .
6     |   .    |   .     |   ✗ silent truncate |   .   |    .
7     |   .    |   .     |   .     |   ✓ audit_shim_ssot + raw 8 |    .
8     |   .    |   .     |   .     |   ✓ audit_shim_ssot + raw#65 |    .
```

## Stage B targets (this session — Class 1 + 3 SHIPPED)

Ranked by (gap severity × implementation ease):

1. **Class 3 — mid-file shebang lexer diagnostic** (15 LOC, lexer.hexa). Witness file: `_bug3_midfile_shebang.hexa` — exit 1 with line+col reported. SHIPPED in self/lexer.hexa + self/hexa_full.hexa (both lexer paths).
2. **Class 1 — parser hard-fail on `fn main` + `main()` coexistence** (45 LOC, parser.hexa). Witness file: `_bug1_main_double.hexa` (existed) → exit 1; opt-out witness `_bug1_main_double_optout.hexa` (`@manual_main` attr) → runs as before. SHIPPED in self/parser.hexa + self/hexa_full.hexa (both parsers).
3. **Class 2 — `aot_build_slot` undefined-symbol fatal** — DEFERRED. Coordinate with parallel hexa_v2 / arena agent (commit 02af1622) — surgical addition only. Mechanism: scan clang stderr in main.hexa::aot_build_slot() ~line 1715 for `Undefined symbols` and propagate fatal sentinel that cmd_run distinguishes from "not cached".

Classes 4-8 deferred — see comments per row.

## Source-of-truth mapping

| Class | File:line | Comment-tag |
|---|---|---|
| 1 codegen | `self/codegen_c2.hexa:848-858, 940` | `T34` |
| 1 interp | `self/hexa_full.hexa:18289-18304` | `T49` |
| 1 NEW parser AOT | `self/parser.hexa::_check_main_double_invoke` (post-parse) | `silent-failure-enforcement Class 1` |
| 1 NEW parser interp | `self/hexa_full.hexa::_check_main_double_invoke` | mirror |
| 1 attr SSOT | `self/attrs/manual_main.hexa` (registered in `_registry.hexa`) | `@manual_main` opt-out |
| 2 cmd_run fallback | `self/main.hexa:1815-1825` | `bt 36` |
| 2 aot fail | `self/main.hexa:1717-1719` | `link_hit` block |
| 3 loader strip | `self/module_loader.hexa:333-348` | `ml_strip_and_clean` |
| 3 lexer skip | `self/lexer.hexa:138-145` | POSIX shebang |
| 3 NEW lexer AOT | `self/lexer.hexa` `#` branch (~line 750) | `silent-failure-enforcement Class 3` |
| 3 NEW lexer interp | `self/hexa_full.hexa::tokenize` `#` branch | mirror; pre-loop POSIX skip ALSO added (was missing in interp lexer) |
| 4 runtime | `self/runtime.c:6855-6863` | `hexa_json_parse` |
| 4 hive workaround | hive `core/session/store.hexa` | `_safe_parse_line` |
| 5 codegen tracking | `self/codegen_c2.hexa:814-823` | `_known_int_add` etc. |
| 7 + 8 audit | hexa-lang `tool/audit_shim_ssot.hexa` | full file |
| 7 hive policy | `.raw rule 8` (stage0/stage1 retire) | raw#8 |

## Witness evidence

```
$ ./build/hexa_interp self/_bug1_main_double.hexa  # interp path
error: auto-invoke conflict — `fn main()` is auto-called by hexa-strict
       AND a top-level `main()` call was found, which would run main() twice
       in the interp path (codegen suppresses but interp deliberately double-calls).
hint: remove the explicit `main()` call (auto-invoke handles it)
hint: OR add `@manual_main` attribute on `fn main` to opt out of auto-invoke
exit=1

$ ./hexa self/_bug1_main_double.hexa  # AOT path (hexa_v2 transpile)
[same error] exit=1

$ ./build/hexa_interp self/_bug1_main_double_optout.hexa  # @manual_main opt-out
X
X     # interp double-runs (intentional with opt-out)
exit=0

$ self/native/hexa_v2 self/_bug3_midfile_shebang.hexa /tmp/_t.c
error: shebang `#!` must be on line 1 of the file; found at line 5:1
hint: remove the shebang OR move it to the very first line
exit=1
```

Pre-fix witness: `_bug1_main_double.hexa` printed `X` twice via interp; `_bug3_midfile_shebang.hexa` would have errored with cryptic "unexpected token" downstream. Both replaced by clear root-cause diagnostics at parse-time.
