# raw#14 Magic-Number Parser-Level Detection — Design

**Date:** 2026-04-19
**Scope:** unblock `.roadmap` entry 8 (deferred — "raw#14 magic-number detection, parser-level")
**Depends-on:** hexa AST literal position info (currently missing)
**Today's enforcer:** `tool/no_hardcode_lint.hexa` (grep + `"..."` heuristic, no magic-number rule)

---

## 1. Current AST literal node — as-is

Literals are produced by `self/parser.hexa::parse_primary()` from `self/lexer.hexa` tokens.
The **lexer already carries `line:int` + `col:int`** on every `Token`; the parser **drops it**.

| layer | struct | position info |
|---|---|---|
| `self/lexer.hexa:5-10` | `Token { kind, value, line, col }` | YES |
| `self/parser.hexa:69-118` | `node_int_lit / node_float_lit / node_string_lit / node_char_lit / node_bool_lit` | **NO** |
| `self/ast.hexa:35-56` | `struct Expr { kind, int_val, float_val, str_val, ... }` | **NO** |

Call sites: `parser.hexa:2557-2576` — `parse_primary()` pulls `tok`, calls `node_*_lit(tok.value)`, discards `tok.line` / `tok.col`.

Effect: once the AST is built, no pass can say "literal `42` appeared at `foo.hexa:123:17`". The lint must fall back to source grep — which can't distinguish `let PORT = 8080` (defined SSOT) from `connect(host, 8080)` (magic-site).

## 2. Position-info addition plan

Minimal, additive, zero-break.

**2a. Extend `Expr` (`self/ast.hexa`)**
- add `line: int` + `col: int` after `inclusive: bool`
- update the 6 `expr_*` constructors (`expr_int / expr_float / expr_bool / expr_string / expr_ident / expr_wildcard`) to take `(..., line, col)` and populate — default `0, 0` for synthetic nodes
- downstream consumers read via `e.line` / `e.col`; existing code ignoring the fields compiles unchanged

**2b. Extend parser literal nodes (`self/parser.hexa`)**
- add `line` + `col` keys to the 6 `node_*_lit` + `node_ident` map-node builders (lines 69-129)
- threadthrough: `node_int_lit(tok.value, tok.line, tok.col)` etc. at the 5 `parse_primary()` call sites (`parser.hexa:2559-2575`)

**2c. Also tag binary/unary/call nodes (stretch)**
- `node_binary` / `node_call` etc. already have the operator token in hand — tag once so downstream passes can locate *any* expr, not only leaves. Optional; magic-number rule only needs the leaf.

**2d. Risk / rollback**
- `Expr` struct grows by 16 B (two `int`). Hexa lists are pass-by-value but `Expr` is returned, not aliased — no aliasing hazard (unlike the 2026-04-15 struct-list bug).
- Self-compile re-bootstrap required: parser.hexa + ast.hexa both change → rebuild stage0 artifact via existing `hexa-build` path.

## 3. Magic-number detection logic — plan

New tool: `tool/magic_number_lint.hexa` — sibling of `no_hardcode_lint.hexa`.

**Pipeline (per .hexa file):**
1. `parse(src)` → AST (requires 2a/2b shipped)
2. Walk Stmts: collect **exemption contexts**
   - `let NAME = <IntLit|FloatLit>` at module scope → register value as named-SSOT (no violation at definition site)
   - stmt preceded by `@const` attr → whole stmt exempt
   - inside `const NAME = ...` / `static NAME = ...` → exempt
3. Walk Exprs: every `IntLit` / `FloatLit` **not** in an exempt parent is a candidate
4. Apply **allow-list** (small-int whitelist — empirically tuned):
   - int: `-1, 0, 1, 2` always allowed (loop bounds, array indexing, sign)
   - float: `0.0, 1.0, -1.0` allowed
   - any literal inside a `range` expr (`0..n`) — allowed (iteration, not magic)
   - any literal used as array literal index/size annotation — allowed
5. Remaining hits → report `path:line:col value=<lit>`
6. Baseline: `.magic-baseline` (same shape as `.hardcode-baseline`); `--bake` / `--list` / `--warn` / `--block` flags mirror raw#14 enforcer

**Exit contract:** mirror `no_hardcode_lint.hexa` — `--bake` writes grandfather list, default `--block` exits 1, `--warn` exits 0.

**Correctness proofs available once shipped:**
- true-positive: literal `0.014` appearing in `self/consciousness_bridge.hexa:96` array ← already hand-catalogued at `consciousness_bridge.hexa:109-114`
- true-negative: `let BATCH = 64` defined once, referenced N times → 0 hits

## 4. Delivery order

1. land 2a + 2b (`Expr.line/col` + parser threading) — behind a no-op (nothing reads the fields yet)
2. rebootstrap stage0 (existing P7-6 pipeline)
3. ship `tool/magic_number_lint.hexa` + `.magic-baseline` initial bake
4. promote `.raw` raw#14 scope line "magic numbers" from informational → enforced; flip roadmap entry 8 from `deferred` to `live`
5. graduate allow-list constants (`-1,0,1,2`) into `.own` so reviewers can adjust without code change

## 5. Open questions

- tag ident-use-sites too? useful for "unused named const" rule, out of raw#14 scope — defer
- multi-file SSOT tracking (one `.hexa` defines `let PORT`, another uses literal `8080`) — requires cross-file symbol table; defer to a follow-up after single-file works
