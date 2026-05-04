# HEXA-JIT IR (F-JIT)

Tiny SSA-ish IR used by `tool/jit/jit_compile.hexa` to lower `@jit`-tagged
hexa functions to C source, then to a `.dylib` loaded via `@link` FFI.

## Instruction set

| op            | operands              | meaning                                |
|---------------|-----------------------|----------------------------------------|
| `CONST_I`     | dst, imm              | dst = imm (i64)                        |
| `MOV`         | dst, src              | dst = src                              |
| `BIN`         | dst, op, a, b         | dst = a `op` b (`+ - * / % == != < <= > >= && || & | ^`) |
| `NEG`         | dst, src              | dst = -src                             |
| `BR`          | block                 | unconditional branch                   |
| `BRC`         | cond, then, else      | conditional branch                     |
| `LABEL`       | block                 | basic-block label                      |
| `CALL`        | dst, fn_id, args[]    | dst = fn_id(args...) (inline-only fns) |
| `RET`         | src                   | return src                             |

## Types

`i64` only in this revision (Hexa `Int`). Floats and pointer args are
documented as future extensions but not lowered: the JIT bails out and
falls back to the interpreter when it sees them.

## SSA discipline

Each value gets a fresh virtual register `vN`; phi nodes are NOT used —
loop-carried variables are lowered as plain `int64_t` C locals (the C
compiler trivially re-SSAs them at -O2). This keeps the lowering pass
~100 lines instead of ~400 for a real phi/dominance pass.

## Bail-outs (fall through to interpreter)

- Function takes/returns non-`Int` (Float, String, struct, list, map, etc.).
- Body uses unsupported constructs: `for`, `match`, closures, `try`/`throw`,
  string ops, list ops, method calls, `extern fn` calls, attribute decorators
  inside the body, multi-arg helpers other than other `@jit` fns in the
  same file.
- Function calls a function not also tagged `@jit` in the same file.

When a bail-out is detected the JIT prints
`[jit] bail: <fn> <reason>` and the original source is run unchanged.

## Pipeline overview

```
.hexa source
    │  (textual @jit scan + brace match)
    ▼
fn_ast (params, body lines)
    │  (lower_fn → emit_c)
    ▼
C source  (build/jit_<name>.c)
    │  cc -O2 -dynamiclib
    ▼
build/libjit_<file>.dylib
    │  (rewrites @jit fn → @link("jit_<file>") extern fn)
    ▼
hexa run <jit_wrapper.hexa>  ← interpreter calls native via FFI
```
