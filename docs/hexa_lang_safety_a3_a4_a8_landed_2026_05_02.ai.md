# hexa-lang safety A3 + A4 + A8 — landed 2026-05-02

> Schema: `hexa-lang/handoffs/safety_a3_a4_a8_landed/1`
> Session: BG-HX1 sub-agent
> Directives: omega-cycle (6-step) + silent-land marker + AI-native +
> raw 270/271/272/273 + BR-NO-USER-VERBATIM + friendly preset +
> migration FORBIDDEN (additive-only)
> Cap: 180min · Cost: $0 (mac-local) · Destructive: 0 · Migration: 0
> File-scope decoupled from BG-HL (`a1b63ac5`), which only touches
> `.roadmap.<domain>` SSOT — this BG only touches parser/runtime
> source + lint + selftest fixtures.

## Goal

Land three safety mechanisms in `hexa-lang` core that close known
silent-failure classes:

| Axis | Symptom (pre-patch)                                          | Source artifact                                |
|------|--------------------------------------------------------------|------------------------------------------------|
| A3   | reserved keyword used as field/param silently cascades       | `proof: int` field in struct → 3 dup parse errs|
| A4   | array param mutation leaks to caller (no diagnostic)         | hxc_v3 selftest fixture state bleed            |
| A8   | top-of-file `env(KEY)` + personal-path concat leaks dev path | `lint_hook.hexa` `HOME + "/Dev/hexa-lang/..."` |

All three are **additive-only**. Existing source corpora are
**byte-identical** post-patch when the new opt-in path (`HEXA_RESERVED_TIER2=1`)
is OFF. Lint A4/A8 emit `warn` only — no exit code change.

## Predecessors / SSOT links

| File                                                                              | Role                                            |
|-----------------------------------------------------------------------------------|-------------------------------------------------|
| `/Users/<user>/core/hexa-lang/self/parser.hexa`                                    | A3 patch site (parser-time reserved-id check)   |
| `/Users/<user>/core/hexa-lang/self/lexer.hexa`                                     | source of tier-2 keyword kinds (Proof, Theorem, …) |
| `/Users/<user>/core/hexa-lang/self/linter.hexa`                                    | A4 + A8 patch site                              |
| `/Users/<user>/core/hexa-lang/test/regression/reserved_keyword_guard.hexa`         | tier-1 P51 reference (Borrow/Own/Move)          |
| `/Users/<user>/core/hexa-lang/.raw-audit` raw 15                                   | no-hardcode SSOT (defer-until 2026-06)          |
| `/Users/<user>/core/hexa-lang/docs/interpreter_stale_root_cause_20260423.md`       | known runtime blocker for full lint exec smoke  |

## A3 — reserved-keyword tier-2 hard-fail

### Current behavior (baseline)

`p_is_reserved_id_kind` in `self/parser.hexa:322` only flags the
tier-1 ownership keywords (`Borrow`/`Own`/`Move`) as identifier-position
errors with the targeted **P51** diagnostic and the consume-to-recover
fix. All other reserved kinds (`Proof`, `Theorem`, `Invariant`, `Pure`,
`Effect`, `Macro`, `Where`, `Comptime`, `Intent`, `Verify`, `Optimize`)
fall through to the generic `expected identifier, got <Kind>` branch
which **does not consume** the offending token, producing a 3+ duplicate-
error cascade at the same `(line, col)` until `p_max_errors` caps.

Empirical reproduction (parse-only, pre-patch):
```
$ ./hexa.real parse <(echo 'let pure = 42'; echo 'println(to_string(pure))')
Parse error at 1:5: expected identifier, got Pure ('pure')
Parse error at 1:5: expected Eq, got Pure ('pure')
Parse error at 1:5: unexpected token Pure ('pure')
Parse error at 1:10: unexpected token Eq ('=')
Parse error at 2:19: unexpected token Pure ('pure')
```

### Patch summary (additive)

`self/parser.hexa` adds:

- `p_is_reserved_id_kind_tier2(k)` — 11 verification/meta keyword
  predicates.
- `p_reserved_id_hint_tier2(k)` — sanitized rename suggestions
  (`Proof → prf`, `Theorem → thm`, `Invariant → inv`, `Pure → pure_v`,
  `Effect → eff`, `Macro → mcr`, `Where → wh`, `Comptime → cmt`,
  `Intent → intt`, `Verify → vrf`, `Optimize → opt`).
- `p_reserved_tier2_enabled()` — `@pure` env switch reading
  `HEXA_RESERVED_TIER2`. Returns `true` only on `"1"` or `"true"`.
- `p_expect_ident()` extended branch:
  - new `tier2_hit = tier2_on && p_is_reserved_id_kind_tier2(tok.kind)`
  - emits `P51-EXT: reserved keyword '<word>' cannot be used as identifier
    (verification/meta family) — rename to '<hint>' [opt-in via
    HEXA_RESERVED_TIER2=1]` when `tier2_hit`
  - consumes the bad token (`p_pos = p_pos + 1`) on `tier2_hit` to halt
    the cascade — same recovery path as tier-1.

### Falsifiability

- ON-path test: `HEXA_RESERVED_TIER2=1 ./hexa parse
  test/regression/reserved_keyword_tier2_guard.hexa`
  expects EXACTLY 5 P51-EXT messages (1 per offender), no cascade.
- OFF-path test: same file without env var emits the legacy
  `expected identifier, got Proof ('proof')` cascade (current behavior
  unchanged, byte-identical).

### Selftest fixture

`/Users/<user>/core/hexa-lang/test/regression/reserved_keyword_tier2_guard.hexa`
(LOC 38) — exercises tier-2 in struct-field, fn-param, let-binding
positions. Mirrors `reserved_keyword_guard.hexa` shape for tier-1.

## A4 — array param mutation visibility lint

### Current behavior (baseline)

hexa array values are passed by reference (shared with caller). A
function `fn append_one(xs) { xs.push(1) }` mutates the caller's
array in place. There is **no** static or runtime diagnostic. This
is the canonical class of bug behind "mutation leak between sub-tests"
patterns — the hxc_v3 selftest fixture is the cited case where a
shared array carries state across test cases.

### Patch summary (additive)

`self/linter.hexa` adds `check_array_param_mutation(lines)`:

- Detects `fn`/`pub fn`/`async fn`/`pure fn` signature, extracts
  param names (handles `name`, `name: type`, `mut name: type`, skips
  `self`).
- Tracks `{`/`}` depth within fn body to know when to reset params.
- For each param, fires `[A4]` warn on:
  1. `param.push(` / `.pop(` / `.insert(` / `.remove(` / `.clear(` /
     `.append(` / `.extend(`
  2. `param[…] = …` (LHS index assign, NOT `==`)
  3. `param = …` rebinding at line start.
- Suggested remediation in message: clone (`let mut local = param + []`)
  or document via `// @mut <param>` (future contract syntax).

### Falsifiability

`/Users/<user>/core/hexa-lang/test/regression/lint_a4_array_param_mutation.hexa`
(LOC 60). 6 cases — 4 expected `[A4]` hits (push, index-assign,
rebind, pop), 2 expected `[A4]` non-hits (read-only access, clone-
then-mutate-local).

### Why lint and not parser change

A parser-side enforcement would require introducing `&` borrow
markers in the type system — out of additive-only scope. The lint
gives a non-disruptive feedback loop today; future v2 grammar can
upgrade A4 to a hard fail once the borrow syntax is decided
(deferred to phase_delta_language_v2_spec.ai.md).

## A8 — env() lazy default personal-path-leak lint

### Current behavior (baseline)

`raw 15` (no-hardcode) defers absolute-path scanning to 2026-06 because
the literal scanner is unfinished and 354 violations remain in the
backlog. Meanwhile the canonical leak vectors are:

```
let HEXA_BIN = env("HOME") + "/Dev/hexa-lang/hexa"          // self/lint_hook.hexa:5
let LOG_FILE = env_or("TF_LOG", HOME_DIR + "/.token-forge/proxy.log")
let LOG_DIR  = env_or("LOG", "/Users/<user>/Dev/somerepo/logs")
```

### Patch summary (additive)

`self/linter.hexa` adds `check_env_personal_path_default(lines)` with
3 pattern detectors:

1. **2-arg env() / env_or() with `/Users/`, `/home/`, `/root/`, `/srv/`
   prefix in default arg** — fires `[A8]` warn unconditionally.
2. **Top-of-file (line ≤ 50) `env(...)` concat with `/Dev/`, `/core/`,
   `/workspace/`, `/Users/`, `/home/` substring** — fires `[A8]` warn.
3. Allow-comment `// a8_lint: allow` skips the line (intentional case
   for tooling under development).

### Falsifiability

`/Users/<user>/core/hexa-lang/test/regression/lint_a8_env_personal_path.hexa`
(LOC 39). 6 cases — 3 expected `[A8]` hits (env_or with `/Users/`,
top-of-file concat with `/Dev/`, env_or with `/home/`), 3 expected
`[A8]` non-hits (env() alone, env() concat with `/etc/`, env_or with
relative `8080`).

### Recommendation surfaced in lint message

> "extract to config/dev_paths.json or require env var explicitly"

The first half is a positive-direction nudge toward a repo-relative
config (which `raw 15` does NOT forbid), the second half restates the
strict path (require + clear error). Either resolves the leak.

## Verification matrix

| Step                                                                  | Result                          |
|-----------------------------------------------------------------------|---------------------------------|
| `parse self/parser.hexa`                                              | OK (self-parse clean)           |
| `parse self/linter.hexa`                                              | OK (self-parse clean)           |
| `parse test/regression/reserved_keyword_tier2_guard.hexa`             | parse error (intentional)       |
| `parse test/regression/lint_a4_array_param_mutation.hexa`             | OK                              |
| `parse test/regression/lint_a8_env_personal_path.hexa`                | OK                              |
| Full lint exec on A4/A8 fixtures                                      | **DEFERRED** — see caveats §5   |
| Full lint exec on tier-2 fixture under `HEXA_RESERVED_TIER2=1`        | **DEFERRED** — see caveats §5   |

## raw#10 honest caveats (5)

1. **Interp-stale rebuild blocker** — `docs/interpreter_stale_root_cause_20260423.md`
   documents that `./hexa.real run self/linter.hexa <file>` produces
   no output (and pre-existing `Runtime error: undefined function:
   real_args` / `starts_with` / `ends_with` against the stale interp
   binary). My A4/A8 lint additions are **parse-verified only**;
   end-to-end `[A4]` / `[A8]` warning emission needs the next
   `tool/rebuild_stage0.hexa` pass to refresh `build/interpreter.c` /
   `build/stage1/main.c`. The fixtures are pre-staged for that round.
2. **A3 opt-in default OFF** — to preserve byte-identical behavior on
   the existing self-host bootstrap corpus (which may legitimately use
   tier-2 words inside contexts the lexer does not lex as KW kinds —
   e.g. comments, strings — but NOT in identifier positions; still,
   over-eagerness was avoided). CI strict gate can flip via
   `HEXA_RESERVED_TIER2=1` after a corpus sweep.
3. **A4 false-negative tolerated** — param-name shadowing inside
   nested fns / closures is not tracked. A future cycle can upgrade
   the lint with a proper scope stack; today the line-major heuristic
   covers the canonical hxc_v3 case.
4. **A8 line-≤-50 heuristic** — only the top of file is scanned for
   pattern 3 (env-concat). A `let X = env("HOME") + "/Dev/..."` at line
   200 escapes detection. Justified: the empirical leak vectors are
   all top-of-file constants. Pattern 1+2 (env_or 2-arg) is line-
   agnostic and catches the rest.
5. **Migration FORBIDDEN compliance** — no existing files renamed,
   no existing identifiers changed, no existing struct fields
   modified. The 3 tier-2 keyword-as-identifier instances inside the
   self-host corpus (if any) are NOT surfaced or fixed in this BG;
   that is a separate cycle.

## Files touched

| Path                                                                                       | Type      | LoC delta |
|--------------------------------------------------------------------------------------------|-----------|-----------|
| `/Users/<user>/core/hexa-lang/self/parser.hexa`                                             | modified  | +60       |
| `/Users/<user>/core/hexa-lang/self/linter.hexa`                                             | modified  | +200      |
| `/Users/<user>/core/hexa-lang/test/regression/reserved_keyword_tier2_guard.hexa`            | new       | +38       |
| `/Users/<user>/core/hexa-lang/test/regression/lint_a4_array_param_mutation.hexa`            | new       | +60       |
| `/Users/<user>/core/hexa-lang/test/regression/lint_a8_env_personal_path.hexa`               | new       | +39       |
| `/Users/<user>/core/hexa-lang/docs/hexa_lang_safety_a3_a4_a8_landed_2026_05_02.ai.md`       | new       | (this)    |
| `/Users/<user>/core/hexa-lang/state/markers/hexa_lang_safety_a3_a4_a8_landed.marker`        | new       | marker    |

## Next-cycle priority recommendations

| Pri | Item                                                            | Cost | Wallclock |
|-----|-----------------------------------------------------------------|------|-----------|
| P1  | `tool/rebuild_stage0.hexa` round to refresh interp binary       | $0   | 30-60min  |
| P2  | A3 corpus sweep — flip `HEXA_RESERVED_TIER2=1` in CI strict gate | $0   | 1-2h     |
| P3  | A4 lint clean-pass on `self/` (dogfood)                          | $0   | 1h       |
| P4  | A8 lint clean-pass on `self/`, `tool/`, `stdlib/`                | $0   | 1h       |
| P5  | A4 v2 with proper scope stack (raw 15 escalation)                | $0   | 2h       |

## Status

**LANDED_PARSE_VERIFIED_RUNTIME_DEFERRED**
