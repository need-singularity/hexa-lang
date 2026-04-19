# HEXA-LANG Update History

Chronological record of significant changes, breakthroughs, and optimizations.
See also: [Roadmap](../plans/roadmap.md) | [Breakthroughs](../breakthroughs/)

## 2026-04-06
- [VOID extern FFI Phase 1](2026-04-06-void-extern-ffi.md) — extern fn + dlopen/dlsym FFI 완성, PTY 테스트 통과, env.set→define 버그 수정
- [VM Performance 333x](2026-04-06-vm-perf-333x.md) — 16-round optimization, Tiered JIT→VM→Interp, Value 72→32B, 1791 tests, -e flag, dream stack safety, PGO verified
- [Language Full Survey](2026-04-06-language-survey.md) — 53K LOC, 53 keywords, 150+ builtins, self-hosting blockers identified
- break/continue added, VS Code LSP client, self-host lexer example, stdlib reference, playground deploy guide

## 2026-04-07
- [@optimize 행렬 거듭제곱](2026-04-07-optimize-matrix-exp.md) — 선형 점화식 감지 → O(log n) 네이티브 JIT, fib(90) 7연산, 1877 tests
