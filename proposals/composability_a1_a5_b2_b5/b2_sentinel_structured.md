# B2 — `@sentinel` structured form

- **Status**: spec proposed (additive, no migration)
- **Date**: 2026-05-02
- **Bundle**: composability A1+A5+B2+B5 (BG-HX2)
- **Cross-refs**:
  - precedent (ad-hoc): all `core/{grammar,attr}_format/*.hexa` use `// @sentinel(__X__ <PASS|FAIL> mode=selftest)` comment-form
  - precedent (printed pattern): `tool/parallel_scheduler.hexa:603` `println("__PARALLEL_SCHED_RESULT__ PASS")`
  - precedent (attr v2 spec): `docs/phase_beta_parser_ext_spec.ai.md` §3.3 `parse_attr_decl()` for AST-form attrs
  - catalog: `self/attrs/attrs.json` includes `sentinel` (extension set, not in 24-core)

## §0 TL;DR

Today `@sentinel` is **two unrelated things**:
1. A comment-form *documentation marker* with ad-hoc payload `__NAME__ <PASS|FAIL>
   mode=selftest` (see all `attr_format/core/*.hexa` headers).
2. A printed-form *runtime breadcrumb* `println("__NAME__ PASS")` matched by external
   ledger scrapers (e.g. `tool/parallel_scheduler.hexa:293` checks for sentinel
   failure markers in subprocess stdout).

These two forms have:
- No grammar (the `__NAME__` payload is convention only).
- No enforcement (no lint catches `__FOO__` typos vs the documented name).
- No structured fields (mode, cycle number, axis, etc. are free-text).

This RFC introduces a **structured `@sentinel` attr-form** with named fields and a
matching printed contract:

```hexa
@sentinel(name="__PARALLEL_SCHED_RESULT__",
         fields=["verdict", "cycle"],
         pattern="__PARALLEL_SCHED_RESULT__ verdict={verdict} cycle={cycle}")
fn run_scheduler() {
    // ... work ...
    sentinel_emit("PARALLEL_SCHED_RESULT", verdict="PASS", cycle=42)
}
```

**Additive only**: existing comment-form `// @sentinel(...)` continues to be honored as
a soft-doc marker. The new attr-form is opt-in. No existing site must change.

## §1 Current behavior (baseline)

Comment-form (documentation):
```hexa
// @sentinel(__GRAMMAR_FORMAT_SOURCE__ <PASS|FAIL> mode=selftest)
fn _selftest_source() { ... }
```

Printed-form (runtime breadcrumb):
```hexa
if all_ok { println("__PARALLEL_SCHED_RESULT__ PASS") }
else      { println("__PARALLEL_SCHED_RESULT__ FAIL") }
```

External scraper pattern (in `tool/parallel_scheduler.hexa:293`):
```hexa
// Check for sentinel failure markers
if line.contains("FAIL") && line.starts_with("__") { record_failure(line) }
```

Issues:
- The doc-form name `__GRAMMAR_FORMAT_SOURCE__` and the println-form name MUST match
  manually — no enforcement.
- Adding a new field (e.g. `cycle=N`) requires touching every emit site.
- Scraper regex is hand-crafted per consumer; no canonical parser.

## §2 Proposed contract (additive)

### §2.1 Attr-form (declares the sentinel)

```hexa
@sentinel(
    name="__SCHED_RESULT__",
    fields=["verdict", "cycle", "axis"],
    pattern="__SCHED_RESULT__ verdict={verdict} cycle={cycle} axis={axis}",
    catalog="hexa-lang/state/markers/sentinels/sched.json"   // optional, registers in catalog
)
fn run_scheduler() { ... }
```

Required fields in attr:
- `name`: str, must start and end with `__`, must be unique within the catalog file (if `catalog` provided).
- `fields`: `[str]`, list of field names. `verdict` is RECOMMENDED but not required.
- `pattern`: str, must contain `{name}`-like placeholders for every field declared.

Optional:
- `catalog`: str, path to a JSON file that aggregates registered sentinels (raw 270 conformant). If absent, sentinel is local-only.
- `mode`: str, one of `"selftest" | "runtime" | "both"` (default `"both"`).

### §2.2 Emit-form (matches the declaration)

```hexa
sentinel_emit("SCHED_RESULT", verdict="PASS", cycle=42, axis="bench")
// → prints: __SCHED_RESULT__ verdict=PASS cycle=42 axis=bench
```

`sentinel_emit(name, kw=val, ...)` is a stdlib fn (new):
- Looks up declared sentinel by `name` in registry built from `@sentinel` attrs in scope.
- Validates kw set matches `fields` list (missing → error; extra → error).
- Renders `pattern` with substitutions; prints to stdout.
- Returns the rendered string (so callers can also write to a log file).

### §2.3 Parse-form (consumer side)

```hexa
let parsed = sentinel_parse(line)        // returns SentinelHit | nil
// SentinelHit { name: str, fields: map<str, str> }
if parsed != nil && parsed.name == "SCHED_RESULT" {
    let verdict = parsed.fields["verdict"]
    if verdict == "FAIL" { record_failure(parsed) }
}
```

### §2.4 Catalog-form (optional, raw 270 SSOT)

`hexa-lang/state/markers/sentinels/<bucket>.json`:
```json
{
  "schema": "hexa-lang/sentinel/catalog/1",
  "entries": [
    {"name": "__SCHED_RESULT__", "fields": ["verdict", "cycle", "axis"], "pattern": "...", "owner": "tool/parallel_scheduler.hexa"}
  ]
}
```

Lint (`tool/sentinel_lint.hexa`, new) verifies every `sentinel_emit(...)` call site
matches a declared `@sentinel(...)` attr in the same module OR catalog.

## §3 Compatibility

- Comment-form `// @sentinel(__X__ <PASS|FAIL>)` — unchanged. Lint treats as soft-doc;
  no validation, no parsing.
- Printed-form `println("__X__ PASS")` — unchanged. Existing scrapers continue to work.
- New attr-form is opt-in per declaration site.
- New `sentinel_emit()` is a stdlib fn — pure addition, no name collision (verified:
  `grep -rn "fn sentinel_emit" /Users/<user>/core/hexa-lang` → empty).

## §4 Impl plan (additive, no migration)

**Attr registry side** (`hexa-lang/attr_format/module/attr_v3.hexa` or sister extension):
- Add `sentinel` to the typed-attr schema set:
  ```
  attr sentinel {
      name:    str   required pattern="^__.+__$"
      fields:  [str] required
      pattern: str   required
      catalog: str   optional
      mode:    str   optional default="both" enum=["selftest","runtime","both"]
  }
  ```
- v3 (typed attr) is the natural home; until v3 lands as WRAPPED, the v1 catalog
  accepts `sentinel` as ext-validated (no structure check).
- Effort: ~30 LoC (schema entry + selftest case).

**Stdlib side** (`hexa-lang/stdlib/sentinel.hexa`, NEW):
- Public API:
  ```
  fn sentinel_emit(name: str, kw: map<str, str>) -> str
  fn sentinel_parse(line: str) -> SentinelHit?
  fn sentinel_registry() -> [SentinelDecl]    // built from @sentinel attrs visible at link time
  ```
- Validation logic: name format, field set match, pattern render.
- Effort: ~150 LoC.

**Lint side** (`hexa-lang/tool/sentinel_lint.hexa`, NEW):
- Scans for `sentinel_emit(...)` calls; cross-checks against `@sentinel(...)` decls in
  same file + catalog.
- Reports: undeclared emit, unused decl, field mismatch.
- Effort: ~120 LoC.

**Catalog side** (`hexa-lang/state/markers/sentinels/`, NEW dir):
- Initial catalog files per bucket (e.g. `sched.json`, `format.json`, `selftest.json`).
- raw 270 schema (handoff field for AI consumers).
- Effort: ~30 LoC of JSON per bucket; bucket population follows opt-in adoption.

**Total estimated impl scope**: ~330 LoC across 4 files (excl. catalog data).

## §5 Falsifiers (raw#71)

1. `@sentinel(name="X")` (missing `__` prefix/suffix) — must error at parse/lint time.
2. `sentinel_emit("Y", verdict="PASS")` where no `@sentinel(name="__Y__", ...)` exists →
   lint error.
3. `sentinel_emit("X", verdict="PASS")` where decl says `fields=["verdict","cycle"]` →
   error (missing `cycle`).
4. `sentinel_parse("__X__ verdict=PASS cycle=1")` returns `nil` when `__X__` is registered → fail.
5. Two `@sentinel` decls with same `name` in same catalog → lint error (duplicate).
6. Existing comment-form `// @sentinel(__X__ <PASS|FAIL>)` site emits a parse warning
   under the new attr-form parser → fail (additive, must be silently ignored as soft-doc).

## §6 Selftest fixture

See `proposals/composability_a1_a5_b2_b5/fixtures/b2_sentinel_structured.hexa` —
8 cases covering decl, emit, parse, missing-field error, name-format error,
catalog round-trip, comment-form silent-ignore, scraper round-trip.

## §7 raw#10 caveats

1. Soft-doc comment-form is **NOT migrated**. The two forms coexist forever —
   comment-form is for human readers, attr-form is for tooling.
2. `sentinel_registry()` requires the parser/typecheck to expose attr metadata at
   runtime; if the build pipeline strips attrs before runtime, registry must be
   reconstructed at link time (offline scan). v1 implementation: link-time scan
   into a static array; runtime reflection deferred.
3. `pattern` rendering is naive `{key}` substitution — no escaping. If a field value
   contains `{` or `}`, behavior is undefined for v1. Document explicitly.
4. Catalog file is raw 270 conformant (schema + ai-native fields). Drift between
   catalog and `@sentinel` decl = lint error (SSOT violation).
5. `sentinel_emit` returns the rendered string AND prints it. If the caller only wants
   the string (e.g. write to log file without stdout), use `sentinel_render()` (sister
   fn, no print). v1: only `sentinel_emit`; `sentinel_render` deferred.
6. The 24-core attr catalog (`self/attrs/attrs.json`) does NOT include `sentinel` —
   it's in the extension set. Promotion to 24-core is a separate governance event.
7. Field values are strings in the API, even if numeric. `sentinel_emit("X", cycle="42")`
   not `cycle=42`. Avoids type-coercion surprises in pattern rendering.
8. Mac-local strict — selftest must run under `hexa` resolver darwin-bypass.

## §8 Effort + retire

- Effort: spec ~3h. Impl ~2 days (stdlib + lint + 1-2 catalog buckets).
- Retire when: §6 fixture passes 4 sessions AND at least 2 existing tools opt in
  (candidates: `tool/parallel_scheduler.hexa`, `grammar_format/core/grammar_format_main.hexa`).
