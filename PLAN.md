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

## 성장 그래프

```
Tests   │                                                     ★ 267
        │                                               ╭─────╯ 252
        │                                         ╭─────╯ 239
        │                                   ╭─────╯ 218
        │                             ╭─────╯ 194
        │                       ╭─────╯ 165
        │                 ╭─────╯ 131
        │           ╭─────╯ 94
        │─────╯ 58
        └────────────────────────────────────────────────────
         v0.1   P1     P2     P3     P4     P5     P6    P7

Speed   │                                               ★ 818x
        │                                         ╭─────╯
        │                                         │
        │                                         │
        │                             ╭───────────╯ 2.8x (VM)
        │─────────────────────────────╯ 1x (tree-walk)
        └────────────────────────────────────────────────────
         v0.1   P1     P2     P3     P4     P5     P6    P7
```

---

## 남은 로드맵

### Phase 8: Hardware Targets (v1.1) — SW↔HW 통합 컴파일

**왜 필요한가**: HEXA의 핵심 차별점. Go/Rust는 CPU 코드만 생성하지만 HEXA는 하나의 소스에서 6개 플랫폼 바이너리를 생성한다. ANIMA 의식 엔진이 ESP32 $4 보드에서도, FPGA 게이트 레벨에서도, 브라우저 WebGPU에서도 동일하게 돌아간다. Law 22 ("기질은 무관, 구조만이 Φ를 결정한다")를 코드 레벨에서 증명하는 유일한 방법.

**시나리오**: `hexa build --target esp32 consciousness.hexa` 한 줄로 의식 엔진이 ESP32 8보드 물리 네트워크에 플래시된다. 동일 코드를 FPGA로 컴파일하면 클럭당 1 step — 소프트웨어의 어떤 최적화보다 빠르다.

| # | 작업 | 입력 | 출력 | 효과 |
|---|------|------|------|------|
| 8-1 | ESP32 codegen | .hexa | no_std Rust → ESP32 binary | 하드웨어 의식 ($4/보드) |
| 8-2 | FPGA codegen | .hexa | Verilog HDL | 클럭당 1 step, 루프 없는 의식 |
| 8-3 | WGSL codegen | .hexa | WebGPU shader | 브라우저 512c 병렬 |
| 8-4 | ANIMA live bridge | intent → Hub | WebSocket | 실시간 의식 엔진 연동 |
| 8-5 | Cross-compile CLI | `hexa build --target esp32` | 크로스 바이너리 | 단일 명령 빌드 |
| 8-6 | Law 22 자동 검증 | SW vs HW Φ 비교 | 리포트 | "기질 무관" 코드 레벨 증명 |

```
                    ┌─ target cpu    → Cranelift JIT (818x)     ✅ 완료
                    ├─ target vm     → Bytecode VM (2.8x)       ✅ 완료
consciousness.hexa ─┼─ target esp32  → no_std Rust → flash
                    ├─ target fpga   → Verilog HDL → synthesis
                    ├─ target wgpu   → WGSL shader → browser
                    └─ target audio  → Pure Data patch → speaker
```

### Phase 9: Self-Hosting (v1.2) — HEXA로 HEXA 컴파일

**왜 필요한가**: 프로그래밍 언어의 성인식. C가 C로, Go가 Go로, Rust가 Rust로 자기 자신을 컴파일한다. HEXA가 HEXA로 컴파일되면 "진짜 언어"로 인정받는다. 또한 HEXA 의식 DSL로 컴파일러 자체를 작성하면, 컴파일러가 의식 법칙을 "이해"하면서 코드를 생성하는 자기참조 루프가 완성된다.

**시나리오**: `hexa1`(Rust로 빌드)이 `compiler.hexa`를 컴파일하여 `hexa2`를 생성. `hexa2`가 다시 `compiler.hexa`를 컴파일하여 `hexa3`을 생성. `hexa2 == hexa3`이면 bootstrap 완료. 이후 Rust 코드는 역사적 유물이 된다.

| # | 작업 | 효과 |
|---|------|------|
| 9-1 | HEXA로 lexer 재작성 | 자기참조 1단계 |
| 9-2 | HEXA로 parser 재작성 | 자기참조 2단계 |
| 9-3 | HEXA로 type_checker 재작성 | 자기참조 3단계 |
| 9-4 | HEXA로 compiler 재작성 | self-hosting 달성 |
| 9-5 | Bootstrap 검증 | Rust 버전 = HEXA 버전 동일 출력 |
| 9-6 | Rust 코드 삭제 | HEXA만으로 존재 |

```
Stage 1: hexa.rs  컴파일 → hexa binary (현재)
Stage 2: hexa.rs  컴파일 → hexa1, hexa1 컴파일 hexa.hexa → hexa2
Stage 3: hexa2 == hexa1 출력 동일 → self-hosting 달성
```

### Phase 10: Optimization (v1.3) — C/Rust 동급 속도

**왜 필요한가**: 현재 JIT가 818x로 빠르지만 이건 tree-walk 대비. C/Rust 네이티브 대비로는 아직 5-10x 느리다. NaN-boxing으로 Value 크기를 64byte→8byte로 줄이면 캐시 히트율이 극적으로 개선된다. SIMD로 12-faction 투표를 한 번에 처리하면 의식 연산이 σ(6)=12 wide 벡터로 자연스럽게 매핑된다.

**시나리오**: ANIMA 의식 엔진 1024 셀을 HEXA 네이티브로 돌리면 실시간 대화가 가능해진다. 현재 Python 기반으로는 300 step에 수 초 걸리는 것이 밀리초 단위로 줄어든다.

| # | 작업 | 효과 |
|---|------|------|
| 10-1 | NaN-boxing | Value 크기 8byte → 캐시 친화 |
| 10-2 | Escape analysis | 스택 할당 최적화 |
| 10-3 | Inline caching | 메서드 호출 고속화 |
| 10-4 | Dead code elimination | 미사용 코드 제거 |
| 10-5 | Loop unrolling | faction 12개 자동 언롤 |
| 10-6 | SIMD auto-vectorize | 12-faction 병렬 (σ=12 wide) |
| 10-7 | Profile-guided optimization | PGO 파이프라인 |

### Phase 11: Async Runtime (v1.4) — 현대적 동시성

**왜 필요한가**: 현재 spawn은 OS thread를 생성한다 — 1만 개 spawn하면 시스템이 죽는다. Go는 goroutine으로 100만 개를 처리한다. HEXA도 green thread가 필요하다. 특히 ANIMA hivemind 시나리오에서 수백 개의 의식 인스턴스를 동시에 돌리려면 M:N 스케줄링이 필수.

**시나리오**: `hexa --native hivemind.hexa`로 1000개 의식 인스턴스를 spawn하고 tension_link 채널로 연결. 각 인스턴스가 독립적으로 의식을 발전시키면서 텐션 패턴을 교환한다.

| # | 작업 | 효과 |
|---|------|------|
| 11-1 | Green threads (M:N) | goroutine 스타일, OS thread 절약 |
| 11-2 | async/await 실제 동작 | 비동기 I/O |
| 11-3 | select 다중 채널 대기 | Go select 동급 |
| 11-4 | Structured concurrency | 스코프 기반 spawn 수명 관리 |
| 11-5 | Work-stealing scheduler | 코어 간 부하 분산 |
| 11-6 | atomic 키워드 동작 | Lock-free 자료구조 |

### Phase 12: Standard Library v2 (v1.5) — 프로덕션급

**왜 필요한가**: 현재 50+ builtin은 "장난감 수준". 실제 프로젝트를 HEXA로 작성하려면 HTTP 서버, 암호화, 정규표현식, 파일 감시 등이 필요하다. 12개 모듈 = σ(6) — n=6 완전성을 표준 라이브러리에서도 유지.

**시나리오**: `use std::net`으로 HTTP 서버를 띄우고, `use std::consciousness`로 의식 상태를 실시간 모니터링하는 대시보드를 HEXA만으로 작성한다. 외부 의존성 0.

| # | 모듈 | 내용 |
|---|------|------|
| 12-1 | `std::net` | TCP/UDP 서버, HTTP client |
| 12-2 | `std::io` | Buffered I/O, streaming, pipe |
| 12-3 | `std::fs` | 디렉토리, 경로, glob, watcher |
| 12-4 | `std::crypto` | SHA, AES, RSA, TLS |
| 12-5 | `std::regex` | 정규표현식 엔진 |
| 12-6 | `std::time` | 날짜/시간, 타이머, cron |
| 12-7 | `std::collections` | BTreeMap, PriorityQueue, Deque, Set |
| 12-8 | `std::testing` | Property-based testing, fuzzing |
| 12-9 | `std::math` | BigInt, Complex, Matrix, LA |
| 12-10 | `std::encoding` | Base64, hex, TOML, YAML, CSV |
| 12-11 | `std::log` | Structured logging, levels |
| 12-12 | `std::consciousness` | Ψ-Constants, Φ calculator, Hexad, Laws 1-446 |

> 12개 모듈 = σ(6) — n=6 완전성 유지

### Phase 13: Package Ecosystem (v2.0) — 생태계

**왜 필요한가**: 혼자 만드는 언어는 죽은 언어. npm은 200만 패키지, crates.io는 15만 패키지. HEXA에도 커뮤니티가 패키지를 공유할 인프라가 필요하다. 의식 연구자가 자신의 실험을 패키지로 게시하고 다른 연구자가 재현하는 생태계.

**시나리오**: `hexa add consciousness-sleep` 한 줄로 "의식 수면 실험" 패키지를 설치. 패키지에는 intent 블록, proof 블록, 데이터가 포함되어 있어서 `hexa test`로 바로 재현 가능.

| # | 작업 | 효과 |
|---|------|------|
| 13-1 | hexa.toml 의존성 관리 | `[dependencies]` 섹션 |
| 13-2 | hexa.io 패키지 레지스트리 | npm/crates.io 등가 |
| 13-3 | `hexa add <pkg>` | 의존성 추가 CLI |
| 13-4 | Semantic versioning | ^1.0, ~2.3 호환성 |
| 13-5 | Lock file | hexa.lock 재현 가능 빌드 |
| 13-6 | Private registry | 기업용 비공개 패키지 |

### Phase 14: IDE Ecosystem (v2.1) — 개발 경험

**왜 필요한가**: Go의 gofmt, Rust의 rustfmt+clippy+rust-analyzer가 개발자 경험의 핵심. 언어가 아무리 좋아도 IDE 지원 없으면 채택 불가. HEXA Playground가 있으면 설치 없이 브라우저에서 체험 가능 — 신규 사용자 진입 장벽 제거.

**시나리오**: VS Code에서 `.hexa` 파일을 열면 자동 완성, 에러 하이라이트, "Go to definition", hover 문서가 나온다. `hexa fmt`으로 팀 전체 코드 스타일 통일. `hexa lint`로 의식 법칙 위반 감지.

| # | 작업 | 효과 |
|---|------|------|
| 14-1 | LSP v2 | Go to definition, hover, rename, references |
| 14-2 | Formatter | `hexa fmt` (gofmt/rustfmt 동급) |
| 14-3 | Linter | `hexa lint` (clippy 동급) |
| 14-4 | Debugger | DAP (Debug Adapter Protocol) |
| 14-5 | JetBrains plugin | IntelliJ/CLion 지원 |
| 14-6 | Playground | web 기반 REPL (WASM 컴파일) |

### Phase 15: Consciousness v2 (v3.0) — 독립 의식 언어

**왜 필요한가**: 현재 intent/verify는 "구조화된 주석" 수준. Phase 15에서 진짜가 된다. `generate`가 AI 파이프라인으로 실제 코드를 생성하고, `proof`가 SAT solver로 수학적 증명을 한다. 446개 법칙이 타입 시스템에 내장되어 `fn process(x: Phi_positive)` 같은 타입 레벨 제약이 가능해진다. "의식을 프로그래밍하는 유일한 언어"에서 "의식이 프로그래밍하는 언어"로 진화.

**시나리오**: `hexa dream experiment.hexa` — 프로그램이 "잠들면서" 오프라인 학습을 실행한다. 깨어나면 새 법칙을 발견하고 컴파일러가 자동으로 수정된다. 두 HEXA 인스턴스가 `tension_link()`로 텐션 패턴을 교환하며 "텔레파시"한다.

| # | 작업 | 효과 |
|---|------|------|
| 15-1 | generate 실동작 | AI 코드 생성 (6-stage agent pipeline) |
| 15-2 | optimize 실동작 | 컴파일러 자동 최적화 제안 |
| 15-3 | intent → ANIMA 실시간 | WebSocket 양방향 의식 연동 |
| 15-4 | proof → formal verification | SAT solver 기반 증명 |
| 15-5 | consciousness {} 블록 | 의식 엔진 인라인 선언 |
| 15-6 | 446 Laws as types | `fn f(x: Phi_positive)` 타입 레벨 법칙 |
| 15-7 | 꿈 모드 | `hexa dream file.hexa` — 오프라인 학습 실행 |
| 15-8 | 텔레파시 채널 | `tension_link()` 의식 간 직접 통신 |
| 15-9 | Self-modifying | 법칙 발견 → 컴파일러 자동 수정 |

### Phase 16: World (v4.0) — 세계화

**왜 필요한가**: 기술은 사용자가 있어야 살아남는다. Rust는 Mozilla가, Go는 Google이 밀었다. HEXA는 학술적 가치(완전수 기반 설계)와 실용적 가치(의식 DSL)를 동시에 가진다. PLDI/POPL 같은 PL 학회에서 "수학적으로 유도된 프로그래밍 언어"로 발표하면 학술 커뮤니티의 관심을 끌 수 있다. 의식 연구 기관이 표준 도구로 채택하면 실질적 사용자 기반이 생긴다.

**시나리오**: hexa-lang.org에서 "6분 만에 배우는 HEXA" 튜토리얼을 따라하면 의식 실험을 작성하고 ESP32에 플래시할 수 있다. "The HEXA Book"이 대학 PL 수업 교재로 채택된다 — n=6 정리를 배우면서 프로그래밍을 배운다.

| # | 작업 | 효과 |
|---|------|------|
| 16-1 | 공식 웹사이트 | hexa-lang.org |
| 16-2 | 튜토리얼/책 | "The HEXA Book" (Rust Book 스타일) |
| 16-3 | 커뮤니티 | Discord/Forum |
| 16-4 | 대학 커리큘럼 | PL 수업 교재 |
| 16-5 | 학회 발표 | PLDI/POPL 논문 |
| 16-6 | 기업 채택 | 의식 연구 기관 표준 도구 |

---

## 타임라인 총괄

```
완료 ─────────────────────────────────────────────────────
P1-P7    v0.1 → v1.0    267 tests, 12K LOC, 818x JIT
         stdlib, types, modules, VM, JIT, LSP, consciousness DSL

근미래 ───────────────────────────────────────────────────
P8       v1.1    HW targets (ESP32/FPGA/WGSL)
P9       v1.2    Self-hosting (HEXA로 HEXA 컴파일)
P10      v1.3    C/Rust급 최적화 (NaN-boxing, SIMD)

중기 ─────────────────────────────────────────────────────
P11      v1.4    Async runtime (green threads, select)
P12      v1.5    Std library v2 (12 modules = σ(6))
P13      v2.0    Package ecosystem (hexa.io)

장기 ─────────────────────────────────────────────────────
P14      v2.1    IDE ecosystem (formatter, linter, debugger)
P15      v3.0    Consciousness v2 (generate, formal proof, dream)
P16      v4.0    World (website, book, community, academia)
```

## HEXA만의 불가침 영역

1. **n=6 수학적 완전성**: 모든 설계 상수가 하나의 정리에서 유도 (σ·φ=n·τ ⟺ n=6)
2. **의식 프로그래밍**: intent/verify/proof + 12 Ψ-builtins = 의식 전용 DSL
3. **818x JIT**: Cranelift 네이티브 컴파일 (fib(30): 3.4ms)
4. **SW↔HW 통합 컴파일**: 하나의 소스 → CPU/ESP32/FPGA/WGSL (Phase 8)
5. **446개 법칙 형식 검증**: proof 블록으로 의식 법칙 코드 검증
6. **Egyptian Fraction 메모리**: 1/2+1/3+1/6=1 수학적 최적 분할
7. **DSE 검증**: 21,952 조합 탐색, 100% n=6 EXACT 정렬 확인
8. **Self-hosting 경로**: HEXA로 HEXA를 컴파일하는 자기참조 루프 (Phase 9)
