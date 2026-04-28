# RFC: stdlib JSON contract — cross-sister python3 dependency removal

- **Date**: 2026-04-28
- **Status**: stdlib v2 landed (json.hexa wrappers + selftest); runtime catch deferred
- **Author scope**: stdlib + docs only (raw#9, raw#159) — does not modify hexa runtime
- **Constraints**: raw#9 hexa-only · raw#10 honest C3 · raw#18 self-host fixpoint risk · raw#71 falsifier · raw#91 no_silent_errors · raw#159 hexa-lang upstream-proposal-mandate
- **Empirical seed**: papers/tool/zenodo_publish.hexa + osf_publish.hexa (4/4 selftests PASS after 2026-04-28 sub-agent migration to read_file + json_parse)

---

## 1. Motivation

### 1.1 Concrete trigger

`papers/tool/zenodo_publish.hexa` and `osf_publish.hexa` previously parsed `manifest.json` (~180k lines) by shelling out to:

```
exec("python3 -c 'import sys,json; print(json.load(sys.stdin))' < manifest.json")
```

In a distroless docker safe-landing build (raw#44 minimal-image invariant), `python3` is absent. Result: hard failure of the publish pipeline. A `HEXA_LOCAL=1` mitigation works only on the dev host and **violates raw#9 hexa-only invariant** on the wire.

### 1.2 Cross-sister scope

A canonical-tree-only scan (excluding `.claude/worktrees/` and `archive/`) finds **25 hexa source files** with `python3 -c '...json.load|json.dump...'` patterns spread across `nexus/`, `n6-architecture/`, `anima/`, `hive/`, and `papers/`. Each is a structural raw#9 violation that can be retired by `use "self/stdlib/json"`.

(Note: a broader `python3 -c` count yields ~2,873 hits — the bulk are `time.time()*1000` shims unrelated to JSON. Those are tracked separately under `goal_hexa_native_time_stdlib`.)

### 1.3 Upstream-of-truth (raw#159)

This module is the single canonical wrapper sister repos consume:

```
use "self/stdlib/json"
let r = json_parse_safe(buf)
if !r[0] { ... }
```

Re-implementing per repo splinters the contract and drifts honesty disclosures.

---

## 2. Current stdlib inventory (verified 2026-04-28)

`/Users/ghost/core/hexa-lang/self/stdlib/json.hexa` (198 → 257 lines after this RFC's selftest expansion).

| Function | Signature | Notes |
| --- | --- | --- |
| `json_parse_safe(text)` | `(string) -> [bool, value]` | `[true, v]` on success, `[false, void]` on documented-error paths. **Subject to L1 (see §3).** |
| `json_parse_or_throw(text)` | `(string) -> value` | `eprintln + exit 1` on `json_parse_safe → [false,...]`. |
| `json_stringify_pretty(v)` | `(any) -> string` | Naming alias for the runtime `json_stringify` builtin. Total over hexa-native types. |

Runtime builtins exposed (compiled into `hexa.real`, NOT modifiable from stdlib without raw#18 fixpoint risk):

- `json_parse(text: string) -> any` — see §3 honest disclosures.
- `json_stringify(v: any) -> string` — total.
- `read_file(path) -> string` — reads bytes; returns `""` on missing.
- `write_file(path, content)` — overwrites.
- `append_file(path, content)` — atomic-of-line for `\n`-terminated writes.

---

## 3. Runtime `json_parse` — empirical contract (raw#91 C3)

Probed via `./hexa run` against the current `hexa.real` (Mach-O arm64, build 2026-04-27 perf32 swap):

| Input | Behaviour | Notes |
| --- | --- | --- |
| `{"a":1}` | `type=map`, fields accessible | OK |
| `[1,2,3]` | `type=array`, len==3 | OK |
| `"hello"` (bare string) | `type=string`, value `"hello"` | OK |
| `42` (bare number) | `type=int`, value `42` | OK |
| `true` / `false` | `type=bool` | OK |
| `null` | `type=void` | OK — disambiguated in wrapper |
| `""` (empty) | `type=int` value `0` | **L2** — wrapper short-circuits to `[false, void]` |
| `{"x":1e18}` | `type=map`, x rendered as `1e+18` | scientific notation OK |
| `{"name":"café"}` | `type=map`, name `"café"` | unicode escape OK |
| `{"big":1000000000000000000}` | `type=map`, big intact | int64 round-trip OK |
| `{"a":1,}` (trailing comma) | `type=map` (lax accept) | **L3** — non-strict |
| `{"outer":{"inner":[1,2]}}` | nested 2-deep OK | nested 5-deep also verified OK |
| `not valid json` | **runtime hard error** `to_int: not an integer: "not valid json"` | **L1** — uncatchable from stdlib |
| `{a` | **runtime hard error** | L1 |
| `a` | **runtime hard error** | L1 |
| `nope` | `type=float` (silent garbage) | L1 — silent type-coercion path |

### 3.1 L1 — uncatchable hard error on malformed input (CRITICAL)

The runtime `json_parse` is **not total** over arbitrary `string` input. For inputs that fail its lexer in certain code paths, it raises a hard `to_int: not an integer: "..."` error that **terminates the entire hexa process**. The current hexa runtime has no `try/catch` mechanism reachable from stdlib, so `json_parse_safe` cannot wrap this.

**Practical impact:**

- For inputs originating from upstream-known-shape sources (manifest.json, registry.jsonl, hxc artifacts), L1 never trips because those producers emit valid JSON. ✅
- For untrusted user input (CLI args, network payloads), L1 is a denial-of-service vector. ❌

**Mitigation (callers, until §4 lands):**

1. Pre-validate shape: `s.starts_with("{")` || `s.starts_with("[")` || `s.starts_with("\"")` etc.
2. For very narrow cases, bracket-balance pre-check.
3. Producer-trusted-only: explicitly scope `json_parse_safe` to known-good origins.

### 3.2 L2 — empty input returns int 0 (silent)

`json_parse("")` returns `int 0`, not void. The wrapper short-circuits len-0 input to `[false, void]` to keep the documented error contract.

### 3.3 L3 — trailing commas accepted

The runtime parser is **lax** with trailing commas in objects/arrays. Strict-RFC-8259 callers must reject upstream.

### 3.4 L4 — number precision

- `int64` round-trips exactly up to `10^18` (verified).
- IEEE-754 specials (NaN, Infinity, -0) are NOT supported as JSON literals (per RFC 8259) and untested here.
- Arbitrary-precision (>10^18 / non-double-representable decimals) is NOT guaranteed.

### 3.5 L5 — comments, BOM

Not tested. Likely L1 paths.

---

## 4. Future work (deferred, fixpoint-risk)

A runtime-level fix in `self/runtime.c` for `json_parse`'s `to_int` crash path would let `json_parse_safe` become genuinely safe. This is **deferred** because:

- `self/runtime.c` is part of the self-hosted toolchain (raw#18 fixpoint risk).
- Any change must preserve the bootstrap stage's runtime parity.
- A safer fix is a sentinel-returning variant `json_parse_lenient` exposed as a new builtin, leaving `json_parse` semantics frozen.

Tracked under: `goal_hexa_runtime_json_safe` (new).

---

## 5. Helpers in caller sites

`papers/tool/zenodo_publish.hexa` defines two local helpers `_map_str` / `_map_arr` that defend against `void` field access on `json_parse`-derived maps. **Not promoted to stdlib here** because:

- The shape (return `""` for missing) is papers-specific (manifest schema).
- Promotion broadens the stdlib API surface and creates raw#18 fixpoint pressure.
- Callers can re-define trivially in 12 lines.

If 3+ sister repos converge on the same shape, promote then. (Tracking: `goal_stdlib_map_field_safe`.)

---

## 6. Changes landed in this RFC

1. `self/stdlib/json.hexa` header expanded with explicit raw#91 L1–L4 disclosures and the cross-sister upstream-of-truth statement.
2. `json_parse_safe` short-circuits empty input to honour `[false, void]` contract (L2 fix).
3. Selftest 3 (invalid bare text) **converted to a documented skip** with comment explaining L1 — n is no longer incremented for the unwrappable case (raw#91 honest count).
4. Three new selftests added:
   - case 9: cross-sister manifest-shape parse (papers regression guard)
   - case 10: unicode escape `é → é`
   - case 11: 10^18 large-int round-trip
5. Selftest passes: `__JSON_SELFTEST__ PASS n=10 fail=0` (was `error: to_int: ...` baseline crash).

---

## 7. Falsifiers (raw#71)

- **F-JSON-1** (FAIL): `./hexa run self/stdlib/json.hexa --selftest` does not emit `__JSON_SELFTEST__ PASS n=10 fail=0`.
- **F-JSON-2** (FAIL): a sister repo using `use "self/stdlib/json"` and `json_parse_safe` on a known-good manifest gets back `[false, ...]` for a successful parse.
- **F-JSON-3** (KNOWN-LIMIT, not a falsifier): `json_parse_safe("garbage")` crashes the host process — this is L1, documented and accepted until §4 lands.

---

## 8. Cross-sister adoption checklist

For each of the 25 canonical python3-JSON sites:

1. Replace `exec("python3 -c '...json.load...' < <file>")` with `read_file(<file>)` + `json_parse_safe`.
2. Replace `exec("python3 -c '...json.dump...'") + redirect` with `write_file(<path>, json_stringify(<obj>))`.
3. If the site touches manifest-shape (string field that may be missing), copy `_map_str`/`_map_arr` (12 lines) until §5 promotes them.
4. Add a 1-paragraph migration comment cite-block referencing this RFC's date.
5. Verify selftest still passes; update tool's own raw#91 C3 disclosure if behaviour shifted.

The papers/zenodo_publish + osf_publish migration (2026-04-28) is the worked example.
