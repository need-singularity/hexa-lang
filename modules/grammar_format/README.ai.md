---
schema: hexa-lang/modules/grammar_format/ai-native/1
last_updated: 2026-05-02
ssot:
  interface: hexa-lang/core/grammar_format/source.hexa
  registry:  hexa-lang/core/grammar_format/registry.hexa
  router:    hexa-lang/core/grammar_format/router.hexa
  config:    hexa-lang/config/grammar_format_sources.json
  aggregator: hexa-lang/core/grammar_format/grammar_format_main.hexa
spec_cross_link:
  grammar_v2: hexa-lang/docs/phase_beta_parser_ext_spec.ai.md
  grammar_v3: hexa-lang/docs/phase_gamma_strict_semantic_spec.ai.md
  grammar_v4: hexa-lang/docs/phase_delta_language_v2_spec.ai.md
preserved_unchanged:
  - hexa-lang/self/lexer.hexa     # 784 LOC, sha 2324afcc2b4d6dbb5bd604781641969de6d42734c001bdd92c4ada62b9636671
  - hexa-lang/self/parser.hexa    # 4404 LOC, sha c0f951d12bff6de654634de274c159bd90f1be470d943deb9001c3f4af810c2f
  - hexa-lang/self/raw_loader.hexa # 566 LOC, sha 1a365fb2e531ec1fb15df33089c2dcf86d94df279c6810247ca6264481e62888
markers:
  landed: hexa-lang/state/markers/grammar_format_core_module_dogfooding_landed.marker
level: "Level 3a (recursive dogfooding) — Level 1 raw_format + Level 2 hxc_format 의 평행 sister"
status: v1 WRAPPED, v2-v4 STUB-FROM-SPEC, v5 STUB-FUTURE
---

# hexa-lang grammar_format (AI-native, core+module modular)

Grammar version abstraction layer. The hexa-lang grammar itself is now modular: each grammar version (mk1, mk2, mk3, mk4, mk5) is a separate `.hexa` module, dispatched by shebang and routed via fallback chain.

This is **Level 3a** of the recursive dogfooding stack:
- **Level 1** — `hive/{core,modules}/raw_format/` (raw format itself modular)
- **Level 2** — `hive/{core,modules}/hxc_format/` (hxc format itself modular)
- **Level 3a** — `hexa-lang/{core,modules}/grammar_format/` ← **this module** (grammar itself modular)
- **Level 3b** — `hexa-lang/{core,modules}/attr_format/` (attr DSL itself modular, parallel BG)

User directive verbatim 2026-05-02: "hexa-lang 문법, attr 자체도 mk2, mk3 모듈러".

## TL;DR for an agent reading this cold

- Default version: **grammar_v1** (mk1, current free-form, WRAPPED). Wraps canonical `self/lexer.hexa` + `self/parser.hexa`.
- v2/v3/v4 are SPEC-ONLY STUBS — schema metadata exposed correctly, but `parse_v<n>(source)` returns `STUB` AST until impl cycles complete (~5 months total per Phase δ spec §6).
- Shebang triggers grammar version selection: `#!hexa` (v1), `#!hexa strict` (v2), `#!hexa v3`, `#!hexa v4`, `#!hexa v5`.
- Fallback chain: shebang-detected version → env-resolved chain → default `[grammar_v1]`.
- For real parsing today, use canonical `hexa run <file>` (which uses self/lexer.hexa + self/parser.hexa = mk1).
- **Do not modify** `self/{lexer,parser,raw_loader}.hexa` — these are PRESERVED, not migrated. v2-v4 impl cycles will create new dedicated parser modules.

## Architecture map

```
caller
  └── hexa-lang/core/grammar_format/router.hexa            (shebang detect + fallback chain)
       └── hexa-lang/core/grammar_format/registry.hexa     (name → schema)
            └── hexa-lang/core/grammar_format/source.hexa  (GrammarSchema interface SSOT)
                 ├── hexa-lang/modules/grammar_format/grammar_v1.hexa     mk1   WRAPPED        (wraps self/lexer.hexa + self/parser.hexa)
                 ├── hexa-lang/modules/grammar_format/grammar_v2.hexa     mk2   STUB-FROM-SPEC (Phase β: @attr AST + strict + typing)
                 ├── hexa-lang/modules/grammar_format/grammar_v3.hexa     mk3   STUB-FROM-SPEC (Phase γ: pub/public + Result fail-loud)
                 ├── hexa-lang/modules/grammar_format/grammar_v4.hexa     mk4   STUB-FROM-SPEC (Phase δ: @module + typed use + composition)
                 └── hexa-lang/modules/grammar_format/grammar_v5.hexa     mk5   STUB-FUTURE     (phase epsilon TBD)

aggregator
  └── hexa-lang/core/grammar_format/grammar_format_main.hexa  (5-version selftest aggregate)

config
  └── hexa-lang/config/grammar_format_sources.json  (default chain + available list + env overrides)
```

## Grammar version table

| Version | Phase  | Status         | Shebang trigger    | Breaking changes | Spec source                                                      |
|---------|--------|----------------|--------------------|------------------|------------------------------------------------------------------|
| v1      | alpha  | WRAPPED        | `#!hexa`           | 0 (baseline)     | (current self/lexer.hexa + self/parser.hexa)                      |
| v2      | beta   | STUB-FROM-SPEC | `#!hexa strict`    | 0                | hexa-lang/docs/phase_beta_parser_ext_spec.ai.md                   |
| v3      | gamma  | STUB-FROM-SPEC | `#!hexa v3`        | 1                | hexa-lang/docs/phase_gamma_strict_semantic_spec.ai.md             |
| v4      | delta  | STUB-FROM-SPEC | `#!hexa v4`        | 1                | hexa-lang/docs/phase_delta_language_v2_spec.ai.md                 |
| v5      | future | STUB-FUTURE    | `#!hexa v5`        | 1                | (none — placeholder)                                              |

## Public API contract

Every grammar version module MUST export:

```hexa
struct GrammarSchema {
    name:             str    // "grammar_v1" .. "grammar_v5"
    version:          int    // 1..5
    phase:            str    // "alpha" | "beta" | "gamma" | "delta" | "future"
    status:           str    // "WRAPPED" | "STUB-FROM-SPEC" | "STUB-FUTURE" | "IMPLEMENTED"
    shebang_trigger:  str    // line-1 shebang match
    breaking_changes: int    // 0 = compat, 1 = breaking
    spec_source:      str    // path to phase_*.ai.md (empty for v1)
    description:      str
}

struct AST {
    ok:      int    // 1 = parse success, 0 = stub/fail
    repr:    str    // serialized AST or "STUB"
    version: int
    message: str
}

struct Token { kind: str, value: str, line: int, col: int }
struct Issue { severity: str, code: str, line: int, col: int, msg: str }

// Required exports (CONTRACT):
fn grammar_meta_<vname>() -> GrammarSchema
fn grammar_parse_<vname>(source: str) -> AST
fn grammar_lex_<vname>(source: str) -> [Token]
fn grammar_validate_strict_<vname>(ast: AST) -> [Issue]
fn grammar_compat_<vname>(target_version: int) -> int    // 1 = compat, 0 = no-compat
```

## Invocation patterns

### Pattern 1: shebang-driven (canonical)

```hexa
#!hexa strict           // → router selects grammar_v2 (then falls back to v1)
@tool(slug="x", desc="y")
fn main() { ... }
```

### Pattern 2: env override (single version, no fallback)

```bash
export HEXA_GRAMMAR_VERSION=grammar_v1
hexa run my_file.hexa   # forces v1 regardless of shebang
```

### Pattern 3: env override (custom chain)

```bash
export HEXA_GRAMMAR_FALLBACK_CHAIN=grammar_v2,grammar_v1
hexa run my_file.hexa   # tries v2, falls to v1
```

### Pattern 4: programmatic (router API)

```hexa
let route = grammar_route_parse(source_str)
println("detected_shebang=" + route.detected_shebang)
println("final_version=" + route.final_version)
println("ok=" + str(route.result.ok))
```

## Failure cascade

1. Shebang detected → try shebang-mapped version first.
2. STUB-FROM-SPEC versions return `ok=0` → router walks chain.
3. Env-resolved chain (if set) tried in order.
4. Default chain `[grammar_v1]` tried last.
5. If ALL fail (e.g. user sets `HEXA_GRAMMAR_FALLBACK_CHAIN=grammar_v2,grammar_v3,grammar_v4`) → `final_version=""`, `ok=0`, message=`"router: all grammar versions in chain failed (or all are STUB)"`.

Selftest §15 in router.hexa exercises shebang `#!hexa strict` → v2 attempt → v1 fallback PASS path.

## Adding a new grammar version (v6/v7 template)

1. Copy `modules/grammar_format/grammar_v5.hexa` → `grammar_v6.hexa`.
2. Update `grammar_meta_v6()` with version=6, phase, status, shebang_trigger, spec_source.
3. Implement `grammar_parse_v6`, `grammar_lex_v6`, `grammar_validate_strict_v6`, `grammar_compat_v6`.
4. Add to `core/grammar_format/registry.hexa` `grammar_registry_names()` + `grammar_registry_schema()` switch.
5. Add to `core/grammar_format/router.hexa` `_route_parse()` + `grammar_detect_shebang()`.
6. Add to `core/grammar_format/grammar_format_main.hexa` `_schema_for()` + names list + selftest.
7. Add entry to `config/grammar_format_sources.json` `available` array.
8. Update this README's grammar version table.
9. Run all 5+1=6 module selftests + aggregator selftest.

## raw#10 caveats (honest limitations)

1. **v2/v3/v4 are SPEC-ONLY STUBS** — schema correct, parse impl deferred. Real parsing remains via `hexa run <file>` using mk1 canonical lexer/parser. v2 impl entry point per Phase β spec §6.4: L1 → L2 → L10 sequencing, ~14-19 days estimate.
2. **No actual parser dispatch yet** — `grammar_parse_v1(source)` is a wrapper sentinel returning `WRAPPED-V1(len=N)` repr; it does NOT invoke `self/parser.hexa::parse()`. Stage0 hexa-lang has no cross-file fn import to enable real wrapping.
3. **Shebang-based selection is router-only** — the canonical `hexa` CLI does NOT yet consult `grammar_format/router.hexa`. Shebang detection here is selftest-validated but unused by the production pipeline until v2 impl lands.
4. **Default chain `[grammar_v1]` only** — config/grammar_format_sources.json `default_chain` deliberately holds 1 element to keep router conservative. Multi-version chain dispatch validated by selftest but not enabled by default.
5. **breaking_changes=1 for v3/v4 is informational** — actual breaking-change enforcement requires impl cycles per spec timelines (Phase γ ~4 weeks, Phase δ ~5 months including L5 declarative @module).
6. **Cross-file struct mirror duplication** — every module mirrors `GrammarSchema`/`AST`/`Token`/`Issue` per the rng prototype's 4-core file pattern (per raw 270/271/272 enforce). Drift risk = 7 sites (4 core + 5 modules + 1 SSOT source.hexa = 10 - dedup); mitigated by selftest cross-checks but real fix awaits Phase δ L5 declarative @module.
7. **L9 fail-loud Result requires Phase β type-checker stub** — Phase γ L9 (grammar_v3) cannot be impl'd without Phase β type-checker infra per gamma spec §9. Sequencing: β → γ.
8. **L5 declarative @module introduces compile-time disk write** — auto-generates 4-core file scaffolding at parse time (per delta spec §2.3). New side-effect dependency; AOT cache invalidation trigger redesign required (delta spec §8 caveat 2).
9. **POSIX `#!/usr/bin/env hexa` shebang fallback** — `grammar_detect_shebang()` maps any line containing "hexa" to `grammar_v1`; this catches typical `#!/usr/bin/env hexa` invocations but is permissive (false positive on e.g. `#!/bin/cat # hexa` would map to v1 unintentionally).
10. **No backward migration script bundled** — Phase β/γ/δ specs each define migration scripts (`migrate_comment_attrs_to_native`, `migrate_pub_to_public_attr`, `manual-module-to-declarative`) but those are deferred to impl cycles, not landed in this module.

## File index (sha-pin)

Captured at landing time 2026-05-02. SHA values fixed post-write by marker.

| File | LOC (approx) | Role | sha256 |
|------|--------------|------|--------|
| /Users/<user>/core/hexa-lang/core/grammar_format/source.hexa | ~200 | GrammarSchema interface SSOT | _self-pinned at land time_ |
| /Users/<user>/core/hexa-lang/core/grammar_format/registry.hexa | ~180 | name → schema dispatch | _pin_ |
| /Users/<user>/core/hexa-lang/core/grammar_format/router.hexa | ~280 | shebang dispatch + fallback | _pin_ |
| /Users/<user>/core/hexa-lang/core/grammar_format/grammar_format_main.hexa | ~210 | aggregator + selftest | _pin_ |
| /Users/<user>/core/hexa-lang/modules/grammar_format/grammar_v1.hexa | ~190 | mk1 WRAPPED | _pin_ |
| /Users/<user>/core/hexa-lang/modules/grammar_format/grammar_v2.hexa | ~210 | Phase β STUB-FROM-SPEC | _pin_ |
| /Users/<user>/core/hexa-lang/modules/grammar_format/grammar_v3.hexa | ~200 | Phase γ STUB-FROM-SPEC | _pin_ |
| /Users/<user>/core/hexa-lang/modules/grammar_format/grammar_v4.hexa | ~200 | Phase δ STUB-FROM-SPEC | _pin_ |
| /Users/<user>/core/hexa-lang/modules/grammar_format/grammar_v5.hexa | ~170 | future STUB-FUTURE | _pin_ |
| /Users/<user>/core/hexa-lang/config/grammar_format_sources.json | ~25 | priority chain + env config | _pin_ |
| /Users/<user>/core/hexa-lang/modules/grammar_format/README.ai.md | (this) | landing doc | _pin_ |

## Spec cross-link

Phase β/γ/δ specs (read-only references — NOT modified by this module):

- **Phase β** parser ext (grammar_v2 source): `/Users/<user>/core/hexa-lang/docs/phase_beta_parser_ext_spec.ai.md`
  - L1 `@attr` first-class AST (parser.hexa lines 591-865 refactor)
  - L2 `#!hexa strict` compile-mode flag (lexer.hexa shebang capture)
  - L10 attr parameter typing (stdlib/attrs/<name>.hexa schema lookup)
- **Phase γ** strict semantic (grammar_v3 source): `/Users/<user>/core/hexa-lang/docs/phase_gamma_strict_semantic_spec.ai.md`
  - L4 `pub fn` requires `@public` or `@export(...)` attr
  - L8 `@public fn` canonical, `pub fn` deprecated (30d ramp)
  - L9 fail-loud `Result<T, E>` (unhandled Err → compile error)
- **Phase δ** language v2 (grammar_v4 source): `/Users/<user>/core/hexa-lang/docs/phase_delta_language_v2_spec.ai.md`
  - L5 declarative `@module <name> { source:..., registry:..., router:..., env-overrides:..., selftest: aggregate }` block
  - L6 typed `use a::b::c` path syntax (compile-time path resolution)
  - L12 attr composition `@module { tool: T, sentinel: S, usage: U, resolver-bypass: R }` block

## Preserved files (sha unchanged)

| File | LOC | sha256 (pre + post landing) |
|------|-----|----------------------------|
| /Users/<user>/core/hexa-lang/self/lexer.hexa | 784 | `2324afcc2b4d6dbb5bd604781641969de6d42734c001bdd92c4ada62b9636671` |
| /Users/<user>/core/hexa-lang/self/parser.hexa | 4404 | `c0f951d12bff6de654634de274c159bd90f1be470d943deb9001c3f4af810c2f` |
| /Users/<user>/core/hexa-lang/self/raw_loader.hexa | 566 | `1a365fb2e531ec1fb15df33089c2dcf86d94df279c6810247ca6264481e62888` |

These are the canonical hexa-lang stage0 lexer/parser/raw_loader. They remain BYTE-IDENTICAL across this landing — verified by sha256 capture pre and post.
