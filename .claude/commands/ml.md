ML 다음 레벨 로드맵을 관리한다.

## 실행 절차

1. `shared/hexa-lang/ml-next-level.json` 을 읽는다 (SSOT).
2. 인자($ARGUMENTS)에 따라 분기:

### 인자 없음 또는 "list"
- 전체 항목을 아래 형식 ASCII 표로 출력:
```
┌────┬─────────────────────┬──────────────────────────────────────┬────────────────────┬────────┐
│ #  │ 영역                │ 설명                                 │ 영향               │ 상태   │
├────┼─────────────────────┼──────────────────────────────────────┼────────────────────┼────────┤
│  1 │ Flash Attention     │ O(N²)→O(N) 메모리, tiling            │ 추론 2-4x          │ ✅ done│
│  2 │ PagedAttention      │ vLLM식 KV-cache 페이징               │ 동시 요청 10x+     │ todo   │
│ ...│                     │                                      │                    │        │
└────┴─────────────────────┴──────────────────────────────────────┴────────────────────┴────────┘
done: N / partial: N / todo: N  (총 N개)
```

### "go" (메인 명령어)
- status가 todo 또는 partial인 **모든** 항목을 수집
- **전부 병렬 백그라운드 Agent로 동시 발사** (run_in_background: true)
- 확인 질문 없이 즉시 실행
- 발사 테이블 출력 (항목별 에이전트 ID + 상태)
- 각 에이전트가 완료되면 JSON status/summary 자동 갱신
- 에이전트 프롬프트에 반드시 포함: 파일 경로, remaining 작업, 문법 주의사항, 구현 규칙, JSON 갱신 지시

### "next"
- "go"와 동일하되, 구현 계획을 먼저 제시하고 사용자 확인 후 시작

### 숫자 (예: "1", "5")
- 해당 id의 항목을 선택하여 구현 시작
- existing_files 가 있으면 먼저 읽고 확장
- 없으면 `self/ml/` 아래 새 파일 생성
- 완료 시 JSON의 status를 "done"으로, completed_at 기록, remaining을 null로 갱신

### "done 숫자" (예: "done 1")
- 해당 항목을 done 으로 마킹 (수동 완료 체크)
- JSON 갱신 + summary 카운트 재계산

### "add 이름 | 설명 | 영향"
- 새 항목을 items 배열 끝에 추가 (id = max+1)
- status: "todo", priority/difficulty/est_hours 자동 판단

### "sync"
- ml-next-level.json 의 현재 상태를 docs/plans/roadmap.md 의 해당 섹션에 반영
- CLAUDE.md ref 테이블에 ml-next-level.json 항목이 없으면 추가
- README.md 의 ML-NEXT-LEVEL 표도 갱신

## 구현 규칙

- 모든 구현은 순수 `.hexa` (self/ml/ 아래)
- Rust src/ 수정 금지
- 빌트인 추가 필요 시 interpreter.hexa + env.hexa + compiler.hexa 3파일 동시 등록 (HX-R-DEC-2)
- 테스트 파일 `self/test_{feature_name}.hexa` 생성 필수
- 완료 후 JSON status/summary 즉시 갱신
- docs/plans/roadmap.md 에 완료 항목 체크
