# 2026-04-06: HEXA Language Full Survey

## Stats
- 53,852 LOC | 38 source files
- 53 keywords (σ·τ+sopfr) | 24 operators (J₂) | 8 primitives (σ-τ)
- 150+ builtin functions | 38 VM opcodes | 25 Value variants

## Self-hosting Blockers (5)
1. ~~HashMap~~ → Map literal `{}` parsing issue only
2. **Generics** — parsed but not implemented in interpreter
3. ~~else if~~ → already supported
4. ~~Module system~~ → basic mod/use exists
5. **break/continue** — not in AST

## JIT Coverage: 14 stmt / 12 expr types
## VM Coverage: 8→10 stmt (Const/Static added) / full expr
## Interpreter: full coverage (all 40+ stmt, all 35+ expr)

## Builtin Categories
- I/O: print, println, read_file, write_file, etc.
- Math: abs, sqrt, pow, sigma, phi, tau, gcd, sopfr
- String: split, trim, replace, contains, starts_with, to_upper, etc.
- Collections: keys, values, has_key, Set, BTree, PQ, Deque
- Concurrency: channel, spawn, select, atomic
- Network: http_get, http_post, net_listen, net_connect
- Crypto: sha256, hmac_sha256, xor_cipher
- JSON: json_parse, json_stringify
- File: read_file, write_file, file_exists, delete_file, fs_glob
- Testing: assert_eq, assert_ne, test_run, bench_fn
