---
schema: hexa-lang/attr_format/module/ai-native/1
last_updated: 2026-05-02
ssot:
  interface:  hexa-lang/attr_format/core/source.hexa
  registry:   hexa-lang/attr_format/core/registry.hexa
  router:     hexa-lang/attr_format/core/router.hexa
  aggregator: hexa-lang/attr_format/core/attr_format_main.hexa
  config:     hexa-lang/config/attr_format_sources.json
preserved_unchanged:
  parser:     hexa-lang/self/parser.hexa            # per-attr inline switch lines 591-865
  catalog:    hexa-lang/self/attrs/attrs.json       # 24-attr catalog SSOT
  lint_v1:    hive/tool/attr_usage_lint.hexa        # raw 3 ai-native 24 attr coverage
  lint_ai:    hexa-lang/tool/ai_native_lint.hexa    # ai-native lint
  scan_ai:    hexa-lang/tool/ai_native_scan.hexa    # file/dir naming hygiene
spec_cross_link:
  v2_v3:      hexa-lang/docs/phase_beta_parser_ext_spec.ai.md   # §3 L1 + §5 L10
  v4:         hexa-lang/docs/phase_delta_language_v2_spec.ai.md # §4 L12
markers:
  landed:     hexa-lang/state/markers/attr_format_core_module_dogfooding_landed.marker
roadmap_entry: 247
status: v1 WRAPPED, v2/v3/v4 SPEC-STUB, v5 FUTURE-STUB (Level 3b dogfooding)
level: "Level 3b — sister to Level 3a (grammar_format), perpendicular to Level 1 (raw) + Level 2 (hxc) + Level 3a (grammar)"
---

# hexa-lang attr-format (AI-native)

Pluggable @attr-format version layer. Consumers ask for parse/validate/expand from a named version (v1..v5) or trust the router default chain. Each attr-format evolution stage plugs in as one `.hexa` module + one config block, mirroring the `anima/{core,modules}/rng/` Level 0 prototype pattern.

This is the **dogfooding** version: hexa-lang's own `@attr` system is wrapped as a core+module pattern so future evolution (v1 comment → v2 AST → v3 typing → v4 composition → v5 future) is itself modular.

## TL;DR for an agent reading this cold

- Default chain: **v1 only** (`["v1"]`). v1 = comment-form `// @tool(...)` lint wrapper, fully functional today.
- v2/v3/v4 are SPEC-STUBs — meta + contract surface only, parse/validate return SPEC-PENDING.
- v5 is FUTURE-STUB (no spec).
- To extract attrs from a source string: call `attr_format_route_parse(src)` from `hexa-lang/attr_format/core/router.hexa`.
- To force a single version: `HEXA_ATTR_FORMAT=v1 hexa <driver>.hexa` (no fallback).
- To override chain: `HEXA_ATTR_FORMAT_CHAIN=v4,v3,v2,v1 hexa <driver>.hexa` (v4-first lower chain — currently SPEC-PENDING through to v1).
- **Do not modify** the 5 preserved files (parser.hexa, attrs.json, attr_usage_lint.hexa, ai_native_lint.hexa, ai_native_scan.hexa) — this layer wraps, not replaces.

## Architecture map

```
caller
  └── hexa-lang/attr_format/core/router.hexa             (config-driven version + lower chain)
       └── hexa-lang/attr_format/core/registry.hexa      (version → module dispatch, 5 entries)
            ├── hexa-lang/attr_format/module/attr_v1.hexa   v1  WRAPPED      ← PRIMARY (current land)
            │    ↳ wraps: hive/tool/attr_usage_lint.hexa regex / attrs.json catalog
            ├── hexa-lang/attr_format/module/attr_v2.hexa   v2  SPEC-STUB    (Phase β L1 — AST first-class)
            ├── hexa-lang/attr_format/module/attr_v3.hexa   v3  SPEC-STUB    (Phase β L10 — param typing)
            ├── hexa-lang/attr_format/module/attr_v4.hexa   v4  SPEC-STUB    (Phase δ L12 — composition group)
            └── hexa-lang/attr_format/module/attr_v5.hexa   v5  FUTURE-STUB  (no spec yet)
```

## Public API contract

```hexa
struct AttrFormatMeta {
    name:                 str   // "v1".."v5"
    version:              int   // 1..5
    form:                 str   // "comment" | "ast" | "ast_typed" | "composition" | "future"
    phase:                str   // "current" | "beta" | "delta" | "future"
    status:               str   // "WRAPPED" | "SPEC-STUB" | "FUTURE-STUB"
    supports_composition: int   // 0/1
    supports_typing:      int   // 0/1
    catalog_size:         int
    spec_doc:             str
    notes:                str
}

struct AttrDecl {
    name:         str    // attr name (no leading @)
    params:       [str]  // serialized "key=value" strings (v1 leaves empty)
    line:         int
    col:          int
    mode:         str    // "file" | "decl"
    form_version: str    // origin version "v1".."v4"
}

struct AttrParseResult    { ok: int, n_decls: int, decls: [AttrDecl], message: str }
struct AttrValidateResult { ok: int, n_issues: int, issues: [AttrIssue], message: str }
struct AttrExpandResult   { ok: int, n_decls: int, decls: [AttrDecl], message: str }

// Per-module: hexa-lang/attr_format/module/attr_<v>.hexa exports
fn attr_format_meta_<v>()                   -> AttrFormatMeta
fn attr_format_parse_<v>(src: str)          -> AttrParseResult
fn attr_format_validate_<v>(decls: [...])   -> AttrValidateResult
fn attr_format_expand_<v>(decls: [...])     -> AttrExpandResult
fn attr_format_catalog_<v>()                -> [str]
```

Caller-visible aggregator: `hexa-lang/attr_format/core/attr_format_main.hexa` runs interface contract checks + v1 catalog detection round-trip (6 categories, byte-identical 2-run).

## Attr version table (canonical)

| Version | Phase | Form | Catalog | Composition | Typing | Status | Spec doc |
|---------|-------|------|---------|-------------|--------|--------|----------|
| **`v1`** | **current** | **comment** | **24** | ✗ | ✗ | **WRAPPED + PRIMARY** | `self/attrs/attrs.json` |
| `v2` | beta | ast | 24 | ✗ | ✗ | SPEC-STUB | `phase_beta_parser_ext_spec.ai.md#L1` |
| `v3` | beta | ast_typed | 24 | ✗ | ✓ | SPEC-STUB | `phase_beta_parser_ext_spec.ai.md#L10` |
| `v4` | delta | composition | 24 | ✓ | ✓ | SPEC-STUB | `phase_delta_language_v2_spec.ai.md#L12` |
| `v5` | future | future | 0 | ✗ | ✗ | FUTURE-STUB | (none — reserved) |

### Breaking change matrix

| Transition | Source change | AST change | Catalog change | Migration tool |
|-----------|---------------|-----------|----------------|----------------|
| v1 → v2 | `// @tool(...)` → `@tool(...)` (drop `//`) | + AttrDecl AST node | none (24 preserved) | regex remove `// ` prefix |
| v2 → v3 | + schema decls in `stdlib/attrs/<name>.hexa` | + AttrSchema validation pass | none | none (additive) |
| v3 → v4 | stacked attrs → `@module {...}` group | + AST transform expander | none | `hexa migrate attr-stack-to-group` |
| v4 → v5 | TBD | TBD | TBD | TBD |

## Attr catalog reference (24 known)

Mirror of `hexa-lang/self/attrs/attrs.json` (SSOT). Drift between this list, `attr_format_catalog_v1()`, and `attr_usage_lint.hexa:KNOWN_ATTRS` = SSOT violation.

```
hot, cold, fuse, memo, bench, profile, inline, noinline, simd, parallel,
allocate, align, unroll, hxblas, trace, audit, self_replay, idempotent,
retry, timeout, checkpoint, invariant, triad, deprecated
```

Plus parser-recognized + ai-native extension set (validation accepts but not in 24-catalog): `tool, sentinel, usage, resolver_bypass, raw117_exempt, pure, memoize, lazy, specialize, dispatch, ossify, refuse, heavy, watchdog, monitor, nonblock, silentexit, specdecode, depth`.

## Invocation patterns

### v1 default (current land state, no env)

```hexa
let src = read_file("path/to/some.hexa")
let route = attr_format_route_parse(src)
// route.final_version == "v1"
// route.result.decls = [AttrDecl{name: "tool", ...}, ...]
```

### Force-single-version (no fallback)

```bash
HEXA_ATTR_FORMAT=v1 hexa <driver>.hexa
```

### Custom lower chain (v4-first, falls through to v1 today)

```bash
HEXA_ATTR_FORMAT_CHAIN=v4,v3,v2,v1 hexa <driver>.hexa
# v4/v3/v2 SPEC-PENDING → router cascades → v1 succeeds
```

### Direct module dispatch (bypass router)

```hexa
use "hexa-lang/attr_format/module/attr_v1"
let pr = attr_format_parse_v1(src)
let vr = attr_format_validate_v1(pr.decls)
```

## Failure cascade (default chain)

```
v1.parse(src)
  ├── ok=1 → return                   (current land state — always works for non-empty src)
  └── ok=0 (empty source / read error)
       └── (no fallback at v1-only chain — caller hard-fails)
```

When a v4-first chain is configured (post-Phase-δ impl), the cascade becomes:

```
v4.parse(src)
  ├── ok=1 → return                   (composition group recognized + expanded)
  └── ok=0 (no @module block / parse error)
       └── v3.parse(src)
            ├── ok=1 → return         (typed AST)
            └── ok=0
                 └── v2.parse(src) → v1.parse(src) → ...
```

**Critical for callers**: when `ok=1`, inspect `meta.form_version` on each decl. Mixed-version decl lists are valid (v3 and v1 can coexist in one source) but `attr_format_lower_to_v1()` is required if downstream consumer only handles v1.

## Adding a new attr-format version (v6+ template)

1. Create `hexa-lang/attr_format/module/attr_v6.hexa` with the 5 exported fns (mirror `attr_v5.hexa` STUB shape).
2. Write spec doc (e.g. `hexa-lang/docs/phase_epsilon_<feature>_spec.ai.md`) — pin grammar, semantics, migration.
3. Implement `attr_format_parse_v6(src)`:
   - Validate inputs.
   - Version-specific tokenizer/AST pass (or wrap parser when first-class lands).
   - Return `attr_parse_ok(decls, msg)` or `attr_parse_fail(reason)`.
4. Register in `hexa-lang/attr_format/core/registry.hexa` (add dispatch case + add to `attr_format_registry_names()`).
5. Add config block to `hexa-lang/config/attr_format_sources.json` with full meta.
6. Add module selftest (`_selftest_v6()`) and aggregator test in `hexa-lang/attr_format/core/attr_format_main.hexa`.
7. Update this README's version table row from FUTURE-STUB to SPEC-STUB → IMPLEMENTED.
8. Land marker: `hexa-lang/state/markers/attr_format_v6_landed.marker`.

Per-version wiring hint:

- **v2 AST first-class** (`attr_v2.hexa`): replace `parser.hexa:591-865` per-attr inline switch with `parse_attr_decl()` returning `AttrDecl`. Backward-compat: comment-form still parsed with deprecation warn. Phase β §3.3 spec.
- **v3 typed params** (`attr_v3.hexa`): introduce `attr <name> { key: type required/optional }` schema syntax; 6 builtin schemas pre-pinned. Phase β §5 spec.
- **v4 composition** (`attr_v4.hexa`): introduce `@module { ... }` group attr; AST transform group → individual; migration tool `hexa migrate attr-stack-to-group`. Phase δ §4 spec.

## raw#10 caveats (read before relying)

1. **v2/v3/v4 parse() returns SPEC-PENDING** — cannot call as if functional. Check `r.ok` and dispatch on `meta.status == "WRAPPED"` first.
2. **v1 catalog list is duplicated** in `attr_format_catalog_v1()`, `attr_format_registry_catalog("v1")`, `_v1_catalog()` (in main aggregator), and `attr_usage_lint.hexa:KNOWN_ATTRS`. SSOT is `attrs.json`. Any divergence = build break (lint should catch).
3. **v1 does NOT structurally parse params** — only the attr name is extracted (`AttrDecl.params = []` always). Structured param parsing is what v2 (AST) introduces.
4. **Composition expand (v4 → v3 lower) algorithm is pinned by Phase δ §4.3 but not impl-verified**. Round-trip property (parse → expand → emit → parse stable) is required at impl time but currently untestable.
5. **`hexa-lang/config/attr_format_sources.json` is tracked** (unlike `anima/config/rng_sources.json` which is gitignored). Changes to this file = governance event.
6. **The 5 preserved files (parser.hexa, attrs.json, attr_usage_lint.hexa, ai_native_lint.hexa, ai_native_scan.hexa) are NOT migrated** — they remain SSOT for their respective concerns. This abstraction wraps them; migration to the router is opt-in per consumer.
7. **`exec("printenv X")` is used for env override resolution** — same env-visibility quirk as `anima/core/rng/router.hexa` (raw#10 §7) may apply in some shell contexts. Direct module dispatch always works.
8. **v1 mode heuristic (`if i < 8 { "file" } else { "decl" }`) is approximate** — real boundary depends on the first non-attr / non-comment statement. v2 AST will have proper placement detection.
9. **Spec link drift** — Phase β/δ specs may change line numbers; `spec_doc` field uses `#L1` / `#L10` / `#L12` anchors that should remain stable. If a spec is restructured, update both the meta and the registry.
10. **Mac-local strict** — all selftests run via `hexa` resolver darwin-bypass; no GPU / heavy compute.

## Verified end-to-end (2026-05-02)

- core/source.hexa contract selftest: PASS (constructor round-trip).
- core/registry.hexa 5-version dispatch: PASS (8 categories — names, meta, unknown sentinel, catalog 24, v1 parse, empty-fail, phase, supports_composition).
- core/router.hexa default chain + lower chain: PASS (8 categories — chain, env resolve, v1 success, v2-v5 SPEC-STUB fail, unknown fail, route_parse, lower_to_v1 pass-through + SPEC-STUB).
- attr_format/core_main.hexa aggregator: PASS (6 categories — interface 5/5, default chain, v1 catalog detection 24/24, composition support 1/5, status sentinel, phase coverage 4/4).
- modules/attr_v1.hexa selftest: PASS (8 categories — meta, catalog, parse, empty-fail, validate known/ext/unknown, expand identity).
- modules/attr_v2.hexa selftest: PASS (meta, catalog 24, parse SPEC-PENDING, validate info, expand fail).
- modules/attr_v3.hexa selftest: PASS (meta, 6 builtin schemas, catalog 24, parse SPEC-PENDING, validate info, expand fail).
- modules/attr_v4.hexa selftest: PASS (meta, 4 group keys, catalog 24, parse SPEC-PENDING, validate info, expand identity for non-module + SPEC-STUB for module).
- modules/attr_v5.hexa selftest: PASS (meta, empty catalog, parse FUTURE-STUB, validate info, expand fail).
- Aggregator byte-identical 2-run: sha256 `12b8fe7d2bf0cc30c07cd31495c08f8f369f5299b32182d79d1d0bf6970e3cd4`.
- Preserved files sha unchanged (5/5): parser.hexa `c0f951d1…`, attrs.json `2741557b…`, attr_usage_lint.hexa `88569c00…`, ai_native_lint.hexa `a155d255…`, ai_native_scan.hexa `ab368379…`.

## File index

| Path | sha256 | LOC |
|------|--------|-----|
| `hexa-lang/attr_format/core/source.hexa` | `dafabd6e798a07cd5cef69f8848fe191c27d7a7614b0c217c43a1be3cbe151ac` | 266 |
| `hexa-lang/attr_format/core/registry.hexa` | `39d6072382b9f58e772119e60dfbb5354428486c851ec8ae71aa9c0e520ec802` | 341 |
| `hexa-lang/attr_format/core/router.hexa` | `6d852ace2052a830ef847c12971a777d38190f72b4796383c499a030338fe2e2` | 291 |
| `hexa-lang/attr_format/core/attr_format_main.hexa` | `947f81959ad67d95960dcfd4e63cfc9e44ff4fc50243390e1d63cfe2a5b033aa` | 272 |
| `hexa-lang/attr_format/module/attr_v1.hexa` | `57a3fef32570c68823cf4462c6c7de6908ca79bd05096d65bbae46718f351d8a` | 348 |
| `hexa-lang/attr_format/module/attr_v2.hexa` | `d2f995ff454489ad27ec447b5d235a4cdfbf662702af40f67138b95a8f1b4f10` | 207 |
| `hexa-lang/attr_format/module/attr_v3.hexa` | `83691fe4860518e5a461eba993b0c38e2e4ca4fe8665ce56eba4d52e4b74b9ba` | 227 |
| `hexa-lang/attr_format/module/attr_v4.hexa` | `ea2744974ca02e592113df495002f9f0ad560f0f24ee7a878619837336cc8110` | 243 |
| `hexa-lang/attr_format/module/attr_v5.hexa` | `9bdf82a2b7ef31dd938d6d42c3260d856c4ce0ea4f3e74eec553adde87e57111` | 175 |
| `hexa-lang/config/attr_format_sources.json` | `e405b9fbbbd76d33f3623077583e6a5dd15ebe333938e998cbe62df2bbac0bbe` | 88 |

shas at land time. After any edit, re-pin via `shasum -a 256` and update this table + the marker.

## Cross-link to sister Levels

- **Level 0 (prototype)**: `anima/anima/{core,modules}/rng/` — 6-source RNG abstraction (raw 270/271/272/273 conformant).
- **Level 1 (raw)**: `BG-raw-format` — raw evolution wrapper (in flight).
- **Level 2 (hxc)**: `BG-hxc-format` — hxc evolution wrapper (in flight).
- **Level 3a (grammar)**: `hexa-lang/{core,modules}/grammar_format/` — grammar evolution wrapper (in flight by sister BG).
- **Level 3b (this)**: `hexa-lang/{core,modules}/attr_format/` — attr evolution wrapper (this BG).

These 5 layers are perpendicular sub-axes of a single recursive dogfooding axis: every artifact format that hexa-lang produces is itself wrappable as a core+module pattern, allowing future evolution to land as a new module without touching the existing layer.
