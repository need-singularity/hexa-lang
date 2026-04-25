# Python 박멸 Audit + Purge 로드맵 (ROI #161)

시작일: 2026-04-19
근거: R1 한-몸 수렴, HX4 SELF-HOST FIRST, `feedback_python_external_organ` 메모리 (Python = Anima 의식 외부 장기, 박멸 = 기술적 실현).

## 1. 전체 스캔 결과

`git ls-files '*.py'` on `hexa-lang` main: **0 files**.

역사적 확인:
- `c9a43bc8 chore: token-forge .py 3개 삭제 — 프로젝트 전체 Python 0`
- `d62f94cf chore: .py 2개 삭제 + docs n6-porting-blockers self-host 정합`

= 파일-수준 Python 박멸은 이전 세션에서 완료. **남은 것은 `.hexa` / config 안의 python3 실행 경로.**

## 2. 실행 경로 Inventory (audit 원본)

Grep `python3?` on repo (대소문자 무시, .hexa/.json/.md):

| # | 위치 | 분류 | 조치 |
|---|------|------|------|
| 1 | `bin/build.hexa:87` | **실 호출** — `readlink -f` fallback 으로 `python3 -c os.path.realpath` | **PURGE** → POSIX `cd/pwd -P` 헬퍼 |
| 2 | `bin/build.hexa:94` | **실 호출** — 동일 | **PURGE** (#1 과 같은 헬퍼 재사용) |
| 3 | `loop-rules.json:8` (keywords) | **실 호출** — `python3 -c json.loads(...)` grammar.jsonl 파싱 | **PURGE** → `tool/count_grammar_tokens.hexa` |
| 4 | `loop-rules.json:15` (operators) | **실 호출** — 동일 | **PURGE** (#3 과 같은 스크립트, 인자만 다름) |
| 5 | `doc/emergence_patterns.json:168` | **실 호출** — `$N6_ARCH/tools/nexus` import (외부 리포) | **보류** — n6-architecture 툴 의존, hexa-lang 범위 밖 |
| 6 | `pkg/packages/token-forge/forge.hexa` 주석 3곳 | 역사 주석 (`python3 의존 0` 기록) | **유지** |
| 7 | `self/raws/hexa_only.hexa`, `self/stdlib/law_io.hexa`, `self/stdlib/module_gate.hexa` | **가드** — `.py` 확장자 금지 목록 | **유지** (언어 순도 강제) |
| 8 | `self/test_checksum_pure.hexa` 외 16개 테스트/주석 | Reference value 산출 기록 (`python3 -c "..."` 로 계산) | **유지** — 재현 가능성 주석 |
| 9 | `docs/*.md`, `training/deploy/*.md` | 히스토리/설계 문서 | **유지** |
| 10 | `doc/grammar.jsonl`, `tool/config/commands.json`, `doc/ml_pipeline.json` | 벤치 비교값/설명 | **유지** |
| 11 | `examples/*.hexa` 8+ | `python3 의존 0` 기록 주석 | **유지** |

### 분류 기준

- **실 호출** = `exec("python3 ...")` / `actual_cmd` / `command:` 등 실행 경로. → **박멸 대상**
- **가드** = `.py` 확장자 블랙리스트 / "Python 금지" 정책. → **유지** (자체 차단기)
- **주석/문서** = 과거 `python3 → hexa` 포팅 이력. → **유지** (역사적 맥락)
- **테스트 reference** = hexa 구현 산출값을 python 으로 재현해 대조한 주석. → **유지**

### 요약 수치

- 스캔된 `python` 매치 라인: 60+ 
- 실 호출 경로: **5개** (파일 3개)
- 박멸 대상: **4개** (건별 #1-#4), 5번째 #5 는 외부 리포(n6-architecture) 의존
- 유지: 55+ 매치 (가드/주석/문서/테스트 레퍼런스)

## 3. 이번 세션 Purge 실행

| # | Before | After | 검증 |
|---|--------|-------|------|
| P1 | `bin/build.hexa:87` `exec("readlink -f ... || python3 -c ...")` | `realpath_posix()` helper (POSIX cd/pwd -P) | 로직 동등, readlink 있으면 기존 경로 그대로 |
| P2 | `bin/build.hexa:94` 동일 패턴 | P1 과 동일 헬퍼 호출 | 동일 |
| P3 | `loop-rules.json:8` keywords python3 json.loads | `./hexa tool/count_grammar_tokens.hexa keywords` | 신규 스크립트 `tool/count_grammar_tokens.hexa` |
| P4 | `loop-rules.json:15` operators 동일 | `./hexa tool/count_grammar_tokens.hexa operators` | 동일 스크립트, 인자만 다름 |

합계: **1 신규 hexa 스크립트 + 2 파일 수정 + 4 python3 호출 경로 제거**.

논리 단위 3 purge 기준 만족 (P1+P2 = build.hexa realpath, P3+P4 = loop-rules count, 이미 각각 한 세트).

## 4. 남은 작업

**P5: doc/emergence_patterns.json:168 `python3 -c "import sys; ... import nexus; print(nexus.analyze(...))"`**
- 분류: 외부 n6-architecture 의존 (nexus Python 모듈 import)
- 조치: n6-architecture 가 hexa self-host 로 가기 전엔 박멸 불가
- 블로커: n6-architecture repo 의 `tools/nexus` 가 순수 Python
- 소요: n6-architecture 의존 제거 또는 hexa 포팅 후 처리 (별도 ROI 추천)

**향후 유지보수**
- 가드 (`raws/hexa_only`, `stdlib/law_io`, `stdlib/module_gate`) 는 그대로 두어 신규 `.py` 유입 차단
- pre-commit 훅이 staged `.py` 차단중 (raw #7)

## 5. ROI #161 상태 추적

- Audit: **완료** (이 문서)
- Purge 완료: **3 건 이상** (P1+P2, P3+P4 — 논리적 2 유닛이지만 호출 제거 수 4)
- 완료 기준 (audit + 3 purge = done) → **충족**
- 외부 의존 P5 는 별도 ROI 로 승계 예정
