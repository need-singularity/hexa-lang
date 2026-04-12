# hexa-lang — HEXA 프로그래밍 언어

commands: shared/config/commands.json — autonomous 블록으로 Claude Code가 작업 중 smash/free/todo/go/keep 자율 판단·실행. ml 명령어: shared/hexa-lang/ml-commands.json (hexa-lang 전용)
rules: shared/rules/common.json (R0~R27) + shared/rules/hexa-lang.json (HX1~HX7)
L0 Guard: `hexa ~/Dev/nexus/shared/lockdown/l0_guard.hexa <verify|sync|merge|status>`

hexa-lang 핵심 규칙:
  HX3: pitfalls 체크 — .hexa 작성 전 shared/hexa-lang/grammar.jsonl 참조
  HX4: SELF-HOST FIRST — src/ Rust 폐기, 모든 코드 .hexa
  HX5: AI-native 알고리즘 교체 의무 — docs/ai-native.md 24종 벡터
  HX6: 돌파 시 nexus blowup 연동 + growth_bus 기록
  HX7: SELF-HOST 경로 전용 — self/codegen_c2.hexa + self/runtime.c + self/interpreter.hexa + self/native/hexa_v2, 프리젠 C는 .hexanoport 마커로만

ref:
  rules     shared/rules/common.json             R0~R27
  project   shared/rules/hexa-lang.json          HX1~HX7
  lock      shared/rules/lockdown.json           L0/L1/L2
  cdo       shared/rules/convergence_ops.json    CDO 수렴
  registry  shared/config/projects.json          7프로젝트
  cfg       shared/config/project_config.json    CLI/빌드/DSE
  core      shared/config/core.json              시스템맵+14명령
  conv      shared/hexa-lang/state.json          CDO+breakthroughs
  roadmap   shared/roadmaps/anima_hexa_common.json  P0~P5
  grammar   shared/hexa-lang/grammar.jsonl       전체 문법+pitfalls
  ai-native docs/ai-native.md                    24종 벡터
  ml        shared/hexa-lang/ml-next-level.json  15+N 다음 레벨
  ml-cmd    shared/hexa-lang/ml-commands.json    ml/ml go/ml next
  api       shared/CLAUDE.md
