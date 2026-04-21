# HEXA Reserved Words & Identifier Pitfalls

Survival guide for `gate/*.hexa` authors. Compiled from `self/token.hexa`
(keyword table) + `self/lexer.hexa::is_keyword` + field reports.

SSOT: `self/token.hexa#keyword_from_str`, `self/lexer.hexa#is_keyword`.

## 1. Confirmed reserved tokens

The lexer promotes these bare words to keyword tokens — using any of them as
a `let`/`fn` parameter name is a parse hazard.

### 1a. Hard failures (parser error, compile stops)

```
scope   (bit us in gate/hexa_url.hexa — "unexpected token Scope ('scope')")
```

Most control-flow/type keywords also hard-fail when used as identifiers:
`if else match for while loop break continue type struct enum trait impl dyn
fn return yield async await let mut const static mod use import pub crate
own borrow move drop channel select atomic effect handle resume proof
assert invariant theorem macro derive where comptime try catch throw panic
recover extern defer typeof guard with as in do`.

### 1b. Silent-void hazards (parser emits error, but `hexa run` does NOT fail)

These are the dangerous ones — the statement collapses and the variable
becomes `void` at runtime. No non-zero exit code.

```
spawn    (bit us in gate/prompt_scan.hexa drill auto-dispatch refactor,
          `let spawn = "..."` silently became void; runtime errors later
          appeared as "cannot call method on void")
```

Candidates with the same failure mode (same keyword group — Concurrency/
Memory/Effects/AI verbs parsed as stmt-starters):  `channel`, `select`,
`atomic`, `own`, `borrow`, `move`, `drop`, `effect`, `handle`, `resume`,
`pure`, `intent`, `generate`, `verify`, `optimize`, `derive`, `theorem`,
`invariant`, `proof`.

Rule of thumb: if a word appears in `self/lexer.hexa::is_keyword`, do not
use it as an identifier — even if a minimal snippet seems to work.

### 1c. Boolean / alias tokens

`true`, `false` → `BoolLit`.  `import` → alias of `use`.

## 2. Method names that differ from common stdlib conventions

HEXA method names diverge from Python/JS/Rust in several spots. Gate code
uses the hexa form consistently.

| want                    | hexa form                       | NOT             |
|-------------------------|---------------------------------|-----------------|
| substring `[a, b)`      | `s.substring(a, b)`             | `.substr`, `.slice` (on strings) |
| length                  | `s.len()`  /  `arr.len()`       | `.length`, `.size`, `len(s)` (top-level also works) |
| char at index           | `s.chars()[i]`                  | `s[i]` (string indexing is not direct) |
| find substring          | *(none built-in)* — use `.contains` + hand-roll, or `char_at` loop | `.index_of`, `.find` |
| split                   | `s.split(sep)`                  | `.split()` with no arg |
| trim                    | `s.trim()`                      | `.strip()` |
| starts / ends           | `s.starts_with(p)` / `.ends_with(p)` | `.startsWith` |
| contains                | `s.contains(t)`                 | `.includes`, `in` operator |
| stringify               | `to_string(x)`                  | `x.to_string()` (top-level only) |
| parse int               | `to_int(s)`  (throws — wrap in `try`) | `int(s)`, `s.parse()` |
| array push              | `arr.push(x)`  *(returns new array — REASSIGN: `arr = arr.push(x)` for top-level `let mut` arrays)* | pure mutation |
| array find              | `stdlib/collections.find(arr, f)` — NOT a method | `.find()` as method |

`@pure` / `pure fn` is a language feature (effect track), not an identifier
convention.  Underscore prefixes are used two ways in the stdlib + self/:

- `__c_foo` (double) — FFI extern (see `self/runtime_pure.hexa`).
- `_foo`   (single) — file-private helper (see `gate/hexa_url.hexa::_hex_to_int`).

Neither is enforced by the compiler, but both are load-bearing conventions:
don't shadow `__c_*` names, and prefer `_foo` over `foo_impl` / `foo_helper`.

## 3. Known-safe alternatives (hazard → safe)

| hazard    | safe rename                      |
|-----------|----------------------------------|
| `spawn`   | `spawn_cmd`, `child`, `dispatch` |
| `scope`   | `scope_name`, `area`, `ctx`      |
| `select` / `channel` / `atomic` | `sel` / `chan` / `atom` |
| `handle` / `effect` / `resume`  | `hdl` / `fx` / `cont`   |
| `intent` / `verify` / `optimize` / `generate` | `goal` / `check` / `tune` / `gen` |
| `derive` / `proof` / `theorem` / `invariant`  | `infer` / `pf` / `thm` / `inv` |
| `guard` / `with` / `defer`      | `grd` / `ctx` / `cleanup` |
| `as` / `in` / `do` / `typeof`   | `alias` / `within` / `body` / `ty` |
| `move` / `own` / `drop` / `borrow` | `mv` / `owner` / `dispose` / `view` |

## 4. How to check: is `FOO` reserved?

One-liner — parse a minimal snippet and look for `Parse error`:

```sh
hexa run /dev/stdin <<<'fn main() { let FOO = "ok"; println(to_string(FOO)) }' 2>&1 \
  | grep -E 'Parse error|unexpected token|ok|void'
```

Interpretation:

- `ok` printed, no errors                    → safe identifier.
- `Parse error ... unexpected token Xxx`     → **hard reserved** (§1a).
- No parse error BUT prints `void` (or `(void)`) or crashes on `.method` calls later
                                             → **silent reserved** (§1b) —
  the `let` was eaten as a keyword stmt. Rename it.

Quick authoritative check (bypass runtime):

```sh
grep -E "\"$FOO\"" /Users/ghost/core/hexa-lang/self/token.hexa \
   /Users/ghost/core/hexa-lang/self/lexer.hexa
```

If either file has a match inside `keyword_from_str` / `is_keyword`, the
word is reserved — pick an alternative from §3.
