## ⛔ L0 CORE 보호 파일 (AI 수정 승인 필수)

> 아래 파일은 수렴 완료된 코어 로직. 수정 시 반드시 유저에게 승인 질문.
> 상세: `nexus/shared/core-lockdown.json`

```
🔴 L0 (불변식 — 코드 수정 전 유저 명시 승인 필수):
  src/interpreter.rs    — 인터프리터 코어 12,164줄
  src/parser.rs         — 파서 문법 정의
  src/lexer.rs          — 렉서 토큰화
  src/ownership.rs      — 소유권 시스템
  src/ir/               — HEXA-IR self-hosting 코어

🟡 L1 (보호 — 리뷰 필요):
  src/escape_analysis.rs — 이스케이프 분석
  src/codegen.rs         — 코드 생성
  tests/                 — 테스트 스위트
```

> 🔴 **AI-NATIVE FIRST**: 성능/자원 개선은 저수준 마이크로 최적화 금지 — **AI-native 알고리즘 교체로 돌파**만 추구. docs/ai-native.md 24종 벡터 로드맵 참조. @optimize/@memoize/@fuse/@parallel 등 attr 시스템으로 구현.

> 🔴 **HEXA-FIRST**: 모든 코드는 `.hexa`로 작성. 부하 유발 명령 최소화.
> - **`src/` Rust 인터프리터/컴파일러는 폐기됨** — 수정/빌드/테스트 금지. 검증·실험이 필요하면 `~/Dev/test`에서 `.hexa`로 진행.

> 🔴 **NEXUS-6 특이점 연동**: 이 프로젝트의 돌파/발견/실험은 nexus 특이점 사이클 입력이다.
> - **돌파 시**: `HEXA=$HOME/Dev/hexa-lang/target/release/hexa && $HEXA $HOME/Dev/nexus/mk2_hexa/native/blowup.hexa <domain> 3 --no-graph`
> - **발견 기록**: `$HOME/Dev/nexus/shared/growth_bus.jsonl`에 JSON append
> - **전체 상태**: `$HEXA $HOME/Dev/nexus/mk2_hexa/native/command_router.hexa "hexa-lang 상태"`

```
<!-- SHARED:WORK_RULES:START -->
  ⛔⛔⛔ 이 블록은 삭제/수정/이동 금지! (sync-claude-rules.sh 자동 주입)
  ⛔ 규칙/인프라 원본: shared/ JSON 파일 참조. 절대 삭제하지 마세요!

  ═══════════════════════════════════════════════════════════════
  ★★★ 수렴 기반 운영 — 규칙 원본: shared/absolute_rules.json ★★★
  ═══════════════════════════════════════════════════════════════

  공통 규칙 (R1~R8):
    R1  HEXA-FIRST — .hexa만
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

# HEXA-LANG

> 참조: `shared/absolute_rules.json` → HX1~HX3 | `shared/convergence/hexa-lang.json` | `shared/todo/hexa-lang.json` | `shared/core.json`

## DSE 수렴
21,952 조합 전수 탐색 완료 — 100% n=6 EXACT. 문법/타입/메모리 전 레벨 수렴.

## 문법 요약
53 keywords (sigma*tau+sopfr) | 24 operators (J2) | 8 primitives (sigma-tau)

## IR 현황
self-hosting Phase1 완료 | LLVM 0% (의존 없음) | 1791 tests | 53.8K LOC | bench 333x

## 실행 모델
**`src/` Rust 구현(JIT/VM/Interp) 폐기됨** — 더 이상 수정/빌드/참조 대상 아님.
검증·실험은 `~/Dev/test`에서 `.hexa`로 진행.

## 빌드
```bash
bash build.sh              # 빌드
./hexa examples/hello.hexa # 실행
bash build.sh test         # 테스트
```

## 할일 (todo)
- "todo", "할일" → `hexa-bin-actual $HOME/Dev/nexus/mk2_hexa/native/todo.hexa hexa` 실행 후 **결과를 마크다운 텍스트로 직접 출력** (렌더링되는 표로)
