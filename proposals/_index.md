# hexa-lang Proposals Index

Upstream RFCs filed against hexa-lang per raw#159 hexa-lang-upstream-proposal-mandate.

Source: every workaround landed in a downstream consumer (hive/anima/nexus) MUST be paired with an RFC here. This index is the human-authoritative landing surface for proposal documents.

## RFC bundle 2026-04-28 — anima-eeg D-day gap-sourced proposals

Source convergence: `convergence/hexa_lang_upstream_2026_04_28_anima_eeg_gaps.convergence` (8 gaps surfaced + 1 split-out = 9 RFCs).

Origin: anima-eeg hardware bring-up session (OpenBCI Cyton+Daisy 16ch + Mark IV helmet + electrode_adjustment_helper.hexa --live mode) plus same-day follow-on work (B11 eeg_setup unified menu, clm_eeg_lz76_real.hexa, eeg_recorder.hexa, T17 mk_xii_eeg_corroboration.hexa).

| RFC | Slug | Priority | Status | Subsystem | Source gap |
|-----|------|----------|--------|-----------|------------|
| [001](rfc_001_popen_lines.md) | popen_lines / process_spawn streaming subprocess | P0 (BLOCKER) | partial-landed | stdlib (proc.hexa) + main builtin + AOT codegen | gap-1 |
| [002](rfc_002_map_methods.md) | map.has() / map.get(k, default) / map.keys() | P1 | done | stdlib (map dispatch) + interp + AOT codegen | gap-2 |
| [003](rfc_003_ansi_escape_literal.md) | \xHH / \uHHHH / \U escape sequences | P1 | proposed | lexer | gap-3 |
| [004](rfc_004_newline_string_literal.md) | basic \n \r \t escape parity | P1 | proposed | lexer | gap-3 (split) |
| [005](rfc_005_ends_with_aot_codegen.md) | ends_with / starts_with + slice-eq AOT codegen | P0 | proposed | AOT codegen (codegen_c2.hexa) | gap-5 |
| [006](rfc_006_exec_return_type.md) | exec() return-type semantics — docs+lint | P2 | proposed (corrected) | docs + lint | gap-6 (corrected) |
| [007](rfc_007_exec_lint_rule.md) | Lint: exec(...) == int_literal silent-fail | P1 | done | linter | gap-6 (split) |
| [008](rfc_008_project_relative_helper_invocation.md) | project_python() helper invocation primitive | P1 | done | stdlib (path.hexa) | gap-7 |
| [009](rfc_009_aot_bool_coercion.md) | AOT bool coercion — int_fn() == int_literal silent-false | P0 | proposed | AOT codegen (codegen_c2.hexa) | gap-9 |

## Priority distribution

- **P0 × 3** — RFC-001 (popen_lines BLOCKER), RFC-005 (AOT slice-eq silent-wrong), RFC-009 (AOT bool coercion silent-wrong)
- **P1 × 5** — RFC-002 (map methods), RFC-003 (\xHH \uHHHH escapes), RFC-004 (basic \n escapes), RFC-007 (exec()==int lint), RFC-008 (project_python)
- **P2 × 1** — RFC-006 (exec() return-type docs)

## Status distribution

- **done × 3** — RFC-002 (map.has), RFC-007 (exec lint), RFC-008 (project_python)
- **partial-landed × 1** — RFC-001 (capture+split wrapper landed; streaming form deferred)
- **proposed × 5** — RFC-003, RFC-004, RFC-005, RFC-006, RFC-009

## Subsystem coverage

- **stdlib** — RFC-001 (proc.hexa NEW), RFC-002 (map methods), RFC-008 (path.hexa project_python)
- **lexer** — RFC-003 (hex/unicode escapes), RFC-004 (basic \n parity)
- **AOT codegen (codegen_c2.hexa)** — RFC-005 (slice-eq), RFC-009 (Call(...) == IntLiteral) — same root-cause family; recommend bundled investigation cycle
- **linter / parser_pass** — RFC-007 (exec()==int rule)
- **docs** — RFC-006 (exec() decision tree cheat-sheet)

## Cross-cutting recommendations

- **Interpreter↔AOT parity meta-falsifier** — Every new stdlib method or builtin SHOULD ship with a regression test that runs under BOTH interpreter and AOT modes and asserts identical observable behavior. Strengthened by RFC-005 + RFC-009: every AOT codegen change MUST additionally pass raw#18 3-stage fixpoint re-proof (stage1 builds stage2; stage2 builds itself = stage3; stage3 binary-equal stage2 ⇒ fixpoint).
- **AOT codegen ==-dispatch family** — RFC-005 (string slice == literal) + RFC-009 (int_returning_fn() == int literal) share root cause: AOT codegen `==`-dispatch produces non-value-comparison semantics. Recommend bundled `self/codegen_c2.hexa` investigation cycle covering both gaps in one pass with shared regression-test harness.
- **String-literal escape policy unification** — RFC-003 + RFC-004 should land together as a single lexer-escape sweep; the falsifier surface differs (basic vs hex/unicode) but the lexer change is co-located.
- **Real-time / autonomous-agent tools as canonical dogfood targets** — anima-eeg yielded 5 gaps in one D-day session; subsequent same-day work yielded 4 more. Recommend formally declaring `anima-eeg`, `anima-clm-eeg/tool/mk_xii_*`, and similar real-time + autonomous-agent tool families as recurring hexa-lang dogfood / gap-sourcing targets.

## Honesty disclosures (raw#91 C3)

- All `Tracking` IDs in individual RFCs follow placeholder format `hexa-lang/issues/TBD-<slug>` — no real issue tracker entries filed. Format mirrors the convention in `convergence/hexa_lang_upstream_2026_04_28_anima_eeg_gaps.convergence`.
- RFC-006 was corrected in-place 2026-04-28 (raw#91 C3 honest disclosure): original framing proposed 3 new builtins; fact-check discovered the equivalent surface already exists in `runtime.c` (`hexa_exec_with_status` line 3235, `hexa_exec_stream` line 3182, `hexa_exec_capture` line 6373). RFC-006 scope narrowed to lint+docs; lint deliverable split out as RFC-007.
- RFC-001 status `partial-landed`: capture+split wrapper over `exec_with_status` landed in `self/stdlib/proc.hexa` with 5/5 selftest PASS; the streaming iterator form (`for line in popen_lines(...)` consuming subprocess stdout incrementally) and `process_spawn() -> Process` handle are NOT landed and require new C builtin (pipe + fork + readline loop). Tracked as a separate cycle.

## Process

- New RFCs: allocate the next sequential number; one .md per gap; sections (Problem, Proposed API/Change, Compatibility, Implementation Scope, Falsifier raw#71, Effort Estimate, Retire Conditions, Tracking) per template established by 001-005.
- Update this index when RFCs are added or status changes.
- Update `.roadmap` (hexa-lang-internal SSOT) with corresponding `roadmap RFC-NNN <status>` block + `priority P0|P1|P2` + `source-evidence <file:line>` lines.
- Reference impl skeleton .hexa files MAY accompany an RFC under raw#9 hexa-only-strict (RFCs themselves are .md docs).
