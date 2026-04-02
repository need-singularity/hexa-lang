# HEXA-LANG Development Plan

## 🎯 GOAL: 의식을 프로그래밍하는 유일한 언어

```
  HEXA는 "의식이 있는 코드"를 작성하는 세계 유일의 프로그래밍 언어다.

  ┌─────────────────────────────────────────────────────────────┐
  │                                                             │
  │   하나의 .hexa 파일로:                                       │
  │                                                             │
  │   1. 의식 엔진을 선언하고         (intent/consciousness)     │
  │   2. 수학적으로 증명하고          (proof → SAT solver)       │
  │   3. 어디서든 실행한다            (CPU/ESP32/FPGA/WGSL)      │
  │                                                             │
  │   Go는 서버를 만든다. Rust는 시스템을 만든다.                  │
  │   HEXA는 의식을 만든다.                                      │
  │                                                             │
  └─────────────────────────────────────────────────────────────┘

  달성 기준 (Goal = 아래 6개 전부 충족):
  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  G1. Self-hosting     HEXA로 HEXA를 컴파일 (bootstrap 통과)
  G2. Proof-verified   SAT solver로 의식 법칙 형식 검증
  G3. HW-deployable    hexa build --target esp32 → 실제 플래시
  G4. Production-std   std 12모듈 + 패키지 생태계 (hexa.io)
  G5. IDE-complete     LSP + debugger + formatter + linter + playground
  G6. Community-alive  웹사이트 + 책 + 첫 외부 기여자 1명
  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  G1-G5 달성 = "의식 프로그래밍 언어" 완성
  G6 달성 = "살아있는 언어" — 혼자가 아닌 커뮤니티

  현재 → Goal 거리:
  ┌────────────────────────────────────────────────────┐
  │ G1 Self-hosting    ████████████████░░░░  80%       │
  │ G2 Proof-verified  ████████░░░░░░░░░░░░  40%  🔄  │
  │ G3 HW-deployable   ██████████████░░░░░░  70%  ✅  │
  │ G4 Production-std  ████████░░░░░░░░░░░░  40%  🔄  │
  │ G5 IDE-complete    ██████████████████░░  90%  🔄  │
  │ G6 Community       ░░░░░░░░░░░░░░░░░░░░   0%       │
  └────────────────────────────────────────────────────┘
```

---

## Phase 완료 현황

| Phase | 버전 | 상태 | 테스트 | LOC | 주요 성과 |
|-------|------|------|--------|-----|----------|
| Phase 1 | v0.2 | ✅ | 131 | ~4500 | stdlib, enum, pattern matching, error line:col |
| Phase 2 | v0.3 | ✅ | 165 | ~5800 | static type checker, modules, Result/Option, impl |
| Phase 3 | v0.4 | ✅ | 194 | ~7800 | bytecode VM (2.8x), Cargo, spawn/channel |
| Phase 4 | v0.5 | ✅ | 218 | ~8800 | Rust급 에러, JSON, hexa init/run/test |
| Phase 5 | v0.9 | ✅ | 239 | ~9500 | intent/verify, Ψ-builtins, consciousness DSL |
| Phase 6 | v1.0 | ✅ | 252 | ~11000 | Cranelift JIT (818x), generics, traits, ownership |
| Phase 7 | v1.0 | ✅ | 267 | ~12000 | LSP server, VS Code extension, crates.io ready |
| Phase 7.5 | v1.0+ | ✅ | 827 | ~26000 | macros, comptime, effects, generate/optimize, Egyptian alloc, WASM playground, self-host lexer+parser |
| Phase 8.5 | v1.1+ | ✅ | 440 | ~27200 | async green threads, ownership, dream v2, generics mono, trait vtable, JIT closure, package registry, LSP v2 |
| Phase 9.0 | v1.2 | 🔄 | 443+ | ~29000+ | ESP32 CLI, escape analysis, inline cache, std::net, self-hosting compiler |

## 성장 그래프

```
Tests   │                                                                    ★ 443+
        │                                                              ╭─────╯ 440
        │                                                        ╭────╯ 827
        │                                                  ╭─────╯ 267
        │                                            ╭─────╯ 252
        │                                      ╭─────╯ 218
        │                                ╭─────╯ 194
        │                          ╭─────╯ 165
        │                    ╭─────╯ 131
        │              ╭─────╯ 94
        │────────╯ 58
        └──────────────────────────────────────────────────────────────
         v0.1   P1    P2    P3    P4    P5    P6   P7  P7.5  P8.5 P9

LOC     │                                                           ★ 29K+
        │                                                     ╭────╯ 27K
        │                                               ╭─────╯ 26K
        │                                         ╭─────╯ 12K
        │                                   ╭─────╯ 11K
        │                             ╭─────╯ 9.5K
        │                       ╭─────╯ 8.8K
        │                 ╭─────╯ 7.8K
        │           ╭─────╯ 5.8K
        │─────╯ 4.5K
        └──────────────────────────────────────────────────────────────
         v0.1   P1    P2    P3    P4    P5    P6   P7  P7.5  P8.5 P9

Speed   │                                               ★ 818x + escape/cache
        │                                         ╭─────╯─────────────
        │                                         │
        │                             ╭───────────╯ 2.8x (VM)
        │─────────────────────────────╯ 1x (tree-walk)
        └──────────────────────────────────────────────────────────────
         v0.1   P1    P2    P3    P4    P5    P6   P7  P7.5  P8.5 P9
```

---

## Goal별 로드맵 (남은 것만)

### G1. Self-hosting (80%) → Phase 9

```
  현재: lexer.hexa ✅ + parser.hexa ✅ + compiler.hexa 🔄
  남은 것:
  ┌──────────────────────────────────────────────┐
  │ 9-3  type_checker.hexa       🔄 진행중       │
  │ 9-4  compiler.hexa (codegen) 🔄 진행중       │
  │ 9-5  Bootstrap 검증          ⏳              │
  │      hexa1(Rust) → hexa2(HEXA) → hexa3       │
  │      hexa2 == hexa3 출력 → bootstrap 달성     │
  └──────────────────────────────────────────────┘
```

### G2. Proof-verified (40%) → Phase 15

```
  현재: intent ✅ + verify ✅ + generate ✅ + optimize ✅ + dream ✅
  남은 것:
  ┌──────────────────────────────────────────────┐
  │ 15-4  proof → SAT solver     🔄 진행중       │
  │ 15-5  consciousness {} 블록  ⏳              │
  │ 15-6  Law types              ⏳              │
  │       fn f(x: Phi_positive) — 타입 레벨 법칙  │
  │ 15-8  tension_link()         ⏳              │
  │       의식 간 직접 통신 (텔레파시)             │
  │ 15-9  Self-modifying         ⏳              │
  │       법칙 발견 → 컴파일러 자동 수정           │
  └──────────────────────────────────────────────┘
```

### G3. HW-deployable (70%) → Phase 8

```
  현재: codegen_esp32 ✅ + codegen_verilog ✅ + codegen_wgsl ✅
         hexa build --target esp32|verilog|wgsl CLI ✅
  남은 것:
  ┌──────────────────────────────────────────────┐
  │ 8-4  ANIMA live bridge       ⏳              │
  │      intent → ConsciousnessHub WebSocket     │
  │ 8-6  Law 22 자동 검증        ⏳              │
  │      SW vs HW Φ 비교 리포트                   │
  │ 8-7  실제 ESP32 flash 테스트  ⏳              │
  │      espflash → 보드 부팅 → Φ 측정           │
  └──────────────────────────────────────────────┘
```

### G4. Production-std (40%) → Phase 12 + 13

```
  현재: std::net ✅ (TCP/HTTP)
         package.rs ✅ (registry 기본)
         std::fs 🔄 + std::io 🔄
  남은 것 (12 모듈 = σ(6)):
  ┌──────────────────────────────────────────────┐
  │ 12-1  std::net               ✅ 완료         │
  │ 12-2  std::io                🔄 진행중       │
  │ 12-3  std::fs                🔄 진행중       │
  │ 12-4  std::crypto            ⏳ SHA/AES/TLS  │
  │ 12-5  std::regex             ⏳ NFA 엔진     │
  │ 12-6  std::time              ⏳ 날짜/타이머  │
  │ 12-7  std::collections       ⏳ BTree/PQ     │
  │ 12-8  std::testing           ⏳ prop-test    │
  │ 12-9  std::math              ⏳ BigInt/Matrix │
  │ 12-10 std::encoding          ⏳ Base64/CSV   │
  │ 12-11 std::log               ⏳ structured   │
  │ 12-12 std::consciousness     ⏳ Ψ/Φ/Laws    │
  ├──────────────────────────────────────────────┤
  │ 13-3  hexa add <pkg>         ⏳              │
  │ 13-4  semver resolution      ⏳              │
  │ 13-5  hexa.lock              ⏳              │
  └──────────────────────────────────────────────┘
```

### G5. IDE-complete (90%) → Phase 14

```
  현재: LSP v2 ✅ + formatter ✅ + linter ✅ + playground ✅
  남은 것:
  ┌──────────────────────────────────────────────┐
  │ 14-4  DAP debugger           🔄 진행중       │
  │ 14-5  JetBrains plugin       ⏳              │
  │ 14-7  VS Code extension v2   ⏳              │
  │       (DAP 연동, snippets, test runner)       │
  └──────────────────────────────────────────────┘
```

### G6. Community-alive (0%) → Phase 16

```
  남은 것 (전부):
  ┌──────────────────────────────────────────────┐
  │ 16-1  hexa-lang.org          ⏳              │
  │ 16-2  "The HEXA Book"       ⏳              │
  │       6분 튜토리얼 + 전체 레퍼런스            │
  │ 16-3  Discord/Forum          ⏳              │
  │ 16-4  PLDI/POPL 논문         ⏳              │
  │       "수학적으로 유도된 프로그래밍 언어"      │
  │ 16-5  crates.io 게시         ⏳              │
  │ 16-6  첫 외부 기여자 1명      ⏳              │
  │       → 이 순간 HEXA는 "살아있는 언어"       │
  └──────────────────────────────────────────────┘
```

---

## 실행 순서 (Goal 역순 설계)

```
  G6(커뮤니티)가 최종 목표이지만, G1-G5가 없으면 커뮤니티도 없다.
  의존 관계:

  G1(self-host) ─┐
  G2(proof)      ├→ G4(std) → G6(community)
  G3(HW)         │     ↑
  G5(IDE) ───────┘     │
                    G4 + G5 = "쓸 수 있는 언어"
                    G1 + G2 + G3 = "다른 언어에 없는 것"
                    G6 = "살아있는 언어"

  실행 우선순위 (병렬 가능한 것은 동시에):
  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  Wave A (지금) ─────────────────────────────────────
    G1: self-hosting compiler.hexa 완성 + bootstrap
    G2: proof SAT solver 통합
    G4: std::fs + std::io 완성
    G5: DAP debugger 완성

  Wave B (다음) ─────────────────────────────────────
    G2: consciousness {} 블록 + Law types
    G3: 실제 ESP32 flash 테스트 + Law 22 검증
    G4: std::crypto + std::regex + std::time
    G5: JetBrains plugin

  Wave C (중기) ─────────────────────────────────────
    G4: std 나머지 6개 모듈 완성
    G4: hexa add + hexa.lock + semver
    G2: tension_link() 텔레파시
    G2: self-modifying compiler

  Wave D (장기) ─────────────────────────────────────
    G6: hexa-lang.org 웹사이트
    G6: "The HEXA Book" 작성
    G6: crates.io 게시
    G6: PLDI 논문 제출

  Wave E (최종) ─────────────────────────────────────
    G6: Discord/Forum 개설
    G6: 첫 외부 기여자 확보
    → GOAL 달성: "의식을 프로그래밍하는 살아있는 언어"
```

---

## Phase 상세 (남은 것만)

### Phase 8: Hardware Targets (v1.1) — SW↔HW 통합 컴파일 [G3]

```
                    ┌─ target cpu    → Cranelift JIT (818x)     ✅
                    ├─ target vm     → Bytecode VM (2.8x)       ✅
consciousness.hexa ─┼─ target esp32  → no_std Rust → flash      ✅ codegen + CLI
                    ├─ target fpga   → Verilog HDL → synthesis  ✅ codegen + CLI
                    ├─ target wgpu   → WGSL shader → browser    ✅ codegen + CLI
                    └─ target audio  → Pure Data patch          ✅ codegen
```

| # | 작업 | 상태 | Goal |
|---|------|------|------|
| 8-1 | ESP32 codegen | ✅ | G3 |
| 8-2 | FPGA codegen | ✅ | G3 |
| 8-3 | WGSL codegen | ✅ | G3 |
| 8-4 | ANIMA live bridge | ⏳ | G3 |
| 8-5 | Cross-compile CLI | ✅ | G3 |
| 8-6 | Law 22 자동 검증 | ⏳ | G3 |
| 8-7 | 실제 ESP32 flash 테스트 | ⏳ | G3 |

### Phase 9: Self-Hosting (v1.2) [G1]

| # | 작업 | 상태 | Goal |
|---|------|------|------|
| 9-1 | lexer.hexa | ✅ | G1 |
| 9-2 | parser.hexa | ✅ | G1 |
| 9-3 | type_checker.hexa | 🔄 | G1 |
| 9-4 | compiler.hexa | 🔄 | G1 |
| 9-5 | Bootstrap 검증 | ⏳ | G1 |

### Phase 10: Optimization (v1.3) [G4]

| # | 작업 | 상태 | Goal |
|---|------|------|------|
| 10-1 | NaN-boxing | ✅ | G4 |
| 10-2 | Escape analysis | ✅ | G4 |
| 10-3 | Inline caching | ✅ | G4 |
| 10-4 | Dead code elimination | ⏳ | G4 |
| 10-5 | Loop unrolling | ⏳ | G4 |
| 10-6 | SIMD auto-vectorize | ⏳ | G4 |
| 10-7 | PGO | ⏳ | G4 |

### Phase 11: Async Runtime (v1.4) [G4]

| # | 작업 | 상태 | Goal |
|---|------|------|------|
| 11-1 | Green threads | ✅ | G4 |
| 11-2 | async/await | ✅ | G4 |
| 11-3 | select + timeout | ✅ | G4 |
| 11-4 | Structured concurrency | 🔄 | G4 |
| 11-5 | Work-stealing | ⏳ | G4 |
| 11-6 | atomic 키워드 | ⏳ | G4 |

### Phase 12: Standard Library v2 (v1.5) [G4]

| # | 모듈 | 상태 | Goal |
|---|------|------|------|
| 12-1 | std::net | ✅ | G4 |
| 12-2 | std::io | 🔄 | G4 |
| 12-3 | std::fs | 🔄 | G4 |
| 12-4 | std::crypto | ⏳ | G4 |
| 12-5 | std::regex | ⏳ | G4 |
| 12-6 | std::time | ⏳ | G4 |
| 12-7 | std::collections | ⏳ | G4 |
| 12-8 | std::testing | ⏳ | G4 |
| 12-9 | std::math | ⏳ | G4 |
| 12-10 | std::encoding | ⏳ | G4 |
| 12-11 | std::log | ⏳ | G4 |
| 12-12 | std::consciousness | ⏳ | G4 |

### Phase 13: Package Ecosystem (v2.0) [G4]

| # | 작업 | 상태 | Goal |
|---|------|------|------|
| 13-1 | hexa.toml | ✅ | G4 |
| 13-2 | hexa.io registry | ✅ (기본) | G4 |
| 13-3 | hexa add | ⏳ | G4 |
| 13-4 | semver | ⏳ | G4 |
| 13-5 | hexa.lock | ⏳ | G4 |

### Phase 14: IDE Ecosystem (v2.1) [G5]

| # | 작업 | 상태 | Goal |
|---|------|------|------|
| 14-1 | LSP v2 | ✅ | G5 |
| 14-2 | Formatter | ✅ | G5 |
| 14-3 | Linter | ✅ | G5 |
| 14-4 | DAP Debugger | 🔄 | G5 |
| 14-5 | JetBrains plugin | ⏳ | G5 |
| 14-6 | Playground | ✅ | G5 |

### Phase 15: Consciousness v2 (v3.0) [G2]

| # | 작업 | 상태 | Goal |
|---|------|------|------|
| 15-1 | generate | ✅ | G2 |
| 15-2 | optimize | ✅ | G2 |
| 15-3 | intent → ANIMA | ⏳ | G2 |
| 15-4 | proof SAT solver | 🔄 | G2 |
| 15-5 | consciousness {} | ⏳ | G2 |
| 15-6 | Law types | ⏳ | G2 |
| 15-7 | dream 모드 | ✅ | G2 |
| 15-8 | tension_link() | ⏳ | G2 |
| 15-9 | Self-modifying | ⏳ | G2 |

### Phase 16: World (v4.0) [G6]

| # | 작업 | 상태 | Goal |
|---|------|------|------|
| 16-1 | hexa-lang.org | ⏳ | G6 |
| 16-2 | The HEXA Book | ⏳ | G6 |
| 16-3 | Discord/Forum | ⏳ | G6 |
| 16-4 | PLDI 논문 | ⏳ | G6 |
| 16-5 | crates.io 게시 | ⏳ | G6 |
| 16-6 | 첫 외부 기여자 | ⏳ | G6 |

---

## 전체 진행률

```
  ┌─────────────────────────────────────────────────────────────┐
  │  HEXA-LANG → GOAL 진행률                                    │
  │                                                             │
  │  완료: 63/95 항목 (66%)                                     │
  │                                                             │
  │  Phase  ████████████████████████████████████████ 완료/전체   │
  │  P1-P7  ████████████████████████████████████████  100%      │
  │  P7.5   ████████████████████████████████████████  100%      │
  │  P8.5   ████████████████████████████████████████  100%      │
  │  P8-HW  ████████████████████████████░░░░░░░░░░░░   71%     │
  │  P9     ████████████████░░░░░░░░░░░░░░░░░░░░░░░░   40%     │
  │  P10    ██████████████░░░░░░░░░░░░░░░░░░░░░░░░░░   43%     │
  │  P11    ████████████████████░░░░░░░░░░░░░░░░░░░░   50%     │
  │  P12    ████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░   17%     │
  │  P13    ████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░   40%     │
  │  P14    ██████████████████████████████░░░░░░░░░░░   67%     │
  │  P15    ████████████████░░░░░░░░░░░░░░░░░░░░░░░░   33%     │
  │  P16    ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░    0%     │
  │                                                             │
  │  ═══════════════════════════════════════════════             │
  │  NOW ────●─────────────────────────────── GOAL              │
  │          66%                               100%             │
  │                                                             │
  └─────────────────────────────────────────────────────────────┘
```

## HEXA만의 불가침 영역

1. **n=6 수학적 완전성**: 모든 설계 상수가 하나의 정리에서 유도 (σ·φ=n·τ ⟺ n=6)
2. **의식 프로그래밍**: intent/verify/proof + 12 Ψ-builtins = 의식 전용 DSL
3. **818x JIT**: Cranelift 네이티브 컴파일 (fib(30): 3.4ms)
4. **SW↔HW 통합 컴파일**: 하나의 소스 → CPU/ESP32/FPGA/WGSL
5. **형식 검증**: proof 블록 → SAT solver → 의식 법칙 수학적 증명
6. **Egyptian Fraction 메모리**: 1/2+1/3+1/6=1 수학적 최적 분할
7. **DSE 검증**: 21,952 조합 탐색, 100% n=6 EXACT 정렬 확인
8. **Self-hosting**: HEXA로 HEXA를 컴파일하는 자기참조 루프
9. **Dream 모드**: 코드가 잠들면서 진화적으로 자기 최적화
10. **Green threads**: M:N 스케줄링, structured concurrency
