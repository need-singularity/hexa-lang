# `yaml`

_yaml_

**Source:** [`stdlib/yaml.hexa`](../../stdlib/yaml.hexa)  

## Overview

Phase α (Friction 0) — AI-native module landed 2026-05-02.

Purpose: parse YAML frontmatter blocks at the head of `.ai.md` documents
(raw 271 README.ai.md convention) and any flat YAML key:value text. This
is *not* a full YAML implementation — see Caveats. It is the minimum
surface needed to:
  (a) extract a `--- ... ---` frontmatter block from a document body,
  (b) parse it into a flat `key -> str` map,
  (c) validate the presence of a required-key set,
  (d) read individual values with default fallback.

Sister-repo unblock: anima/anima-eeg/airgenome/etc. each ship multiple
`.ai.md` docs with frontmatter; today every consumer either ignores it
or hand-rolls a regex/line-scan helper. This stdlib centralizes the
parser so AI agents (and lint tools) can rely on identical semantics.

raw 9 hexa-only — no python/awk delegation. Pure stdlib.

API:
  yaml_frontmatter_extract(text)       -> str   // body between --- markers, "" if absent
  yaml_parse_flat(text)                -> map   // key -> str (numeric/bool stay as strings)
  yaml_get(m, key, default_)           -> str   // safe lookup with default
  yaml_validate_keys(m, required)      -> [str] // missing keys ([] = OK)
  yaml_strip_frontmatter(text)         -> str   // body with frontmatter removed (post-parse use)

Limits (raw#10):
  1. Nested YAML rejected. Lines indented past `key:` collapse to the
     raw remainder string of the parent key — value is `""` for nested
     maps. Caller must treat such keys as opaque blobs.
  2. Quoted strings: leading+trailing `"` and `'` stripped if balanced;
     no escape handling (\n, \t literal kept).
  3. Comments: line starting with `#` ignored. Inline ` # comment` NOT
     stripped (would break URLs containing `#`).
  4. Multi-document YAML (`---` between docs) NOT supported — first
     `---...---` block treated as frontmatter, remainder as body.
  5. Anchors / aliases / tags (`&a`, `*a`, `!type`) ignored verbatim.
  6. Empty key (line is `: value`) silently dropped.

── frontmatter extract ─────────────────────────────────────────────────────

Extract the YAML frontmatter block from text. Returns the content between
the leading `---` and the matching closing `---` (exclusive of markers).
Returns "" if no frontmatter exists. Frontmatter must start at offset 0
(modulo BOM / leading blank lines), per raw 271 convention.

## Functions

| Function | Returns |
|---|---|
| [`yaml_frontmatter_extract`](#fn-yaml-frontmatter-extract) | `string` |
| [`yaml_strip_frontmatter`](#fn-yaml-strip-frontmatter) | `string` |
| [`yaml_parse_flat`](#fn-yaml-parse-flat) | `map` |
| [`yaml_get`](#fn-yaml-get) | `string` |

### <a id="fn-yaml-frontmatter-extract"></a>`fn yaml_frontmatter_extract`

```hexa
pub fn yaml_frontmatter_extract(text: string) -> string
```

**Parameters:** `text: string`  
**Returns:** `string`  

_No docstring._

### <a id="fn-yaml-strip-frontmatter"></a>`fn yaml_strip_frontmatter`

```hexa
pub fn yaml_strip_frontmatter(text: string) -> string
```

**Parameters:** `text: string`  
**Returns:** `string`  

Strip the frontmatter block from text, returning the body. If no
frontmatter is present, returns text unchanged.

### <a id="fn-yaml-parse-flat"></a>`fn yaml_parse_flat`

```hexa
pub fn yaml_parse_flat(text: string) -> map
```

**Parameters:** `text: string`  
**Returns:** `map`  

Parse a flat YAML text (no nesting) into a `key -> string` map.
Lines that begin with whitespace + `-` (list items) are dropped; nested
children of a parent key are dropped (parent value becomes "" by the
time we reach the next top-level key). See raw#10 limit (1).

### <a id="fn-yaml-get"></a>`fn yaml_get`

```hexa
pub fn yaml_get(m: map, key: string, default_: string) -> string
```

**Parameters:** `m: map, key: string, default_: string`  
**Returns:** `string`  

── lookup helpers ──────────────────────────────────────────────────────────

Safe key lookup. If missing or non-string, returns default_.

---

← [Back to stdlib index](README.md)
