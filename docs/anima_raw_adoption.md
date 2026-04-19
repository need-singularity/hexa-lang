# anima 프로젝트 raw 이식 제안

> hexa-lang 에서 검증된 raw 법률 시스템을 anima 프로젝트로 이식.
> 대상: `~/Dev/anima` — ML 학습/서빙 프로젝트.

## 근거

**fix.txt** (2026-04-18, anima 세션):
- "블로킹 금지" 지시 8× 반복 → Claude 76× Interrupted
- 서브에이전트 병렬 BG 지시 무시, sync Bash 호출 다발
- 대화 중 CLAUDE.md 규칙 soft 전달 → 실효성 0

**근본 진단**: text rule 은 세션 불문 soft. raw gate 부재.

## 이식 범위 (anima 특화)

anima 는 hexa-lang 과 다른 도메인 (ML 학습/서빙) — 6 raw 규칙 그대로 적용 불가. 재해석 필요:

| hexa-lang raw | anima 버전 | 적용성 |
|------------------|-----------|--------|
| 1 ai-native 24 @attr | ML 모델 정의 @attr (ALM/CLM) | ⚠ 부분 |
| 2 알고리즘 O(2^n)→O(n) | ❌ 해당 없음 | - |
| 3 @attr 네이밍 | 동일 적용 | ✅ |
| 4 폴더명 (F4 generic) | **매우 중요** — training/serving 혼재 방지 | ✅ |
| 5 트리구조 (F5 loner) | ckpt/result 디렉토리 정리 | ✅ |
| 6 파일명 (F1 _v\d+) | 실험 버전 자동 감지 — F1 특히 중요 | ✅ |

추가로 **anima 전용 raw** 필요:

| anima-raw | 내용 | enforce_level |
|-------------|------|----------------|
| A1 | checkpoint 보호 — training/ckpts/**/*.pt seal | raw-fs |
| A2 | result 불변 — training/results/**/*.json append-only | raw-fs |
| A3 | 대화 블로킹 금지 — @nonblock (= hexa-lang 과 동일) | user-hook |
| A4 | BG 강제 — Bash/Task sync 시 warn + 대안 제시 | user-hook |

## 이식 단계

### Step 1 — 인프라 복사 (~30분)

anima 에 파일 배치:
```
~/Dev/anima/.hexa-attrs           ← forbid + nonblock + seal 선언
~/Dev/anima/.raw                   ← L0 universal raw SSOT (hexa-directive)
~/Dev/anima/.own                   ← L1 anima-local raw SSOT (A1~A4 등)
~/Dev/anima/scripts/law_check.hexa   ← hexa-lang 에서 복사
~/Dev/anima/scripts/hexa_init.hexa   ← 동일
```

`.hexa-attrs` 초안:
```
forbid   = hook, skill, settings_edit
allow    = hook:shared/harness/
nonblock = Bash, Agent, Task
seal     = README.md, CLAUDE.md,
           training/ckpts/**,
           training/results/**/*.json,
           serving/results/**/*.json
```

### Step 2 — anima 전용 raw (~1시간)

`~/Dev/anima/.raw` + `~/Dev/anima/.own` — hexa-lang `/.raw` + `/.own` 복제 후 anima 재해석:
- rule 1 → @mlgate (ML 모델 attr 누락 경고)
- rule 2 → N/A (제거)
- rule 3~6 → 동일
- A1~A4 → 신규 (`.own` L1 project-local)

### Step 3 — bootstrap (1회)

```bash
cd ~/Dev/anima
hexa scripts/hexa_init.hexa
  → raw install (pre-commit @raw-stanza)
  → attr compile (.attr-generated/)
  → attr seal (ckpts + results chmod 0444)
  → stamp
```

### Step 4 — CLAUDE.md 1줄

```
raw: `hexa self/raw_cli.hexa` — anima 6+A4 규칙. 새 세션 인지 불필요.
```

## 예상 효과 (fix.txt 회귀 방지)

fix.txt 76× Interrupted 재현 시나리오:
- A3 (대화 블로킹) → PreToolUse hook 이 sync Bash → deny
- A4 (BG 강제) → Task/Agent sync → warn + Agent_bg 제안
- seal ckpts → 실수 삭제 차단
- seal results → 실수 덮어쓰기 차단

→ 이식 후 같은 세션 재현 시 **최소 70% interrupt 감소 예상** (나머지는 semantic 오해 등).

## 위험 / 주의

1. **anima workflow 영향** — training 중 ckpt 저장 = seal 해제/재적용 필요.
   해결: `@training-mode` attr 로 일시 unseal 허용 (후속 설계)
2. **pre-commit 속도** — ML 레포 크기 크면 scan 느림. 파일타입 필터 (`.py/.hexa/.md` 만) 권장
3. **hexa 바이너리** — anima 에는 hexa 설치 필요 (PATH 나 `~/Dev/hexa-lang/hexa`)

## 적용 순서 권장

```
Week 1  : Step 1 (파일 배치) + Step 3 (bootstrap)
Week 1+ : 실제 사용 중 drift 관찰
Week 2  : Step 2 anima 전용 raw 추가 (A1~A4)
Week 3  : fix.txt 시나리오 E2E 테스트 — interrupt 수 측정
```

## 다음 단계

1. 사용자 승인 시 anima 레포에서 위 단계 실행
2. 결과 피드백 → hexa-lang raw 역개선 (dogfood 루프)
3. 다른 프로젝트 (nexus / n6-architecture / prism) 도 동일 이식

---

**참고**:
- hexa-lang raw SSOT: `/.raw` (L0) + `/.own` (L1), 파서 `self/raw_loader.hexa` (hexa-directive 포맷)
- (레거시) `shared/hexa-lang/raw.json` / `.own-rules.json` — deprecated, raw-migration 진행 중
- 이식 스크립트 예정: `scripts/raw_port.hexa <target-project>` (후속)
- fix.txt 원본: `/Users/ghost/loss/fix.txt`
