# Interp path pre-existing regression — `pad_start` / `u_floor`

Date: 2026-04-24
Scope: read-only investigation. No code fix in this session.
Constraint: 1h deadline, worktree-isolated, docs commit only.

## 1. Summary

Two bench snapshots fail when executed through the tree-walking interpreter
path (`HEXA_CACHE=0 ./hexa ...`) while the AOT path (`./hexa <file>` with
cache, or `./hexa <file> --selftest`) passes:

| snapshot                                     | interp symptom                                           | AOT path |
| -------------------------------------------- | --------------------------------------------------------- | -------- |
| `bench/snapshots/string_method_coverage.hexa` | `Runtime error: unknown method .pad_start() on string`   | PASS     |
| `bench/snapshots/builtin_mangling.hexa`       | `Runtime error: undefined function: u_floor` (+ append, byte_len silent-0) | PASS     |

Step 5 note ("Interp path shows pre-existing failures … not a regression")
is correct: these are not caused by M1-lite work. They are gaps in the
interpreter's method/builtin dispatch tables that never landed alongside
the corresponding AOT codegen additions.

## 2. Reproduction

Commands run from worktree root (`/Users/ghost/core/hexa-lang`):

```
HEXA_CACHE=0 ./hexa bench/snapshots/string_method_coverage.hexa 2>&1 | head
# → Runtime error: unknown method .pad_start() on string
#   FAIL pad_start 5,'0': got=[void] want=[00042]

HEXA_CACHE=0 ./hexa bench/snapshots/builtin_mangling.hexa 2>&1 | head
# → Runtime error: undefined function: append
#   FAIL append_free_fn
#   FAIL byte_len
#   Runtime error: undefined function: u_floor
#   FAIL u_floor
#   FAIL builtin_mangling: 3/12 failed
```

Additional probe confirming `byte_len` silent-0 divergence:

```
cat > /tmp/probe_byte_len.hexa <<'EOF'
fn main() {
    println("byte_len(abc) = " + to_string(byte_len("abc")))
}
EOF
HEXA_CACHE=0 ./hexa /tmp/probe_byte_len.hexa   # → byte_len(abc) = 0   (interp)
./hexa /tmp/probe_byte_len.hexa                # → byte_len(abc) = 3   (AOT)
```

## 3. Observed symptoms (classification)

### 3.1 `pad_start` / `pad_end` — unknown method

- Error path: `Runtime error: unknown method .pad_start() on string`
  (`self/hexa_full.hexa:16964`).
- Method dispatch at `self/hexa_full.hexa:15917` registers only `pad_left`
  / `pad_right` (the legacy names). `pad_start` / `pad_end` aliases are not
  registered.
- AOT codegen at `self/codegen_c2.hexa:3625` rewrites the JS/Python alias
  to `cg_string_sym("str_pad_left")` / `str_pad_right` — runtime C impl is
  shared (`self/runtime.c:3876` `hexa_pad_left`, etc.). The comment there
  even acknowledges: "JS/Python naming aliases — runtime impl is shared
  with the older pad_left/pad_right entrypoints … pad_left/pad_right stay
  registered for backward compat" — but the corresponding interp dispatch
  arms were never added.

### 3.2 `u_floor` — undefined free function

- Error path: `Runtime error: undefined function: u_floor`
  (`self/hexa_full.hexa:9269`).
- `call_builtin` (`self/hexa_full.hexa:9897`) has ~400 `if name == "…"`
  arms. `grep 'name == "u_floor"'` → 0 hits in hexa_full.hexa. Not
  registered.
- AOT codegen at `self/codegen_c2.hexa:2901` routes the name to the host C
  symbol `hexa_u_floor` (runtime impl `self/runtime.c:3630`). Labeled
  "FIX-A (Anima serving unblock, 2026-04-19): append(arr, item) + u_floor(a, b)"
  — that FIX-A only patched the AOT codegen arm, not the interp builtin
  table. `codegen_c2.hexa:5121` also adds `u_floor` to the "is_builtin"
  whitelist for shape-agnostic codegen.

### 3.3 `append` — undefined free function (companion)

- Same class as `u_floor`. `self/codegen_c2.hexa:2891` maps `append(arr,
  item)` to `hexa_array_push(...)`. Interp never registered a free-fn
  `append` builtin (the method form `.push()` works at
  `self/hexa_full.hexa:9924`).
- Not in the task title but surfaces alongside in the same snapshot.

### 3.4 `byte_len` — silent wrong result (not "unknown fn")

- `call_builtin` at `self/hexa_full.hexa:11534` DOES handle `"byte_len"`
  but implements it by calling the bare name `byte_len(args[0])` — that
  routes to the host C runtime `hexa_byte_len` (`self/runtime.c:6063`).
- Host receives the interp's `Val` struct (12 fields) — none of
  `HX_IS_STR` / `HX_IS_ARRAY` / `HX_IS_MAP` match on a raw Val record
  (actually it may match as TAG_VOID/TAG_NOT_BUILTIN when the HexaVal
  representation diverges), so `hexa_byte_len` falls through to
  `hexa_int(0)`. Result: interp → 0, AOT → 3.
- Classification: semantics divergence inside an already-dispatched builtin
  (the interp branch forwards `args[0]` un-unwrapped instead of extracting
  `args[0].str_val` / `get_array(args[0])` per tag like the other
  container builtins do at lines 9906-9914).

## 4. Hypotheses (ranked)

### H1 (most likely). Interp dispatch tables were not updated when the AOT codegen added new names.

Evidence:
- `pad_start` / `pad_end` / `append` / `u_floor` all share the same
  pattern: present in `codegen_c2.hexa` (often under `// FIX-A` or
  "roadmap M4" / "anima roadmap" comments), absent in
  `hexa_full.hexa`'s `call_builtin` or string-method block.
- Specifically: the AOT comment at codegen_c2.hexa:3622 says
  "pad_left/pad_right stay registered for backward compat" — the author
  kept legacy names on the AOT side but only added the new aliases there,
  not on the interp side.
- `u_floor` / `append` were introduced in a single FIX-A batch
  (2026-04-19) that edited only codegen_c2.hexa.
- Mechanically: each missing name needs one dispatch arm, paralleling an
  existing arm (`pad_start` ↔ `pad_left`, `pad_end` ↔ `pad_right`,
  `append` ↔ the method form of `push`, `u_floor` ↔ integer divide).

### H2 (secondary, applies to byte_len). AOT vs. interp value-representation divergence inside forwarded builtins.

Evidence:
- `call_builtin` for `byte_len` calls the bare host symbol and expects
  transparent pass-through, but the interp wraps everything in a `Val`
  struct (`self/hexa_full.hexa:5045`) with 12 fields. The host cannot
  tell what it is getting.
- Same pattern could silently bite other host-forwarded builtins that
  don't unwrap by tag; byte_len is the one that surfaced because the
  snapshot has an `==` check rather than an "unknown fn" error.

### H3 (ruled out). Stdlib not loaded in interp.

Evidence against:
- `byte_len` is a runtime/host builtin, not stdlib.
- `pad_start` would produce "unknown identifier", not "unknown method on
  string".
- `u_floor` is declared as a free-function builtin in codegen_c2.hexa,
  not imported via stdlib.
- `HEXA_CACHE=0` flips AOT cache, not stdlib loader.

## 5. Paths explored

- Dispatch entry point: `self/hexa_full.hexa:9200` (`call_builtin` site),
  `:9269` (undefined-fn error), `:9897` (call_builtin def).
- String method dispatch: `self/hexa_full.hexa:15786-16029`
  (method-by-method arms). `pad_left`/`pad_right` at `:15917` and
  `:15924`. No `pad_start` / `pad_end`.
- Unknown-method error: `self/hexa_full.hexa:16964`.
- AOT rewriter: `self/codegen_c2.hexa:2881-2903` (`byte_len`, `append`,
  `u_floor`), `:3625-3642` (`pad_start`, `pad_end`, `center`, `char_at`,
  `char_code_at`).
- AOT shape-agnostic whitelist: `self/codegen_c2.hexa:5121-5122`
  (`append`, `u_floor`).
- Runtime C impls: `self/runtime.c:3630` (`hexa_u_floor`), `:3876`
  (`hexa_pad_left`), `:6063` (`hexa_byte_len`).
- Snapshots: `bench/snapshots/string_method_coverage.hexa:33-36` for
  pad_start/pad_end; `bench/snapshots/builtin_mangling.hexa:52,74,82` for
  append/byte_len/u_floor.

No hits for `pad_start` / `u_floor` / `append` in `self/interpreter.hexa`
(a separate, smaller interp variant) — the active interp is
`self/hexa_full.hexa`.

## 6. Suggested fix path (next session — NOT applied here)

### F1. Add `pad_start` / `pad_end` arms in the interp string-method block.

Location: `self/hexa_full.hexa` near line 15917-15930. Two blocks,
copy-paste of the existing `pad_left` / `pad_right` arms, renamed. The
logic is already correct (the AOT side reuses `str_pad_left` /
`str_pad_right` runtime impl).

```
if method == "pad_start" {   // same body as pad_left
    let width = args[0].int_val
    let ch = args[1].str_val
    let mut out = s
    while len(out) < width { out = ch + out }
    return val_str(out)
}
if method == "pad_end"   {   // same body as pad_right ... }
```

### F2. Add `u_floor` / `append` arms in `call_builtin`.

Location: `self/hexa_full.hexa` in the `call_builtin` body. Suggested
placement near the `push` / `to_int` fast-path cluster around line
9924-9930, or inside the main dispatch chain near the `byte_len` arm at
:11534.

```
if name == "u_floor" {
    if len(args) < 2 { return val_int(0) }
    let a = args[0].int_val
    let b = args[1].int_val
    if b == 0 { return val_int(0) }
    return val_int(a / b)     // truncation toward zero matches hexa_u_floor
}
if name == "append" {
    if len(args) >= 2 && args[0].tag == TAG_ARRAY {
        return array_push_inplace(args[0], args[1])
    }
    return args[0]
}
```

### F3. Fix `byte_len` interp path to unwrap `args[0]` by tag (rather than forwarding the Val struct to the host).

Location: `self/hexa_full.hexa:11534-11537`. Mirror the pattern used by
`len` at :9906-9914:

```
if name == "byte_len" {
    if len(args) == 0 { return val_int(0) }
    let a = args[0]
    if a.tag == TAG_STR   { return val_int(len(a.str_val)) }
    if a.tag == TAG_ARRAY { return val_int(len(get_array(a))) }
    if a.tag == TAG_MAP   { return val_int(len(get_map_keys(a))) }
    return val_int(0)
}
```

### Validation after fix (next session)

```
HEXA_CACHE=0 ./hexa bench/snapshots/string_method_coverage.hexa    # → PASS
HEXA_CACHE=0 ./hexa bench/snapshots/builtin_mangling.hexa          # → PASS
./hexa bench/snapshots/string_method_coverage.hexa --selftest      # AOT regression guard
./hexa bench/snapshots/builtin_mangling.hexa --selftest             # AOT regression guard
```

All three changes are additive arms in existing dispatch tables — no
change to Val representation, no change to AOT codegen, no change to the
host C runtime. Risk is minimal.

## 7. Out of scope for this session

- Actual code fix (F1/F2/F3). Deferred to next session per task spec.
- AOT regression testing / full test suite run. Read-only per spec.
- Auditing the rest of the 400+ builtin dispatch arms for similar
  divergences — recommended as a follow-up sweep, potentially automated
  by cross-diffing `grep 'name == "' codegen_c2.hexa` vs. `hexa_full.hexa`.
