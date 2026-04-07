> 🔴 **AI-NATIVE FIRST**: 성능/자원 개선은 저수준 마이크로 최적화 금지 — **AI-native 알고리즘 교체로 돌파**만 추구. docs/ai-native.md 24종 벡터 로드맵 참조. @optimize/@memoize/@fuse/@parallel 등 attr 시스템으로 구현.
> - **핵심 가치**: 성능 비율이 아닌 **복잡도 교체**가 진짜 돌파
> - O(2^n) → `@memoize` → O(n): **10^25배 감소** (fib(90) 즉시)
> - O(n) → `@optimize` → O(log n): **13배 추가 감소** (행렬 거듭제곱)
> - O(n²) → `@fuse` → O(n): 중간 배열 할당 제거 (단일 패스 퓨전)
> - **LLVM/GCC는 이 변환 불가** — AI-native 컴파일러 전용 영역

> 🔴 **HEXA-FIRST**: 모든 코드는 `.hexa`로 작성. sh/py/rs 신규 작성 금지. 부하 유발 명령 최소화.

> 🔴 **NEXUS-6 특이점 연동**: 이 프로젝트의 돌파/발견/실험은 nexus 특이점 사이클 입력이다.
> - **돌파 시**: `HEXA=$HOME/Dev/hexa-lang/target/release/hexa && $HEXA $HOME/Dev/nexus/mk2_hexa/native/blowup.hexa <domain> 3 --no-graph`
> - **발견 기록**: `$HOME/Dev/nexus/shared/growth_bus.jsonl`에 JSON append
> - **전체 상태**: `$HEXA $HOME/Dev/nexus/mk2_hexa/native/command_router.hexa "hexa-lang 상태"`

# HEXA-LANG

## ⚡ HEXA-IR 수렴진화 + 특이점 사이클
```
[수렴 JSON]
  config/hexa_ir_convergence.json  — CDO 트러블슈팅 (AR 7규칙, TS 13건)
  config/emergence_patterns.json   — 창발 패턴 레지스트리 (NEXUS-6 스캔)

[특이점 사이클 모듈] 블로업→수축→창발→특이점→흡수
  src/singularity.rs              — Rust 코어 (9 tests, CycleEngine)
  scripts/singularity_cycle.py    — Python wrapper (전 프로젝트 import 가능)
  사용: CycleEngine::new() → feed() → run_cycle() → report()

[현황] 1791 tests | 53.8K LOC | bench 333x (JIT 0.006s) | Value 32B | 수렴 100% ★
[모듈] LSP 1.7K | JIT 2.3K | VM 1.4K | Package 1.7K | WASM 96 | Interp 7.7K

⚠ 모든 트러블슈팅 → config/hexa_ir_convergence.json
⚠ 모든 창발/n6 발견 → config/emergence_patterns.json
⚠ 변경 후 → singularity_cycle.py 또는 CycleEngine 재실행
```

## ⚠️ 필수 규칙

### hexa-native 전용 (sh/py/rs 작성 금지)
- **새 파일은 `.hexa`만 허용** — `.sh`, `.py`, `.rs` 등 다른 언어 파일 작성 금지
- 모든 새 모듈은 `mk2_hexa/native/` 에 `.hexa` 파일로 생성
- 기존 sh/py 스크립트는 참조만 허용, 신규 작성 불가
- **예외**: 이 리포의 컴파일러 Rust 소스(`src/`) — 컴파일러 자체 코드이므로 제외

## Build
```bash
bash build.sh
./hexa                        # REPL
./hexa examples/hello.hexa    # Run file (JIT→VM→Interp 자동)
./hexa -e "println(42)"      # Eval mode
./hexa --vm file.hexa         # VM 전용
./hexa --native file.hexa     # JIT 전용
bash build.sh test            # Tests
```

## ⚠️ Tiered Execution — 반드시 숙지
```
실행 순서: JIT(Cranelift→x86/ARM) → VM(바이트코드) → Interpreter(트리워킹)
  - 성공하면 즉시 return, 다음 tier 실행 안 함
  - JIT이 처리하면 VM/Interpreter 코드는 절대 실행 안 됨

[핵심 규칙]
  1. 런타임 기능 추가 시 → 3개 tier 모두 구현 or can_jit에서 제외
  2. 성능 측정 시 → 어느 tier에서 실행되는지 먼저 확인
     - 단순 코드(fn, if, for, call) → JIT이 잡음 (네이티브 머신코드)
     - @evolve, async, effect 등 → JIT 불가 → VM으로 fallback
  3. -e 모드도 동일한 tiered 실행 (JIT 우선)
  4. eprintln 디버그 찍었는데 안 보이면 → 해당 tier 안 탄 것

[파일 매핑]
  JIT:         src/jit.rs (Cranelift, can_jit로 게이트)
  VM:          src/vm.rs + src/compiler.rs (바이트코드)
  Interpreter: src/interpreter.rs (트리워킹, 가장 호환성 높음)
  라우터:      src/main.rs run_source_with_dir() (JIT→VM→Interp 순서)

[AI-native @attr 시스템]
  Token:       src/token.rs (Attribute 토큰 + AttrKind enum 13종)
  AST:         src/ast.rs (Attribute struct, FnDecl.attrs, StructDecl.attrs)
  Parser:      src/parser.rs (collect → pending_attrs → take_attrs)
  VM memo:     src/vm.rs (CallFrame.memo_key, fn_is_memoize, memo_cache)
  JIT gate:    src/jit.rs (can_jit_stmt에서 attr 체크)
  지원 attr:   @pure @inline @hot @cold @simd @parallel @bounded(N)
               @memoize @evolve @test @bench @deprecated @link
```

## Update History & Roadmap
```
⚠ 모든 주요 변경/최적화/돌파 → docs/history/ 에 기록
⚠ 형식: docs/history/YYYY-MM-DD-제목.md
⚠ docs/history/README.md 에 인덱스 추가
⚠ 성능 변경 시 → docs/breakthroughs/ 에도 BT 기록

★ 로드맵 자동 갱신 (docs/plans/roadmap.md)
  - 돌파/개선 완료 시 → Completed 섹션에 [x] 체크 + 날짜
  - 작업 중 발견한 다음 벡터 → Active 섹션에 [ ] 추가
  - 명시적 요청 없어도 자동으로 갱신할 것!

[최근]
  2026-04-06  VM 333x 성능 돌파 — Tiered Execution, Value 72→32B, 1791 tests
```

## n=6 Constants
n=6, σ=12, τ=4, φ=2, sopfr=5, J₂=24, μ=1, λ=2
Keywords: 53 (σ·τ+sopfr), Operators: 24 (J₂), Primitives: 8 (σ-τ)
Pipeline: 6 phases (n), Type layers: 4 (τ), Visibility: 4 (τ)
Memory: 1/2 Stack + 1/3 Heap + 1/6 Arena = 1

## Work Rules
```
<!-- SHARED:WORK_RULES:START -->
  ⛔⛔⛔ 이 블록은 삭제/수정/이동 금지! (sync-claude-rules.sh 자동 주입)
  ⛔ 규칙/인프라 원본: shared/ JSON 파일 참조. 절대 삭제하지 마세요!

  ═══════════════════════════════════════════════════════════════
  ★★★ 수렴 기반 운영 — 규칙 원본: shared/absolute_rules.json ★★★
  ═══════════════════════════════════════════════════════════════

  공통 규칙 (R1~R8):
    R1  HEXA-FIRST — .hexa만, sh/py/rs 신규 금지
    R2  하드코딩 절대 금지 — shared/*.jsonl 동적 로드
    R3  NEXUS-6 스캔 의무 — 변경 전후 스캔, 스캔 없이 커밋 금지
    R4  CDO 수렴 — 이슈→해결→규칙승격→재발0
    R5  SSOT — 데이터 원본 JSON 1개, 중복 금지
    R6  발견/결과 자동 기록 — 누락=소실=금지
    R7  sync 블록 삭제/수정/이동 금지
    R8  데이터 파일 로컬 보관 금지 — nexus/shared만 (nexus 제외)

  프로젝트별 규칙: shared/absolute_rules.json → projects 참조

  ═══════════════════════════════════════════════════════════════
  ★ 핵심 인프라 (shared/) ★
  ═══════════════════════════════════════════════════════════════

  코어 인덱스:     shared/core.json (시스템맵 + 명령어 14종 + 프로젝트 7개)
  보호 체계:       shared/core-lockdown.json (L0 22개 / L1 / L2)
  절대 규칙:       shared/absolute_rules.json (공통 R1~R8 + 프로젝트별 17개)
  수렴 추적:       shared/convergence/ (골화/안정/실패 — 7 프로젝트)
  할일 SSOT:       shared/todo/ (수동 + 돌파 엔진 자동)
  성장 루프:       shared/loop/ (nexus/anima/n6 자율 데몬)

  ═══════════════════════════════════════════════════════════════
  ★ NEXUS-6 (1022종 렌즈) — 상세: shared/CLAUDE.md ★
  ═══════════════════════════════════════════════════════════════

  CLI:  nexus scan <domain> | nexus scan --full | nexus verify <value>
  API:  nexus.scan_all() | nexus.analyze() | nexus.n6_check() | nexus.evolve()
  합의: 3+렌즈=후보 | 7+=고신뢰 | 12+=확정
  렌즈: shared/lens_registry.json (1022종)

  ═══════════════════════════════════════════════════════════════
  ★ 명령어 — 상세: shared/core.json → commands ★
  ═══════════════════════════════════════════════════════════════

  못박아줘    → L0 등록 (core-lockdown.json)
  todo/할일   → 돌파 엔진 할일 표 (todo.hexa)
  블로업/돌파 → 9-phase 특이점 (blowup.hexa)
  go          → 전체 TODO 백그라운드 병렬 발사
  설계/궁극의 → 외계인급 설계 파이프라인
  동기화      → 전 리포 sync (sync-all.sh)
<!-- SHARED:WORK_RULES:END -->
```

## 할일 (todo)
- "todo", "할일" → `hexa-bin-actual $HOME/Dev/nexus/mk2_hexa/native/todo.hexa hexa` 실행 후 **결과를 마크다운 텍스트로 직접 출력** (렌더링되는 표로)
