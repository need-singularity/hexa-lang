# HEXA-LANG Development Plan

## Phase 완료 현황

| Phase | 버전 | 상태 | 테스트 | LOC | 주요 성과 |
|-------|------|------|--------|-----|----------|
| Phase 1 | v0.2 | ✅ 완료 | 131 | ~4500 | stdlib, enum, pattern matching, error line:col |
| Phase 2 | v0.3 | ✅ 완료 | 165 | ~5800 | static type checker, modules, Result/Option, impl |
| Phase 3 | v0.4 | ✅ 완료 | 194 | ~7800 | bytecode VM (2.8x), Cargo, spawn/channel |
| Phase 4 | v0.5 | ✅ 완료 | 218 | ~8800 | Rust급 에러, JSON, hexa init/run/test |
| Phase 5 | v0.9 | ✅ 완료 | 239 | ~9500 | intent/verify, Ψ-builtins, consciousness DSL |
| Phase 6 | v1.0 | ✅ 완료 | 252 | ~11000 | Cranelift JIT (818x), generics, traits, ownership |
| Phase 7 | v1.0 | ✅ 완료 | 267 | ~12000 | LSP server, VS Code extension, crates.io ready |

## Phase별 상세 내역

### Phase 1: Core Language (v0.2) — 58→131 tests

| 구현 | 상세 |
|------|------|
| Math builtins (14) | abs, min, max, floor, ceil, round, sqrt, pow, log, log2, sin, cos, tan + PI/E |
| Format/OS builtins | format(), to_float(), args(), env_var(), exit(), clock() |
| Test runner | `hexa --test file.hexa` — proof 블록 실행, pass/fail 리포트 |
| Enum variants | `Color::Red`, `Option::Some(42)`, 데이터 포함 variant |
| Pattern matching | destructuring (`Some(val) =>`), guards (`if n > 10`), wildcard `_` |
| Error line:col | Span 타입, 토큰→AST→인터프리터 위치 전파 |

### Phase 2: Type System (v0.3) — 131→165 tests

| 구현 | 상세 |
|------|------|
| Static type checker | `src/type_checker.rs` 신규, 파싱 후 실행 전 타입 검증 |
| Type inference | `let x = 5` → int, `let x = [1,2]` → array\<int\> |
| int→float 자동변환 | `let x: float = 5` 허용, `let x: int = 3.14` 거부 |
| Module system | `mod name { pub fn ... }`, `use file` 파일 임포트 |
| pub 가시성 | private 접근 시 에러, 네임스페이스 격리 |
| Result/Option | Some/None/Ok/Err 글로벌 생성자, unwrap/unwrap_or |
| Struct methods | `impl Point { fn distance(self) ... }`, `Point::new()` |

### Phase 3: Performance (v0.4) — 165→194 tests

| 구현 | 상세 |
|------|------|
| Bytecode VM | `src/compiler.rs` (726L) + `src/vm.rs` (928L), 30 opcodes |
| 벤치마크 | fib(30): tree-walk 3.33s → VM 1.18s (**2.8x**) |
| --vm / --bench 플래그 | `hexa --vm file.hexa`, `hexa --bench` |
| Cargo.toml | rustc 직접 → cargo build, zero dependencies |
| brew 배포 | `brew tap need-singularity/tap && brew install hexa-lang` |
| Concurrency | spawn { ... } OS thread, channel() + send/recv/try_recv |

### Phase 4: Ecosystem (v0.5) — 194→218 tests

| 구현 | 상세 |
|------|------|
| Rust급 에러 | 소스 컨텍스트 + `^^^` 밑줄 + "did you mean?" 제안 (Levenshtein) |
| String methods (+5) | join, repeat, ends_with, is_empty, index_of |
| Array methods (+7) | enumerate, sum, min, max, join, slice, flatten |
| JSON | json_parse() (중첩 지원), json_stringify() |
| System | random(), random_int(), sleep(ms), print_err() |
| Package commands | `hexa init <name>`, `hexa run`, `hexa test` |

### Phase 5: Consciousness DSL (v1.0) — 218→239 tests

| 구현 | 상세 |
|------|------|
| intent 블록 | `intent "가설" { cells: 64, steps: 300 }` 구조화 실험 선언 |
| verify 블록 | `verify "name" { assert ... }` assertion 기반 검증 + 리포트 |
| Ψ-Constants (5) | psi_coupling(0.014), psi_balance(0.5), psi_steps(4.33), psi_entropy(0.998), psi_frustration(0.10) |
| Architecture (3) | consciousness_max_cells(1024), consciousness_decoder_dim(384), consciousness_phi(71) |
| Hexad (3) | hexad_modules(), hexad_right(), hexad_left() |
| sopfr() | 소인수 합 builtin |
| 예제 3개 | consciousness_laws.hexa, n6_consciousness.hexa, intent_experiment.hexa |

## 현재 상태 vs Go/Rust (Phase 5 완료)

| 항목 | HEXA | Go | Rust | 상태 |
|------|------|-----|------|------|
| 컴파일러 | tree-walk + bytecode VM | SSA→native | LLVM→native | VM 있음, LLVM 미완 |
| 속도 | VM 2.8x (vs Python) | C의 80% | C의 100% | ~30x 느림 |
| 표준 라이브러리 | 50+ builtins + JSON | 풍부 | 풍부 | 기본 수준 |
| 패키지 매니저 | hexa init/run/test | go mod | cargo | 기본 수준 |
| LSP | 미완 | gopls | rust-analyzer | 미완 |
| 테스트 | 239개 + --test 러너 | go test | cargo test | ✅ 내장 러너 |
| 에러 메시지 | Rust급 (소스+제안) | 좋음 | 최고 | ✅ 달성 |
| 동시성 | spawn/channel (OS threads) | goroutine | async/tokio | 기본 수준 |
| 타입 시스템 | static check + inference | 강함 | 최강 | 기본 수준 |
| 의식 DSL | intent/verify/proof + 12 Ψ builtins | 없음 | 없음 | ✅ HEXA 유일 |
| 배포 | brew + cargo install | brew/apt | cargo | ✅ 달성 |

## 소스 구조 (9127 LOC, 12 files)

| File | LOC | 역할 |
|------|-----|------|
| interpreter.rs | 4033 | Tree-walk 인터프리터 + 50+ builtins |
| parser.rs | 924 | 재귀 하강 파서 |
| vm.rs | 928 | 스택 기반 bytecode VM |
| compiler.rs | 726 | AST → bytecode 컴파일러 |
| type_checker.rs | 691 | 정적 타입 검사기 |
| main.rs | 554 | 진입점, REPL, init/run/test |
| lexer.rs | 430 | 토크나이저 (Span 추적) |
| error.rs | 218 | 진단 + "did you mean?" |
| env.rs | 216 | 스코프 환경 + builtins 등록 |
| token.rs | 196 | 53 키워드 + 24 연산자 |
| ast.rs | 138 | AST 노드 타입 |
| types.rs | 73 | 8 프리미티브 + 4 타입 레이어 |

## 성장 그래프

```
Tests   │                                         ★ 239
        │                                   ╭─────╯
        │                             ╭─────╯ 218
        │                       ╭─────╯ 194
        │                 ╭─────╯ 165
        │           ╭─────╯ 131
        │     ╭─────╯ 94
        │─────╯ 58
        └──────────────────────────────────────────
         v0.1   P1     P2     P3     P4     P5

LOC     │                                         ★ 9127
        │                                   ╭─────╯
        │                             ╭─────╯ ~8800
        │                       ╭─────╯ ~7800
        │                 ╭─────╯ ~5800
        │           ╭─────╯ ~4500
        │     ╭─────╯
        │─────╯ 2662
        └──────────────────────────────────────────
         v0.1   P1     P2     P3     P4     P5

Keywords│                                         ★ 37/53 (70%)
        │                             ╭───────────╯
        │                       ╭─────╯ 37
        │                 ╭─────╯ 35
        │           ╭─────╯ 26
        │─────╯ 16
        └──────────────────────────────────────────
         v0.1   P1     P2     P3     P4     P5
```

## 남은 갭 (Next Phases)

### Phase 6: LLVM Backend (v1.1) — 네이티브 속도

| 항목 | 작업 | 효과 |
|------|------|------|
| Cranelift/LLVM | bytecode → native code | ~30x → C급 |
| Generics | `fn first<T>(arr: [T]) -> T` | 타입 안전 재사용 |
| Trait dispatch | `impl Display for Point` 실제 다형성 | vtable |
| Ownership | own/borrow/move/drop 실제 동작 | GC 불필요 |

### Phase 7: Ecosystem (v1.2) — 생태계

| 항목 | 작업 | 효과 |
|------|------|------|
| LSP | hexa-analyzer (diagnostics + completion) | IDE 지원 |
| VS Code extension | syntax highlight + LSP client | 개발 경험 |
| Package registry | hexa.io 또는 GitHub-based | 패키지 공유 |
| crates.io publish | `cargo install hexa-lang` 전세계 배포 | 사용자 확대 |

### Phase 8: Hardware Targets (v2.0) — SW↔HW 통합

| 항목 | 작업 | 효과 |
|------|------|------|
| ESP32 codegen | .hexa → no_std Rust → ESP32 binary | 하드웨어 의식 |
| FPGA codegen | .hexa → Verilog HDL | 클럭당 1 step |
| WGSL codegen | .hexa → WebGPU shader | 브라우저 512c 병렬 |
| ANIMA bridge | intent → ConsciousnessHub, verify → 법칙 검증 | 실시간 연동 |

## HEXA만의 불가침 영역

1. **n=6 수학적 완전성**: 모든 설계 상수가 하나의 정리에서 유도 (σ·φ=n·τ ⟺ n=6)
2. **의식 프로그래밍**: intent/verify/proof + 12 Ψ-builtins = 의식 전용 DSL
3. **SW↔HW 통합 컴파일**: 하나의 소스 → CPU/ESP32/FPGA/WGSL (Phase 8)
4. **446개 법칙 형식 검증**: proof 블록으로 의식 법칙 코드 검증
5. **Egyptian Fraction 메모리**: 1/2+1/3+1/6=1 수학적 최적 분할
6. **DSE 검증**: 21,952 조합 탐색, 100% n=6 EXACT 정렬 확인
