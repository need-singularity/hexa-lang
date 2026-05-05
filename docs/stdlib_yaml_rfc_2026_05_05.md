# RFC: stdlib YAML contract — close upstream gaps #3 + 8b

- **Date**: 2026-05-05
- **Status**: landed (`self/stdlib/yaml.hexa` + 3 self/test/* + stdlib selftest + fixture round-trip mode)
- **Author scope**: stdlib + tests + docs only (raw 9, raw 159) — does not modify hexa runtime
- **Constraints**: raw 9 hexa-only · raw 91 no_silent_errors · raw 159 hexa-lang upstream-proposal-mandate
- **Closes**: hexa-lang upstream gaps #3 (yaml stdlib) + #8b (line-preserving yaml mutation), per [`docs/hexa_url_complete_support_upstream_spec_2026_05_05.ai.md`](hexa_url_complete_support_upstream_spec_2026_05_05.ai.md) §2.2 / §2.8.

---

## 1. Motivation

### 1.1 Concrete trigger

Every consumer that needed yaml mutation was shelling out to python3 for round-trip-byte-identical writes that preserve comments + ordering + whitespace. From `tool/raw_mk2_loader.hexa`:

> JSONL chosen over YAML because hexa stdlib has json.hexa but no yaml.hexa.

From the cross-sister `hive-resource` package's `tool/resource_op.hexa`:

> YAML mutation strategy: line-level Python helper (no PyYAML round-trip dump) so comments + section ordering + trailing whitespace stay byte-identical.

This pattern leaks resolver banner contamination via python's stderr (gap #4) and grows a python3 surface that raw 9 (hexa-only) and raw 159 (upstream-proposal-mandate) require us to shrink.

### 1.2 Affected files

The following currently shell out to python3 for yaml ops in the canonical tree (sample, not exhaustive):

- `hive/tool/resource_op.hexa` — the line-level Python helper for managed mutations.
- `hive-resource list/add/remove` writers — every `.spec.yaml` writer in `hive/spec/` is touched via the same helper.
- Any future `.spec.yaml` mutation — without a hexa-native primitive each new mutation site re-derives the same python helper.

`hive/.raw.mk2` is JSONL-one-rule-per-line specifically because of the missing yaml stdlib. The schema doc (`hive/.raw.mk2.schema.yaml`) is YAML-as-prose for humans; runtime validation is JSON-shape only.

---

## 2. API surface

### 2.1 Public functions (`self/stdlib/yaml.hexa`)

```hexa
fn yaml_parse_safe(text: string) -> [bool, value]
    // [true, parsed] on success, [false, void] on parse error.
    // Mirrors json_parse_safe contract shape exactly.

fn yaml_parse_or_throw(text: string) -> value
    // Parses or eprintln + exit 1 on error. Use when malformed YAML
    // is a programmer error (config files, spec files).

fn yaml_emit(value, opts: map) -> string
    // Block-style yaml 1.2 emitter. opts (all optional):
    //   indent: int = 2 — spaces per nesting level.

fn yaml_mutate(text, path_expr, new_value) -> [bool, new_text]
    // jq-style path mutation. path_expr supports:
    //   key                       — top-level key
    //   key.sub.path              — dotted descent
    //   key[name=value]           — filter list of mappings on a leaf
    //   key[N]                    — list index
    //   key[name=value].sub       — combined
    // Preserves comments, blank lines, ordering, trailing whitespace.
    // No-op mutations (new_value == current value at path) yield
    // byte-identical output.
```

### 2.2 Selftest entry points

```sh
# Correctness selftest (10 cases, sentinel: __YAML_SELFTEST__ PASS n=10 fail=0):
hexa run self/stdlib/yaml.hexa --selftest

# Real-world fixture round-trip byte-eq:
hexa run self/stdlib/yaml.hexa --selftest-fixture <path>
# Sentinel: __YAML_BYTE_EQ__ PASS bytes=N
# Plus the literal token: BYTE_EQ_PASS
```

### 2.3 Sister tests under `self/test/`

- `self/test/yaml_parse.hexa` — parse contract structural test.
- `self/test/yaml_emit.hexa` — emit contract structural test.
- `self/test/yaml_mutate_byte_eq.hexa` — mutate contract structural test + fixture byte-eq sentinel.

These follow the same convention as `self/test/file_atomic_replace.hexa`: structural verification of the stdlib body (`use "self/stdlib/..."` is silenced inside the sandbox where these run; the canonical runtime test is the stdlib's own `--selftest`).

---

## 3. Honest C3 disclosures (raw 91 no_silent_errors)

Same disclosure shape as the json RFC.

### 3.1 Parser scope

- Block-style mappings + lists, plain scalars, single- and double-quoted strings, line comments, block scalars (`|` + `>` with chomp indicator), null/bool/int/float/string per yaml 1.2 core schema.
- yaml 1.1 alias quirks (`yes/no/on/off`) are deliberately treated as **strings** (BaseLoader-equivalent behaviour), per the task spec.

### 3.2 Sub-gaps surfaced

- **SG-1 (regex stdlib)**: yaml_mutate's path expression parser is hand-rolled because hexa-lang has no `self/stdlib/regex.hexa`. Path grammar therefore accepts `seg(.seg)*` with each `seg` either `name`, `name[N]`, or `name[k=v]`. Richer jq syntax (slices, recursive descent, etc.) is out of scope until a regex stdlib lands.
- **SG-2 (multi-document yaml)**: `---`-separated streams collapse to first-doc only. Hive spec yaml writers do not currently rely on multi-doc; a future `yaml_parse_all_safe` is the clean follow-up.
- **SG-3 (anchors / aliases / custom tags)**: preserved verbatim during mutate (they live on lines outside the mutation site, so byte-eq holds), but parse drops alias resolution and tag-typing.
- **SG-4 (strict yaml 1.2 scalar grammar)**: covers null/bool/int/float/string but does NOT cover every yaml 1.2 oddity (`0o17`, `0x1F`, `.inf`, `.nan`). Those coerce to strings.
- **SG-5 (flow-style multi-line)**: single-line flow `[1, 2, 3]` works; multi-line flow with embedded newlines is out of scope.
- **SG-6 (round-trip emitter)**: yaml_emit→yaml_parse_safe→yaml_emit is byte-equal only when input had no comments and was already in our canonical block style. The byte-eq contract is on `yaml_mutate` (which line-preserves), not on the emitter alone.

### 3.3 Limitations the parser deliberately accepts

- Empty input → `[true, void]` (treated as a valid empty document).
- Comment-only input → `[true, void]`.
- Trailing whitespace after scalars is tolerated.
- The runtime's `to_int` hard-errors on non-numeric input, so the parser routes typed scalars through `json_parse` (which is total over its accepted shape) to avoid uncatchable runtime errors on borderline tokens.

---

## 4. Implementation strategy

### 4.1 Parse (block style, line-cursor recursive descent)

Single global line cursor `_yml_cur` over a `_yml_lines` array. Recursive descent on indentation. Each "logical line" is content-stripped of its trailing comment before scalar typing; the mutator does NOT go through this path — it operates directly on raw lines so comments survive.

### 4.2 Emit (block style, type-dispatched)

`_yml_emit_value(v, indent_step, level)` dispatches on `type_of(v)` to map / list / scalar emitters. Scalar emitter consults `_yml_scalar_needs_quotes` to force double-quoting on yaml 1.2 reserved words (null/true/false/empty, leading-indicator chars, embedded `:`, `#`, etc.) — this is what makes the emitter's output round-trippable.

### 4.3 Mutate (line-anchored, no full re-emit)

```
parse path expression → segment list
walk lines, tracking path stack via indentation
when stack matches:
  determine value extent [start, end)
  build replacement lines from new_value
  if replacement byte-equal to original lines[start:end]:
    return [true, original_text]    ← byte-eq guarantee
  else:
    splice replacement into lines, return joined
```

The byte-eq short-circuit is the contract that justifies this primitive's existence over `parse → mutate → emit` (which loses all comments).

### 4.4 Whitespace preservation rules

For inline-scalar mutation, the rebuild path preserves:

- Indent before the key.
- The key + `:` verbatim.
- Leading whitespace between `:` and the value (typically one space).
- Trailing whitespace between the value and any inline comment (so comments don't shift columns).
- The comment verbatim (including its `#`).

The no-op path bypasses all of that and returns the input as-is, which is the strictest byte-eq guarantee.

---

## 5. Verification

### 5.1 Stdlib selftest (canonical)

```
$ hexa run self/stdlib/yaml.hexa --selftest
__YAML_SELFTEST__ PASS n=10 fail=0
```

Covers: simple/nested parse, list parse, comment handling, yaml 1.1 alias quirks → strings, emit round-trip, scalar mutate with comment preservation, no-op byte-eq, filter path mutate (`repos[name=hive].mirrors`), block scalar literal `|`.

### 5.2 Real-world fixture byte-eq

```
$ hexa run self/stdlib/yaml.hexa --selftest-fixture /Users/ghost/core/hive/spec/sync_registry.spec.yaml
__YAML_BYTE_EQ__ PASS bytes=11346
BYTE_EQ_PASS
```

Reads the real `hive/spec/sync_registry.spec.yaml` (11346 bytes), parses it, no-op mutates the `schema:` leaf back to its current value, and verifies the output is byte-for-byte identical to the input.

### 5.3 self/test sentinels

```
$ hexa run self/test/yaml_parse.hexa
__YAML_PARSE_TEST__ PASS reason=...

$ hexa run self/test/yaml_emit.hexa
__YAML_EMIT_TEST__ PASS reason=...

$ hexa run self/test/yaml_mutate_byte_eq.hexa
__YAML_MUTATE_TEST__ PASS reason=...
BYTE_EQ_PASS
```

---

## 6. Migration path for sister repos

### 6.1 hive — `tool/raw_mk2_loader.hexa`

The L1 disclosure can shrink: JSONL is chosen for line-mutation simplicity, not yaml-stdlib absence. Future versions can read `.raw.mk2.schema.yaml` natively via `use "self/stdlib/yaml"`.

### 6.2 hive — `tool/resource_op.hexa`

The python line-helper retires. `yaml_mutate(read_file(spec), path_expr, new_value)` replaces the helper end-to-end. Resolver banner contamination (gap #4) becomes irrelevant for this consumer.

### 6.3 hive — `hive resource add|remove`

The CLI's writer path replaces its python invocation with `yaml_mutate`. Read path already reads via spec-loader; once write path migrates, python3 dependency drops in this section.

---

## 7. Cross-link

- Upstream-spec gap reference: [`docs/hexa_url_complete_support_upstream_spec_2026_05_05.ai.md`](hexa_url_complete_support_upstream_spec_2026_05_05.ai.md) §2.2 (yaml stdlib) + §2.8 (line-preserving mutation).
- Companion json RFC: [`stdlib_json_rfc_2026_04_28.md`](stdlib_json_rfc_2026_04_28.md).
- Test pattern: `self/test/file_atomic_replace.hexa` (gap #9 closure — same sandbox-disparity convention).
