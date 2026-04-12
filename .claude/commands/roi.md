AI-native 개선 가능 항목을 ROI(impact/effort) 순으로 관리한다.

## ⛔ 표시 규칙 (절대 위반 금지)

1. **status="done" 항목은 절대 표시하지 않는다** — todo와 in_progress만 표시
2. **ROI(impact_x / est_hours) 내림차순 정렬**
3. **매 행 사이에 ├────┼── 가로 구분선을 반드시 넣는다**
4. 컬럼: # | 분류 | 항목 | 효과(x) | 시간(h) | ROI | 우선순위 | 상태
5. 하단에 done/in_progress/todo 카운트 표시

## 실행 절차

1. `shared/hexa-lang/roi.json` 을 읽는다 (SSOT).
2. 인자($ARGUMENTS)에 따라 분기:

### 인자 없음 또는 "list"
- JSON에서 status가 "todo" 또는 "in_progress"인 항목만 필터링
- ROI 내림차순으로 아래 형식 출력 (done 항목은 절대 포함하지 않는다):
```
┌────┬───────────────┬──────────────────────────────┬───────┬───────┬──────┬──────┬────────┐
│ #  │ 분류          │ 항목                         │ 효과x │ 시간h │ ROI  │ 우선 │ 상태   │
├────┼───────────────┼──────────────────────────────┼───────┼───────┼──────┼──────┼────────┤
│  1 │ attr_semantics│ @contract                    │  3.0  │   2   │ 1.50 │ P0   │ todo   │
├────┼───────────────┼──────────────────────────────┼───────┼───────┼──────┼──────┼────────┤
│  2 │ algo_rewrite  │ 선형탐색 → 이진탐색          │ 10.0  │   8   │ 1.25 │ P1   │ todo   │
├────┼───────────────┼──────────────────────────────┼───────┼───────┼──────┼──────┼────────┤
│ .. │ ...           │ ...                          │  ...  │  ...  │ ...  │ ...  │ ...    │
└────┴───────────────┴──────────────────────────────┴───────┴───────┴──────┴──────┴────────┘
done: 0 / in_progress: 0 / todo: 13  (총 13개, 남은 예상: 127h)
```

### "go" (메인 명령어)
- status=todo|in_progress 전부 ROI 순으로 병렬 bg Agent 발사 (run_in_background: true 필수)
- 확인 질문 없이 즉시 실행
- 포그라운드 Agent 금지

### "next"
- ROI 최상위 항목 1개 계획 → 확인 → 구현

### 숫자 (예: "3")
- 해당 id 항목 구현 시작
- target_files 읽고 기존 코드에 추가

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
- ai-native.json의 stub_registered 항목과 동기화 유지
- 명령어 정의: `shared/hexa-lang/roi-commands.json`
