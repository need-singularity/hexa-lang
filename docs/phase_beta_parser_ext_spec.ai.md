---
schema: hexa-lang/docs/phase_beta_parser_ext_spec/ai-native/1
last_updated: 2026-05-02
phase: beta
status: spec_only_impl_deferred
depends_on:
  - self/parser.hexa@4404L
  - self/lexer.hexa@784L
  - self/ast.hexa@339L
  - tool/ai_native_lint.hexa@687L
  - hive/tool/attr_usage_lint.hexa
sister_specs:
  - self/attrs/attrs.json
  - self/ai_native/ai_native.json
marker: state/markers/phase_beta_parser_ext_spec_landed.marker
items:
  - L1: "@attr first-class AST decorator"
  - L2: "#!hexa strict compile-mode flag"
  - L10: "attr parameter typing (schema-driven)"
impl_eta_days:
  L1: 4-6
  L2: 2-3
  L10: 5-7
total_eta_days: 11-16
backward_compat: required
destructive: 0
---

# Phase β — Parser Extension Spec (L1 + L2 + L10)

Spec for promoting 3 lint-level conventions to first-class hexa-lang parser features. Implementation deferred — this doc lands design + impl checklist + migration plan only.

## §1 TL;DR

- **L1**: `@attr(...)` becomes first-class AST decorator (`AttrDecl` node), replaces lint-level `// @tool(...)` comment grep. Backward-compat: comment form deprecated, warn-only for 30d.
- **L2**: `#!hexa strict` shebang already lexer-skipped; promote to parser strict-mode flag; in strict, missing file-level `@tool` → compile error (was: lint warn).
- **L10**: Attr parameters get schema-typed (`@tool(slug: str required, desc: str required)`); compiler validates at parse time against `stdlib/attrs/<name>.hexa` (or builtin registry); invalid → compile error.

## §2 Current parser audit (Step 1 finding)

| Concern | Current state | Path | Notes |
|---|---|---|---|
| `@` token | Lexer emits `Token{kind:"At", value:"@"}` | self/lexer.hexa:624 | Tokenized but not promoted to AST |
| `@attr` parsing | Parser consumes in `parse_stmt` while-loop | self/parser.hexa:591-865 | Per-attr branches: `invariant`/`contract`/`symbol`/`link`/`select`/`cli`/`flag`/`doc`/`schema`/`gate`/`memo`/`depth`; rest fall through as comma-joined string |
| `p_pending_attrs` accumulator | Module-state global | self/parser.hexa:157 | Serialized as `"name1,name2,name3"` into FnDecl.attrs (string) |
| FnDecl.attrs | `attrs: string` (serialized) | self/ast.hexa:201 | Not first-class AST nodes; downstream consumers re-parse string |
| Shebang | Lexer skips entire line if begins `#!` | self/lexer.hexa:140-145 | No mode flag emitted; `#!hexa strict` vs `#!hexa lax` indistinguishable to parser |
| Strict mode | Implemented in lint only | tool/ai_native_lint.hexa:291-306 (`has_strict_header`) | Text-grep on lines 1-2; parser ignores |
| `parse_strict` fn | Distinct from strict-mode | self/parser.hexa:556 | Halts on first error vs error-recovery; orthogonal axis (parse-error policy ≠ strict compile-mode) |
| Mid-file shebang | Error: "shebang must be on line 1" | self/lexer.hexa:745-754 | Already enforced |
| `@tool(slug=, desc=)` | Comment-level only | hive/tool/attr_usage_lint.hexa:1-9 | Convention: `// @tool(...)` lines 7-9 in module header; not parsed |
| `@sentinel`/`@usage` | Same comment convention | (same files) | Used by attr_usage_lint via grep |
| `@symbol`/`@link` | Already parsed with `("string")` arg | self/parser.hexa:656-674 | Single-string-arg precedent for L10 schema typing |
| Attrs catalog | self/attrs/attrs.json (24 attrs) | + self/ai_native/ai_native.json | Has `purpose`/`category`/`requires`/`auto_test` schema fields, but parser does not consult |

**Key insight**: parser already has `At` token + per-attr branches. L1 = lift the per-attr branch table into a generic AttrDecl AST node. L2 = read `#!hexa strict` from line-1 source (currently discarded by lexer skip). L10 = wire self/attrs/attrs.json into validate-on-parse pass.

## §3 L1 spec — `@attr` first-class AST

### 3.1 Syntax (unchanged from current convention)

```hexa
@tool(slug="foo", desc="bar")
@sentinel(__FOO__ <PASS|FAIL> mode=<once|selftest>)
@usage(hexa tool/foo.hexa [--once])
fn main() {
    // ...
}
```

Multiple stackable attrs allowed. File-level (top, after shebang) and decl-level (before fn/struct/let/const) supported.

### 3.2 New AST nodes

```hexa
// self/ast.hexa additions
struct AttrDecl {
    name: string,           // e.g. "tool", "sentinel"
    params: string,         // serialized [(key, value, kind)] — kind ∈ {str, int, bool, ident, raw}
    line: int,              // source line for error msgs
    col: int,
    mode: string            // "file" | "decl" — placement
}

struct FileAttrs {
    attrs: string           // serialized [AttrDecl] — file-level (top of file)
}
```

FnDecl.attrs (existing `string`) gets a sister field `attrs_typed: string` (serialized `[AttrDecl]`); old `attrs` retained for codegen consumers (deprecate after migration).

### 3.3 Parser changes

| Site | Change |
|---|---|
| `parse_stmt` line 591-865 (the `while p_peek_kind() == "At"` loop) | Replace per-attr if/else chain with single `parse_attr_decl()` that returns `AttrDecl`; per-attr semantics (invariant DSL, contract requires/ensures, select arms) move to dedicated `parse_<name>_args` helpers dispatched by name |
| New `parse_file_attrs()` | Called once after shebang skip, before first statement; collects file-level `@attr` block until non-attr token |
| Backward-compat: `// @tool(...)` comment | Lexer emits a synthetic `CommentAttr` token if comment matches `^//\s*@\w+\(`; parser warns "deprecated form, use bare `@tool(...)`"; migration script (§7) removes the `//` |

### 3.4 Examples

```hexa
#!hexa strict
@tool(slug="foo_runner", desc="raw 3 entrypoint")
@sentinel(__FOO_RUNNER__ <PASS|FAIL>)
@usage(hexa tool/foo_runner.hexa)

// File-level attrs above. Decl-level below.

@pure
@memoize
fn helper(x: int) -> int {
    return x * 2
}
```

After parse, AST root contains `FileAttrs{[tool, sentinel, usage]}` and `helper` FnDecl has `attrs_typed = [pure, memoize]` (each AttrDecl).

## §4 L2 spec — `#!hexa strict` compile mode

### 4.1 Activation

| Source line 1 | Mode | Behavior |
|---|---|---|
| `#!hexa strict` | strict | file-level @tool required (compile error if missing); silent error patterns → error (Phase γ) |
| `#!hexa lax` | lax | warn-only (matches current `// @raw117_exempt` legacy path) |
| `#!/usr/bin/env hexa` | default | follows `--default-strict` CLI flag (default: strict, since 2026-04-18 raw 11) |
| (no shebang) | default | same as above |
| Mid-file `#!hexa …` | error | already enforced (lexer.hexa:745-754) |

### 4.2 Lexer change (minimal)

```hexa
// self/lexer.hexa:140 — augment shebang skip to capture mode
if total >= 2 && chars[0] == '#' && chars[1] == '!' {
    let line1 = <slice from pos 0 to first \n>
    if line1 == "#!hexa strict" { tokens.push(Token{kind:"ShebangMode", value:"strict", line:1, col:1}) }
    else if line1 == "#!hexa lax" { tokens.push(Token{kind:"ShebangMode", value:"lax", line:1, col:1}) }
    // else: skip silently (POSIX shebang or no mode marker)
    while pos < total && chars[pos] != '\n' { pos = pos + 1; col = col + 1 }
}
```

### 4.3 Parser change

```hexa
// self/parser.hexa — at parse() entry
let mut compile_mode = "default"   // "strict" | "lax" | "default"
if p_peek_kind() == "ShebangMode" {
    let tok = p_advance()
    compile_mode = tok.value
}
// After parse_file_attrs():
if compile_mode == "strict" {
    let has_tool = file_attrs.attrs.contains("tool|")
    if !has_tool {
        eprintln("error: #!hexa strict requires file-level @tool(slug, desc) attribute")
        eprintln("hint: add `@tool(slug=\"<name>\", desc=\"<purpose>\")` after shebang")
        exit(1)
    }
}
```

### 4.4 Strict-mode enforcement table (Phase β scope)

| Rule | Phase β | Phase γ | Phase δ |
|---|---|---|---|
| File-level `@tool` required | YES | (kept) | (kept) |
| Public fn `@pub` or `@export` attr | NO | YES | (kept) |
| `Result` ignored (silent error) | NO | NO | YES |
| L2 lint (numeric literal compares) | (lint stays) | promoted | (kept) |

## §5 L10 spec — attr parameter typing

### 5.1 Schema declaration syntax

```hexa
// stdlib/attrs/tool.hexa (new file)
attr tool {
    slug: str required,
    desc: str required,
    version: str optional default "0.1.0",
}

// stdlib/attrs/sentinel.hexa
attr sentinel {
    marker_name: ident required,        // __TOOL_NAME__
    status_options: list<str> required, // [PASS, FAIL]
    mode: ident optional,               // once | selftest
}

// stdlib/attrs/usage.hexa
attr usage {
    invocation: raw required,           // free-form CLI line
}
```

### 5.2 Builtin schemas (Phase β minimum set)

| attr | required keys | optional keys | source |
|---|---|---|---|
| tool | slug:str, desc:str | version:str | spec |
| sentinel | marker_name:ident, status_options:list<str> | mode:ident | spec |
| usage | invocation:raw | — | spec |
| resolver-bypass | reason:str | — | spec |
| pure | — | — | self/attrs/pure.hexa |
| memoize | — | requires:str | self/attrs/memoize.hexa |
| symbol | name:str | — | parser.hexa:656 |
| link | name:str | — | parser.hexa:656 |
| depth | n:int | — | parser.hexa:799 |
| invariant | (DSL block, see parse_invariant_decl) | — | parser.hexa:608 |
| contract | requires:expr, ensures:expr | — | parser.hexa:612 |

24 ai-native attrs from hive/tool/attr_usage_lint.hexa:KNOWN_ATTRS get progressive schema rollout (start with `--no-validate` flag for unknown attrs).

### 5.3 Validation pass

```hexa
// New: self/attr_validate.hexa
fn validate_attr_decl(attr: AttrDecl, schema: AttrSchema) -> [Error] {
    let mut errors = []
    // Required keys missing?
    for key in schema.required {
        if !attr.params.has_key(key) {
            errors.push(Error{
                line: attr.line, col: attr.col,
                msg: "@" + attr.name + " missing required key `" + key + "`"
            })
        }
    }
    // Extra keys?
    for (key, _) in attr.params {
        if !schema.required.contains(key) && !schema.optional.contains(key) {
            errors.push(Error{
                line: attr.line, col: attr.col,
                msg: "@" + attr.name + " unknown key `" + key + "` (allowed: " + ... + ")"
            })
        }
    }
    // Type check each param
    for (key, val) in attr.params {
        let expected = schema.type_of(key)
        if !val.matches_type(expected) {
            errors.push(Error{...})
        }
    }
    return errors
}
```

Unknown attr (no schema): warn (`--strict-attrs` flag → error).

## §6 Impl checklist

### 6.1 L1 @attr AST (5 file changes, ~4-6d)

- [ ] **lexer**: confirm `@<ident>` tokenization (already done at line 624) — no change needed
- [ ] **ast.hexa**: add `AttrDecl` + `FileAttrs` structs (§3.2)
- [ ] **parser.hexa**: extract `parse_attr_decl()` from inline while-loop (lines 591-865); call sites: `parse_stmt`, new `parse_file_attrs`
- [ ] **parser.hexa**: add `parse_file_attrs()` after shebang skip
- [ ] **parser.hexa**: visitor — attach `attrs_typed: [AttrDecl]` to FnDecl/StructDecl/EnumDecl/LetStmt
- [ ] **parser.hexa**: AST printer round-trip — `print_attr_decl(a)` → `@<name>(<k>=<v>, ...)`
- [ ] **selftest** test/test_parser_attr_decl.hexa: 5 fixtures
  - single attr no params: `@pure fn f(){}`
  - single attr with params: `@tool(slug="x", desc="y") fn f(){}`
  - stacked decl-level: `@pure @memoize fn f(){}`
  - file-level block: shebang + 3 file attrs + first fn
  - mixed file + decl: file-level @tool + decl-level @pure on first fn
- [ ] **backward-compat**: lexer emits `CommentAttr` token for `// @<x>(`; parser warns deprecated; opt-in via `HEXA_COMMENT_ATTR_LEGACY=1`
- [ ] **codegen consumers**: keep FnDecl.attrs string format for old consumers (codegen_c2:gen2_has_attr); shim `attrs_typed → attrs string serialize` at parse-end

### 6.2 L2 strict mode (3 file changes, ~2-3d)

- [ ] **lexer.hexa**: add ShebangMode token emit (§4.2); read line-1 chars before skip
- [ ] **parser.hexa**: read ShebangMode at parse() entry; store in `compile_mode` global
- [ ] **parser.hexa**: post-`parse_file_attrs()` strict check — missing @tool → error with hint
- [ ] **CLI**: respect `--default-strict` flag (already in tool/ai_native_lint.hexa); apply to no-shebang files
- [ ] **selftest** test/test_parser_strict_mode.hexa:
  - strict + @tool present → pass
  - strict + @tool missing → compile error
  - lax (no @tool) → pass
  - default-strict + no shebang + no @tool → error
  - default-strict + no shebang + @tool → pass
- [ ] **migration**: add `#!hexa lax` to legacy files via `tool/migrate_add_lax_header.hexa`

### 6.3 L10 attr param typing (7 file changes, ~5-7d)

- [ ] **ast.hexa**: add `AttrSchema` + `AttrParam` structs
- [ ] **stdlib/attrs/<name>.hexa**: 6 builtin schema files (tool, sentinel, usage, resolver-bypass, pure, memoize)
- [ ] **stdlib/attrs/_registry.hexa**: load all stdlib/attrs/*.hexa at parse-init; expose `attr_schema_lookup(name) -> Option<AttrSchema>`
- [ ] **self/attr_validate.hexa**: new module — `validate_attr_decl(attr, schema)` + `validate_all(file_ast) -> [Error]`
- [ ] **parser.hexa**: invoke validate at end-of-parse if compile_mode == "strict"; collect errors before exit
- [ ] **error messages**: human-readable diffs ("missing key `slug`; got: [desc]")
- [ ] **selftest** test/test_attr_typing.hexa:
  - valid: `@tool(slug="x", desc="y")` → pass
  - missing required: `@tool(slug="x")` → error "missing `desc`"
  - extra key: `@tool(slug="x", desc="y", foo="z")` → error "unknown `foo`"
  - wrong type: `@depth(n="abc")` → error "expected int, got str"
  - unknown attr: `@unknownattr(x=1)` → warn (or error with `--strict-attrs`)
- [ ] **CLI**: `hexa --strict-attrs file.hexa` flag

### 6.4 Sequencing

L1 → L2 → L10 (each depends on prior).

L1 blocks L2 (file-level attrs needed for strict-check).
L2 blocks L10 (strict mode is the trigger for type validation).

## §7 Backward compat & migration plan

### 7.1 Affected file inventory (estimated)

| Repo | .hexa files | comment-attr files (~est) | shebang missing |
|---|---|---|---|
| anima | ~800 | ~30 | ~200 |
| hive | ~500 | ~50 | ~100 |
| hexa-lang/{self,tool,stdlib} | ~600 | ~10 | ~50 |
| airgenome | ~150 | ~5 | ~30 |
| n6 | ~200 | ~5 | ~40 |
| **total** | **~2250** | **~100** | **~420** |

(Exact counts deferred to migration cycle — `find . -name '*.hexa' | wc -l` per repo.)

### 7.2 30-day migration ramp

| Day | Phase β state | Action |
|---|---|---|
| D+0 (impl land) | comment-attr WARN, bare-attr REQUIRED in new files | publish migration tool |
| D+0..D+30 | comment-attr WARN | run migrate script per-repo, review diffs |
| D+30 | comment-attr ERROR (strict mode), WARN (default) | grandfather via `.hexa-attr-baseline` (analog to `.hexa-lax-baseline`) |
| D+90 | comment-attr ERROR (default mode) | full removal |

### 7.3 Migration script spec

**Path**: `hexa-lang/tool/migrate_comment_attrs_to_native.hexa`

```hexa
#!hexa strict
@tool(slug="migrate_comment_attrs", desc="convert // @<x>(...) → @<x>(...) in .hexa files")
@sentinel(__MIGRATE_COMMENT_ATTRS__ <PASS|FAIL> files=<N> changed=<M>)
@usage(hexa tool/migrate_comment_attrs_to_native.hexa <root> [--dry-run] [--verify])

// Behavior:
//   1. find <root> -name '*.hexa' | xargs grep -l '^// @\w\+('
//   2. per file: regex `^// (@\w+\([^)]*\))$` → `\1`
//   3. --verify: re-parse modified file; if AttrDecl matches old comment-attr text → pass
//   4. emit JSON report: state/migrate_comment_attrs_<ts>.jsonl
//   5. dry-run: print diffs only, no write
```

**Validation**: byte-equivalence test — parse pre-migration file with comment-attr backward-compat enabled, parse post-migration file with new AttrDecl path; serialize both to canonical JSON; require sha256 equality.

## §8 raw#10 caveats

1. **AST-level breaking change for downstream codegen** — `FnDecl.attrs` serialized-string consumers (codegen_c2:gen2_has_attr) require shim during transition; risk: stale codegen path reads `attrs_typed` field that doesn't exist yet → compile error in interpreter bootstrap. Mitigation: introduce `attrs_typed` as optional field with empty default for 1 cycle.
2. **L1 file-level vs decl-level ambiguity** — current `parse_stmt` while-loop attaches all `@attr` to *next decl*. New `parse_file_attrs()` must define cutoff: "first non-attr non-comment non-newline token". Edge case: `#!hexa strict\n@tool(...)\n\n// big header comment\n\nfn main(){}` — file-level vs decl-level depends on whether comment counts as separator.
3. **L2 `parse_strict` fn name collision** — `self/parser.hexa:556 parse_strict` is parse-error policy (halt-on-first-error), orthogonal to `#!hexa strict` compile-mode. Naming: rename existing to `parse_halt_on_error()` to avoid future confusion. Risk: 4404L parser, every internal caller must update.
4. **L10 schema circular dependency** — `stdlib/attrs/<name>.hexa` declares `attr <name> { ... }`; but the `attr` keyword itself doesn't exist yet. Bootstrap: hardcode 6 builtin schemas in `self/attr_validate.hexa` for Phase β; user-defined schemas defer to Phase γ.
5. **Comment-attr backward-compat false-positive** — `// @link("not a real link")` inside a code comment unrelated to attrs may match regex. Mitigation: require comment-attr to be at file-top (before first non-comment non-shebang line) AND match strict regex.
6. **Lexer line-1 capture cost** — adding shebang-mode read to lexer increases line-1 hot-path; benchmark: must not regress >1% on `bench/lexer_throughput.hexa`. Mitigation: cap to first 32 chars match.
7. **Schema language not yet defined** — `attr <name> { key: type required }` syntax is new; `required`/`optional`/`default` keywords must not collide with existing reserved words. Audit needed: scan `keyword_kind` in lexer.hexa.
8. **`@usage(invocation:raw)` raw type unprecedented** — current params are str/int/ident only; `raw` (free-form tokens until end-of-parens) is a new param kind; risk: ambiguity with nested `@<x>` inside usage text. Mitigation: define `raw` as "tokens until top-level `)`", document escape rule.
9. **Migration script verification cost** — re-parsing every .hexa file twice (pre + post) for byte-equivalence on 2250+ files at ~15ms/file → 67s sequential, parallelize via `parallel_for` in stdlib. Risk: flaky if parser non-deterministic on attr ordering.
10. **Phase γ scope creep risk** — silent-error → compile-error (Phase γ) requires effect/Result type tracking which hexa-lang lacks today; defer to dedicated cycle. This spec MUST NOT bind Phase γ.
11. **Parallel BG-α (stdlib/yaml + registry_autodiscover) coordination** — BG-α may modify `stdlib/_registry.hexa` and `stdlib/attrs/_registry.hexa` concurrently. Phase β impl cycle must rebase on BG-α HEAD before AttrSchema registry wiring. Conflict probability: medium (both touch stdlib/_registry.hexa).

## §9 Follow-up cycle

| Cycle | Scope | ETA | Deps |
|---|---|---|---|
| Phase β impl-1 | L1 AttrDecl land + selftest | 4-6d | Phase α YAML registry (BG-α) — green |
| Phase β impl-2 | L2 strict mode + migration script `migrate_add_lax_header` | 2-3d | impl-1 green |
| Phase β impl-3 | L10 attr typing + 6 builtin schemas + `migrate_comment_attrs_to_native` | 5-7d | impl-2 green |
| Phase β audit | full repo migration runs (anima/hive/hexa-lang/airgenome/n6) | 2d | impl-3 green |
| Phase β land | hexa-lang version bump 0.2.0 → 0.3.0 | 1d | audit pass |
| **Total** | | **14-19d** | |

Phase γ (deferred): public-fn @pub required, silent-error → compile-error (requires Result type infra).

## §10 File index (sha-pin)

Spec doc only — no impl files yet. Audited files (read-only references):

| File | LOC | Role |
|---|---|---|
| /Users/<user>/core/hexa-lang/self/parser.hexa | 4404 | parse_stmt @attr while-loop (591-865) |
| /Users/<user>/core/hexa-lang/self/lexer.hexa | 784 | shebang skip (140-145), At-token emit (624) |
| /Users/<user>/core/hexa-lang/self/ast.hexa | 339 | FnDecl.attrs (201) — string serialized |
| /Users/<user>/core/hexa-lang/self/linter.hexa | 524 | (current home of post-parse checks) |
| /Users/<user>/core/hexa-lang/self/attr_cli.hexa | 800 | (existing attr-driven CLI dispatch) |
| /Users/<user>/core/hexa-lang/self/attrs/attrs.json | — | 24-attr catalog (purpose/category/auto_test) |
| /Users/<user>/core/hexa-lang/tool/ai_native_lint.hexa | 687 | text-grep `has_strict_header` (291), `has_lax_header` (310) |
| /Users/<user>/core/hive/tool/attr_usage_lint.hexa | — | comment-attr enforcer for @tool/@sentinel/@usage |

### Spec doc this turn

- `docs/phase_beta_parser_ext_spec.ai.md` — this file (sha sealed by marker)
- `state/markers/phase_beta_parser_ext_spec_landed.marker` — JSON schema-driven landing marker

---

End of Phase β spec.
