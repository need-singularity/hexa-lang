---
schema: hexa-lang/stdlib/yaml/ai-native/1
last_updated: 2026-05-02
module: stdlib/yaml.hexa
depends_on: (none — pure stdlib)
sister_doc: /Users/<user>/core/anima/anima/modules/rng/README.ai.md
marker: state/markers/phase_alpha_yaml_registry_autodiscover_landed.marker
status: landed
phase: alpha (Friction 0)
---

# stdlib/yaml (AI-native)

Minimum-viable YAML parser scoped to `.ai.md` frontmatter blocks (raw 271 convention) and flat `key: value` text. **Not a full YAML implementation** — see Caveats.

## TL;DR

- `yaml_frontmatter_extract(text)` — returns body between leading `---` markers, `""` if absent.
- `yaml_parse_flat(text)` — flat `key -> string` map. Nested children dropped (parent value `""`).
- `yaml_get(m, key, default)` — safe lookup with default fallback.
- `yaml_validate_keys(m, required)` — returns missing keys (empty array means OK).
- `yaml_strip_frontmatter(text)` — body with frontmatter removed.
- All values stay **as strings** — caller casts via `to_int_safe(v)` / `v == "true"` etc.

## API surface

```hexa
use "stdlib/yaml"

pub fn yaml_frontmatter_extract(text: string) -> string
pub fn yaml_strip_frontmatter(text: string)   -> string
pub fn yaml_parse_flat(text: string)          -> map      // key -> string
pub fn yaml_get(m: map, key: string, default_: string) -> string
pub fn yaml_validate_keys(m: map, required: [string])  -> [string]   // missing keys
```

## Invocation matrix

| Goal | Function |
|------|----------|
| Extract `--- ... ---` from `.ai.md` | `yaml_frontmatter_extract(text)` |
| Get body without frontmatter | `yaml_strip_frontmatter(text)` |
| Parse flat key/value text | `yaml_parse_flat(text)` |
| Read one key with default | `yaml_get(m, "schema", "MISSING")` |
| Verify required keys present | `yaml_validate_keys(m, ["schema","last_updated","status"])` |

## Failure modes

| Input | Behavior |
|-------|----------|
| `""` (empty) | extract → `""`, parse → `#{}` |
| Frontmatter without closing `---` | extract → `""` (defensive — treats unterminated as none) |
| Non-frontmatter text | extract → `""`, strip → text unchanged |
| Nested YAML | child lines dropped, parent value `""` (raw#10 limit 1) |
| Comment lines (`# ...`) | dropped |
| List items (`- foo`) | dropped (flat parser only) |
| Inline `# comment` after value | NOT stripped (URLs containing `#` preserved) |
| Unbalanced quotes | preserved verbatim |
| Multi-document `---`/`---`/`---` | only first block treated as frontmatter |
| Anchors / aliases / tags | passed through verbatim |

## Caveats (full list in marker)

1. **Nested YAML rejected.** Indented child lines drop; parent key parses as `""`. Caller must use a richer parser if nested maps are required (none of the current `.ai.md` consumers need nesting beyond `ssot:` block, and that block is human-readable as a string).
2. **Quotes minimal.** Balanced leading+trailing `"` or `'` stripped; no escape handling.
3. **No type coercion.** All values are strings. Caller responsible for `to_int_safe` / bool comparison.
4. **No inline comment strip.** A literal `#` mid-value (e.g. URL fragment) is preserved.
5. **Multi-doc unsupported.** Only the first `--- ... ---` pair treated as frontmatter.
6. **Anchors/tags ignored.** `&anchor`, `*alias`, `!type` survive into the value verbatim.

## Sister consumer

raw 271 README.ai.md frontmatter validation across anima / anima-eeg / airgenome. Today consumers either ignore frontmatter or hand-roll regex; this module centralizes the parser so AI agents can rely on identical semantics for `schema:`, `last_updated:`, `ssot:`, `markers:` keys.

## Selftest

`stdlib/test/test_yaml.hexa` — 27 cases across 9 scenarios:
- empty input (2)
- text without frontmatter (2)
- valid raw-271 frontmatter (5)
- missing required key (2)
- extra keys preserved (3)
- nested YAML rejected (3)
- quoted values (5)
- mixed comments + lists (3)
- default fallback (2)

Byte-identical 2-run: stdout sha256 `f88f7dbcedc073d22310a946c1b8ea550be8ca0d32377ea39e7a0edaa81f9127`.

## File index

| Path | sha256 |
|------|--------|
| `stdlib/yaml.hexa` | `0ea2ac53edcfad26725e7d5ca96c27bca6698c368fc792f900c01eee8e80e99a` |
| `stdlib/test/test_yaml.hexa` | `293a554592b2ba82d0b4833b88b4f23d6503f9ab99b394be1c96e6a80dd64145` |
| `stdlib/yaml.ai.md` | (this doc) |
| `state/markers/phase_alpha_yaml_registry_autodiscover_landed.marker` | (see marker) |
