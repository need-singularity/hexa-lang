# hexa-lang — HEXA 프로그래밍 언어

> shared/ JSON 단일진실 (R14). 규칙: `shared/rules/common.json` (R0~R27)

## ⛔ 규칙 준수 (필수)

작업 시작 전 `shared/rules/common.json` + `shared/rules/hexa-lang.json` 을 읽고 전 규칙 준수. 위반 시 즉시 수정.

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

## ml 명령어

사용자가 `ml`, `ml go`, `ml next`, `ml 5`, `ml done 1`, `ml add ...`, `ml sync` 입력 시 `.claude/commands/ml.md` 의 절차를 따른다.

- `ml` → 전체 목록 ASCII 표
- `ml go` → todo/partial **전부** 병렬 bg Agent 발사 (확인 없이)
- `ml next` → 최우선 항목 계획 → 확인 → 구현
- `ml 숫자` → 해당 id 항목 구현
- `ml done 숫자` → 완료 마킹 + JSON 갱신
- `ml add 이름 | 설명 | 영향` → 새 항목 추가
- `ml sync` → roadmap.md + README.md 동기화
