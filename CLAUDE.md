# hexa-lang — HEXA 프로그래밍 언어

## 명령어 (`shared/config/commands.json`)

| 명령 | 동작 |
|------|------|
| `todo` / `할일` | 전 프로젝트 할일 표 |
| `go` / `가자` | 모든 TODO 병렬 발사 |
| `smash` / `부셔` | 블로업 돌파 엔진 |
| `free` / `dfs` | 자율 조립 + DFS 탐색 |
| `ml` / `ml go` | ML 로드맵 (hexa-lang 전용) |
| `list` / `목록` | 이 명령어 표 다시 출력 |

> shared/ JSON 단일진실 (R14). 규칙: `shared/rules/common.json` (R0~R27)

## ⛔ 규칙 (위반 시 즉시 중단)

### 공통 (`shared/rules/common.json`)
- **R1** HEXA-FIRST — 신규 코드 .hexa로만 작성
- **R2** 하드코딩 금지 — shared/*.jsonl에서 동적 로드
- **R14** shared/ JSON 단일진실 — SSOT 1개만 존재
- **AI-NATIVE** bit-twiddling/manual-loop 금지, @attr 우선, O(n²)→해시맵

### hexa-lang 전용 (`shared/rules/hexa-lang.json`)
- **HX3** pitfalls 체크 — .hexa 작성 전 `shared/hexa-lang/grammar.jsonl` 참조
- **HX4** SELF-HOST FIRST — src/ Rust 폐기, 모든 코드 .hexa
- **HX5** AI-native 알고리즘 교체 의무 — `docs/ai-native.md` 24종 벡터
- **HX6** 돌파 시 nexus blowup 연동 + growth_bus 기록

전체 규칙: `shared/rules/common.json` (R0~R27) + `shared/rules/hexa-lang.json` (HX1~HX6)

## ref

| 항목 | 파일 | 내용 |
|------|------|------|
| 공통 규칙 | `shared/rules/common.json` | R0~R27 |
| 프로젝트 규칙 | `shared/rules/hexa-lang.json` | HX1~HX6 |
| 보호 체계 | `shared/rules/lockdown.json` | L0/L1/L2 (src/ L0) |
| CDO 수렴 | `shared/rules/convergence_ops.json` | 수렴 운영 원칙 |
| 프로젝트 레지스트리 | `shared/config/projects.json` | 7개 프로젝트 |
| 프로젝트 설정 | `shared/config/project_config.json` | CLI/빌드/DSE |
| 시스템 코어 | `shared/config/core.json` | 시스템맵 + 14명령 |
| 수렴 상태 | `shared/hexa-lang/state.json` | CDO + breakthroughs |
| 로드맵 | `shared/roadmaps/anima_hexa_common.json` | anima x hexa P0~P5 |
| 문법 | `shared/hexa-lang/grammar.jsonl` | 전체 문법 + pitfalls |
| AI-native | `docs/ai-native.md` | 24종 벡터 로드맵 |
| ML 로드맵 | `shared/hexa-lang/ml-next-level.json` | 15+N 다음 레벨 |
| ML 명령어 | `shared/hexa-lang/ml-commands.json` | ml/ml go/ml next 등 |
