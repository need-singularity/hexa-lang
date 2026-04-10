# hexa-lang — HEXA 프로그래밍 언어

> shared/ JSON 단일진실 (R14). 규칙: `shared/rules/common.json` (R0~R27)

## 돌파 시스템

- "XXX 돌파" → `blowup.hexa "XXX" 3` 자동 실행 (문장 전달)
- 확인 질문 없이 즉시 Bash 실행

```sh
hexa shared/blowup/core/blowup.hexa <domain> 3 --seeds "$(hexa shared/blowup/seed/seed_engine.hexa merge | tr '|' ',')"
```

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
