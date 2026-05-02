---
schema: hexa-lang/docs/phase_gamma_strict_semantic_spec/ai-native/1
last_updated: 2026-05-02
status: spec-only
phase: gamma
depends_on:
  - phase_beta_parser_extension (BG-β concurrent)
  - raw#3 attr-usage
  - raw#10 silent_error
  - raw#11 ai-native-enforce
predecessor_specs:
  - self/raws/silent_error.hexa
  - self/stdlib_result.hexa
  - self/attrs/allow_silent_exit.hexa
marker: state/markers/phase_gamma_strict_semantic_spec_landed.marker
impl_estimate_weeks: 4
impl_estimate_loc: 1800-2400
cost_cap: $0_mac_local_spec_only
items:
  - L4_pub_fn_attr_required
  - L8_at_public_vs_pub_fn
  - L9_fail_loud_result
---

# Phase γ — strict semantic SPEC (L4 + L8 + L9)

Spec-only. Implementation deferred to a separate cycle (~1 month estimate). Phase γ
extends Phase β (parser ext: @attr AST + strict mode + attr typing) with three
strict-mode semantic rules that close raw#10 and raw#11 in the type/decl layer.

## §1 TL;DR

| ID | Name | Strict-mode behavior | Non-strict | Audit count (primary) |
|----|------|----------------------|------------|------------------------|
| L4 | `pub fn` attr-required | `pub fn` MUST carry `@public` or `@export(...)` else compile error | warn-only | 3,050 hexa-lang + 395 anima = **3,445 total** |
| L8 | `@public` vs `pub fn` | `pub fn` deprecated; `@public fn` is canonical | warn (30d ramp → error) | 0 `@public fn` in primary tree (greenfield) |
| L9 | fail-loud `Result<T,E>` | unhandled `Err` (caller does not match/?/unwrap_or_panic) → compile error | unchanged | 1 `Result<>` site (anima ready/) |

3 items together convert raw#10 (silent_error lint) and raw#11 (ai-native-enforce)
from line-scan-time enforcement into compile-time enforcement. Migration risk dominated
by L4 (3,445 sites) — automation script in §7 covers the bulk; manual review for
slug/desc population on @export.

## §2 L4 — `pub fn` attr-required

### §2.1 Rule

Strict mode (raw#11 ai-native-enforce already gates strict-by-default for the
primary tree): every `pub fn` declaration MUST carry **exactly one** of the
following attributes immediately preceding the signature:

```hexa
@public
pub fn rng_meta_make(name: str) -> RngSourceMeta { ... }

// or — registry-grade public (preferred for tool/stdlib export points)
@export(slug="rng-meta-make", desc="Construct RngSourceMeta record for registry insertion.")
pub fn rng_meta_make(name: str) -> RngSourceMeta { ... }
```

Absence in strict mode → compile error `E_PHASE_GAMMA_L4_PUB_FN_MISSING_VISIBILITY_ATTR`:

```
error[L4]: pub fn requires @public or @export(...) attr in strict mode
  --> anima/core/rng/registry.hexa:93:1
   |
93 | pub fn rng_registry_names() -> [str] {
   | ^^^^^^ missing @public or @export(...)
   |
   = note: strict mode = raw#11 ai-native-enforce default for primary tree
   = help: add `@public` (visibility-only) or `@export(slug=..., desc=...)` (registry-grade)
   = grandfather: add file path to `.hexa-lang-pub-fn-grandfather` to defer (30d ramp)
```

### §2.2 Rationale

- raw#3 @attr-usage requires every visibility/intent declaration to carry attr metadata
  for AI-native discovery. `pub fn` without attr → invisible to lint/registry/LSP.
- `@export(slug, desc)` populates the same registry as @tool/@module — enables
  `hexa registry list --visibility public` cross-tree query without ad-hoc grep.
- `@public` is the lightweight escape hatch for module-internal `pub fn` that is NOT
  meant for cross-tree consumption (slug/desc would be noise).

### §2.3 Non-strict behavior

`@allow-bare-pub-fn` file-level attr (analog to `allow_silent_exit`) downgrades L4 to
warn. Required during migration window; auto-removed by §7 migration script when
file passes attr-add pass.

### §2.4 Selftest fixtures (5)

| # | Pattern | Expected |
|---|---------|----------|
| 1 | `@public\npub fn f() {}` | PASS |
| 2 | `@export(slug="f", desc="d")\npub fn f() {}` | PASS |
| 3 | `pub fn f() {}` (no attr, strict) | ERROR L4 |
| 4 | `pub fn f() {}` (no attr, file has `@allow-bare-pub-fn`) | WARN only |
| 5 | `@public\n@export(slug="f", desc="d")\npub fn f() {}` | ERROR L4_DUPLICATE_VISIBILITY |

## §3 L8 — `@public` vs `pub fn` (canonicalization)

### §3.1 Rule

`pub` keyword on `fn` declarations is **deprecated** as of Phase γ. Canonical form:

```hexa
// CANONICAL (Phase γ+):
@public
fn rng_meta_make(name: str) -> RngSourceMeta { ... }

// DEPRECATED (warn in strict, error after 30d ramp):
pub fn rng_meta_make(name: str) -> RngSourceMeta { ... }
```

### §3.2 Compatibility matrix (during 30d ramp)

| Form | Strict d0..d30 | Strict d30+ | Non-strict |
|------|----------------|-------------|------------|
| `@public fn ...` | PASS | PASS | PASS |
| `@public\npub fn ...` | WARN (redundant) | ERROR L8_REDUNDANT_PUB | WARN |
| `pub fn ...` (no attr) | ERROR L4 | ERROR L4 | WARN |
| `pub fn ...` (with @allow-bare-pub-fn) | WARN L8_DEPRECATED | ERROR L8_PUB_REMOVED | PASS |

After d30, `pub` keyword is removed entirely from the parser; `@public fn` is the
only public-visibility form. `pub` reverts to a reserved-but-unused keyword for
forward compatibility (no new semantics added).

### §3.3 Equivalence to Rust `pub`

`@public fn` ≡ Rust `pub fn` semantically (cross-module callable). `@export(...)`
adds registry-grade metadata (slug + desc) for AI-native discovery — strictly more
than Rust `pub`.

### §3.4 Migration script

`tool/migrate_pub_to_public_attr.hexa` — see §7. Idempotent, byte-stable,
sed-style line-rewrite with comment-aware skip logic.

### §3.5 Selftest fixtures (4)

| # | Pattern | Expected |
|---|---------|----------|
| 1 | `@public fn f() {}` | PASS canonical |
| 2 | `@public\npub fn f() {}` | WARN L8_REDUNDANT_PUB (d0..d30); ERROR after |
| 3 | `pub fn f() {}` (with @allow-bare-pub-fn) | WARN L8_DEPRECATED |
| 4 | `pub fn f() {}` (no attr, no allow) | ERROR L4 (precedes L8) |

## §4 L9 — fail-loud `Result<T,E>`

### §4.1 Rule

A function returning `Result<T, E>` produces a value that the caller MUST handle in
one of four canonical ways. Silent discard (assign to `_`, expression-statement, or
ignore) → compile error `E_PHASE_GAMMA_L9_RESULT_DISCARDED`.

### §4.2 Canonical handling forms

```hexa
// (a) match — explicit branching
match http_get(url) {
    Ok(body) => process(body),
    Err(e)   => log_and_continue(e)
}

// (b) ?-rethrow — propagate up, caller-fn must also return Result
let body = http_get(url)?

// (c) intentional panic — single-arg string (Rust unwrap_or_panic / expect)
let body = http_get(url).unwrap_or_panic("HTTP fetch must not fail at boot")

// (d) typed-default fallback — explicit Err→default mapping
let body = http_get(url).unwrap_or("")
```

### §4.3 Rejected patterns

```hexa
// REJECTED — silent discard
http_get(url)                            // ERROR L9: Result<>-typed expression-statement
let _ = http_get(url)                    // ERROR L9: Result<> bound to underscore
let body = http_get(url).val             // ERROR L9: field access bypasses match
http_get(url); next_step()               // ERROR L9: discarded by sequencing
```

### §4.4 `?` operator semantics

- `expr?` where `expr: Result<T, E>` → unwraps to `T` on Ok, returns `Err(e)` from
  enclosing fn on Err.
- Enclosing fn MUST return `Result<_, E'>` where `E: Into<E'>` (or identical).
- Type mismatch → compile error `E_PHASE_GAMMA_L9_RETHROW_TYPE_MISMATCH`.

### §4.5 Interaction with raw#10 silent_error lint

raw#10 (line-scan) handles `exit(N≠0)` + `empty catch`. L9 (type-checker) handles
`Result<>` discard. Together: 3 silent-error vectors closed at compile time:
| Vector | Layer | Status |
|--------|-------|--------|
| `exit(N≠0)` no msg | line-scan raw#10 | already enforced |
| `catch e {}` empty | line-scan raw#10 | already enforced |
| `Result<>` discarded | type-check L9 | NEW Phase γ |

raw#10 (b) `let _ = <error-return fn>()` — was deferred Tier 2 in raw#10. **L9 closes
raw#10 (b) at the type-system layer**, supersedes the deferred line-scan approach.

### §4.6 Selftest fixtures (7)

| # | Pattern | Expected |
|---|---------|----------|
| 1 | `match r { Ok(v)=>..., Err(e)=>... }` | PASS |
| 2 | `let v = r?` (caller returns Result) | PASS |
| 3 | `let v = r.unwrap_or_panic("msg")` | PASS |
| 4 | `let v = r.unwrap_or(default)` | PASS |
| 5 | `r` (expression-statement) | ERROR L9_DISCARD |
| 6 | `let _ = r` | ERROR L9_DISCARD |
| 7 | `let v = r?` (caller returns `int`) | ERROR L9_RETHROW_TYPE_MISMATCH |

## §5 Implementation checklist

### §5.1 L4 impl steps (8)

- [ ] strict-mode flag plumbing (depends on Phase β L1 strict-mode infra)
- [ ] AST visitor: walk `pub fn` decls, check preceding attr list contains exactly one of {`@public`, `@export(...)`}
- [ ] error message constant `E_PHASE_GAMMA_L4_PUB_FN_MISSING_VISIBILITY_ATTR` + locale-stable formatter
- [ ] grandfather list reader: `.hexa-lang-pub-fn-grandfather` (newline-separated path globs)
- [ ] file-level `@allow-bare-pub-fn` attr handling (analog to `allow_silent_exit.hexa`)
- [ ] LSP integration: surface L4 violations as red diagnostics
- [ ] selftest harness: 5 fixtures (§2.4)
- [ ] integration test: anima rng prototype 0 violations after migration

### §5.2 L8 impl steps (6)

- [ ] `@public` attr definition (depends on Phase β L1 @attr AST)
- [ ] parser: accept `@public fn` syntax (no `pub` keyword)
- [ ] parser: warn on `pub fn` in strict mode (deprecation)
- [ ] migration script `tool/migrate_pub_to_public_attr.hexa` (idempotent)
- [ ] post-d30 hook: `pub` keyword removal (separate cycle, NOT in Phase γ)
- [ ] selftest: 4 fixtures (§3.5)

### §5.3 L9 impl steps (10)

- [ ] type-checker: track `Result<T, E>` flow through expression-statement positions
- [ ] caller-side use detection: assignment / match / `?` / `unwrap_or_panic` / `unwrap_or` / `unwrap_or_else` / `is_ok` / `is_err`
- [ ] `?` operator parsing (postfix unary, lower precedence than `.`, higher than `;`)
- [ ] `?` typing rules: `Result<T, E>` → `T` in Ok-flow, return `Err(e)` cast to enclosing fn's error type
- [ ] type compatibility check `E: Into<E'>` (or identity for Phase γ — full Into-trait deferred)
- [ ] error messages: `L9_DISCARD`, `L9_RETHROW_TYPE_MISMATCH`, `L9_FIELD_ACCESS_BYPASS`
- [ ] grandfather: `.hexa-lang-result-discard-grandfather` (line-level annotation)
- [ ] LSP integration
- [ ] selftest: 7 fixtures (§4.6)
- [ ] integration: anima `ready/rust/evo_runner.hexa:50` audit (1 known site)

### §5.4 Total

- L4 = 8 steps
- L8 = 6 steps
- L9 = 10 steps
- **Total = 24 impl steps**

## §6 Backward compatibility — 30-day ramp + grandfather

### §6.1 Ramp schedule (Phase γ activation = day 0)

| Day | L4 | L8 | L9 |
|-----|----|----|----|
| d0..d7 | warn | warn | warn |
| d7..d30 | error if no grandfather | warn | error if no grandfather |
| d30+ | error always | error (`pub fn` removed) | error always |

### §6.2 Grandfather files

- `.hexa-lang-pub-fn-grandfather` — line-separated globs (e.g. `anima/anima/core/rng/**`)
- `.hexa-lang-result-discard-grandfather` — line-separated `path:line` (precise pin)

Grandfather entries are NOT permanent; each entry SHOULD carry a `# expires_yyyy_mm_dd`
trailer. Lint reports stale entries past expiry.

### §6.3 Migration burden estimate

| Item | Sites | Auto-fixable | Manual review |
|------|-------|---------------|---------------|
| L4 `pub fn` → add `@public` | 3,445 | ~95% (greenfield, no slug/desc decision) | ~5% (registry-grade `@export` requires slug+desc) |
| L8 `pub fn` → `@public fn` | 3,445 | ~99% (token replace) | ~1% (rare `pub` use as identifier) |
| L9 `Result<>` discard | 1 | 100% (line `evo_runner.hexa:50` is comment) | 0 |

Both L4 + L8 share the same 3,445 sites. Combined migration script handles both
in one pass.

## §7 Migration script — `tool/migrate_pub_to_public_attr.hexa`

### §7.1 Behavior

```
Usage: hexa tool/migrate_pub_to_public_attr.hexa <path...> [--dry-run] [--export-slug-from=<glob>]

Operations (idempotent, byte-stable):
  1. For each `pub fn <name>` line:
     a. If file is in `--export-slug-from=<glob>` matching path:
        prepend `@export(slug="<name>", desc="TODO_AUTO_GENERATED_FILL_IN")` line
     b. Else:
        prepend `@public` line
     c. Replace `pub fn <name>` with `fn <name>`
  2. Skip lines already preceded by @public/@export (idempotent re-run)
  3. Skip comment-only lines, string literals
  4. Emit migration report to stdout: <path> <before-count> <after-count> <skipped>
  5. --dry-run: report only, no file writes

Exit codes:
  0 — clean migration, all sites converted
  1 — partial (some sites need manual review, e.g. ambiguous `pub` usage)
  2 — fatal (file unreadable, parse error)
```

### §7.2 Verification

After migration script run, verify:

```bash
# 0 remaining `pub fn` in primary tree
grep -rn "^pub fn\|^[[:space:]]*pub fn" <root> --include="*.hexa" \
    | grep -v "/.claude/worktrees/" | grep -v "/build/" | grep -v "/dist/"
# expected: 0 lines

# All @public/@export attrs paired with fn decls
hexa tool/attr_audit.hexa --check pub-fn-pair <root>
# expected: 0 orphans
```

## §8 raw#10 caveats (honest limitations)

1. **Type-system change = largest impact**: L9 requires Result-flow tracking in the
   type-checker. Phase β parser ext alone is insufficient — L9 needs Phase β +
   Phase γ.0 (basic type-checker) co-landed. Estimate: +1 week beyond pure parser ext.
2. **`?` operator** introduces flow-sensitive return-type checks. Initial impl
   restricts `?` to identical-error-type rethrow; full `Into<E>` trait deferred.
3. **Grandfather lists are technical debt**: 30d ramp prevents big-bang break, but
   grandfather entries can rot. Mandatory `# expires_` trailer + stale-entry lint.
4. **3,445 site migration is the dominant risk**: even 99% auto-conversion leaves
   ~34 sites for manual review. Slug/desc population for `@export(...)` requires
   human judgment (NOT mechanical).
5. **`@public fn` vs `@export(...)` choice** is currently NOT mechanizable. Heuristic
   in §7 (export-slug-from glob) shifts the burden to the migration-time path filter
   — works for stdlib/tool/ trees, falls back to `@public` for everything else.
6. **Backward-incompat risk**: `pub` keyword removal at d30+ would break any
   external/third-party `.hexa` source that uses bare `pub fn`. Phase γ assumes
   primary tree only; cross-repo (`anima/`, `hive/`) MUST be migrated synchronously.
7. **Phase β dep**: Phase γ cannot land before Phase β. If Phase β slips, Phase γ
   slips by the same amount. Concurrent BG-β monitoring is REQUIRED.
8. **rng prototype audit shows 0 `pub fn`**: anima/core/rng/* uses bare `fn` (no `pub`).
   This means rng prototype is NOT representative of broader migration burden — the
   3,445 site count comes from older code, not the canonical rng template.
9. **Result<T,E> usage today is ~1 site total**: L9 enforcement is forward-looking;
   immediate breakage near zero. The cost is in NEW Result<>-using stdlib (e.g.
   stdlib_result.hexa is opt-in stub).
10. **Interaction with throw/catch**: hexa-lang has `throw e` / `catch e` already
    (see silent_error.hexa). L9 does NOT remove throw/catch; it adds Result<> as a
    typed alternative. Migration from throw/catch to Result<> is a SEPARATE cycle
    (not Phase γ).

## §9 Dependency on Phase β

Phase γ depends on Phase β deliverables:

| Phase β output | Phase γ consumer | Required for |
|----------------|------------------|--------------|
| @attr AST node | L4 attr presence check | L4 |
| @attr typed args (slug=str, desc=str) | L4 `@export(...)` validation | L4 |
| strict-mode flag | L4 + L8 strict gating | L4, L8 |
| @public attr def | L8 canonical form | L8 |
| Type-checker basic infra | L9 Result<> flow | L9 |

**If Phase β does NOT deliver type-checker infra**: Phase γ.L9 is forced to a line-scan
fallback (analogue to raw#10 silent_error.hexa) — strictly weaker. **Recommendation**:
co-land Phase β type-checker stubs even if minimal (Result<>-only matcher is sufficient).

## §10 File index sha-pin

| Path | Role | sha256 (Phase γ d0 baseline) |
|------|------|-------------------------------|
| `/Users/<user>/core/hexa-lang/self/raws/silent_error.hexa` | raw#10 line-scan predecessor | _pin at impl-cycle d0_ |
| `/Users/<user>/core/hexa-lang/self/stdlib_result.hexa` | Result<T,E> stdlib (opt-in) | _pin at impl-cycle d0_ |
| `/Users/<user>/core/hexa-lang/self/attrs/allow_silent_exit.hexa` | grandfather attr template | _pin at impl-cycle d0_ |
| `/Users/<user>/core/hexa-lang/docs/phase_gamma_strict_semantic_spec.ai.md` | this spec | _self-sha at land time_ |
| `/Users/<user>/core/hexa-lang/state/markers/phase_gamma_strict_semantic_spec_landed.marker` | land marker | _self-sha at land time_ |

sha-pin policy: each impl-cycle commit re-pins this section; spec doc tracks the
exact predecessor revision used by the impl.

## §11 Estimate summary

- **Spec scope (this doc)**: 3 items × ~600 LOC equivalent design = ~1800 LOC native
  impl estimate. With migration script + selftest fixtures: **~2400 LOC total**.
- **Cycle estimate**: 4 weeks (1 week each L4/L8/L9 + 1 week migration + selftests).
- **Cost cap**: $0 (mac-local impl, no GPU/LLM dependency).
- **Land target**: Phase γ d0 = Phase β d28 (concurrent ramp).

## §12 Out of scope

- `Into<E>` trait full impl (deferred — L9 v2)
- `pub` keyword removal (separate Phase γ.1, post-d30)
- Cross-repo migration (anima, hive) — handled by per-repo migration cycles consuming
  this spec
- raw#10 line-scan retirement (raw#10 retained as belt-and-suspenders even after L9 lands)
- `@private`, `@internal`, `@friend` attrs (separate Phase δ visibility refinement)
- ABI stability — `@export(slug, desc)` registers metadata, NOT a stable ABI contract
