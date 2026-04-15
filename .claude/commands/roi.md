AI-native 개선 가능 항목을 ROI(impact/effort) 순으로 관리한다.

## ⛔ 표시 규칙 (절대 위반 금지)

1. **status="done" 및 "running" 항목은 절대 표시하지 않는다** — todo만 표시. done/running은 카운트에만 포함
2. **ROI(impact_x / est_hours) 내림차순 정렬**
3. **매 행 사이에 ├────┼── 가로 구분선을 반드시 넣는다**
4. **표시할 항목이 20개 미만이면 새 항목을 제안하여 20개까지 같은 표 안에 채운다** — 상태 컬럼에 "제안" 표시
5. 컬럼: # | 분류 | 항목 | 효과(x) | 시간(h) | ROI | 우선 | 상태
6. 하단에 done/running/todo 카운트 표시

## 실행 절차

1. `shared/hexa-lang/roi.json` 을 읽는다 (SSOT).
2. 인자($ARGUMENTS)에 따라 분기:

### 인자 없음 또는 "list"
- status=todo만 필터링, ROI 내림차순 출력

### "go"
- status=todo 전부 병렬 bg Agent 발사 (run_in_background: true 필수)
- 발사 시 status=todo→running 전환
- 확인 질문 없이 즉시 실행, 포그라운드 Agent 금지

### "next"
- ROI 최상위 항목 1개 계획 → 확인 → 구현

### 숫자 (예: "3")
- 해당 id 항목 구현 시작. target_files 읽고 기존 코드에 추가

### "done 숫자" (예: "done 1")
- 해당 항목 done 마킹 + JSON summary 재계산

### "add 이름 | 설명 | 효과x | 시간h"
- items 끝에 새 항목 추가 (id=max+1, roi=자동계산, status=todo)

### "scan"
- `self/ai_native_pass.hexa` + `shared/hexa-lang/ai-native.json` 스캔
- 새 개선 기회를 roi.json에 자동 추가 제안

## 구현 규칙

- 순수 `.hexa` (self/ 아래), Rust 수정 금지
- 테스트 `self/test_{feature}.hexa` 필수
- 완료 후 roi.json status/summary 즉시 갱신
