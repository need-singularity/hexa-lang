---
status: proposal
related-raw-rule: 16
related-roadmap: []
date: 2026-04-20
---

# `.loop` chain integration — 제안 패치 / proposal

`.loop` SSOT 에 chain-triggered loop entry 두 건을 추가하는 공식
제안. `.loop` 는 raw#16 으로 잠겨 있어 직접 편집 불가 — unlock
ceremony 를 거친 뒤 sed/manual edit 를 가하고 즉시 재잠금한다.

## 1. 배경 / Context

기존 `.loop` 는 loop#1 (daily fixpoint drift check), loop#2 (weekly
fixpoint archive) 두 건. 각각 `verify_fixpoint.hexa` /
`fixpoint_archive.hexa` 를 **직접** 호출한다. chain system 이 들어오면:

- release 와 daily-check 를 각각 **chain** 으로 한 번 선언해두고
- `.loop` 는 해당 chain 을 trigger 하는 얇은 래퍼가 된다.

이로써 (a) cron schedule 은 `.loop` 한 곳, (b) step 시퀀스는 `.chain`
한 곳으로 **완전히 분리** — raw#0 "SSOT 유일성" 원칙과 합치.

## 2. 제안 변경 / Proposed additions

`.loop` 끝(loop#2 뒤)에 빈 줄 + 아래 블록 추가:

```
loop 3 live "nightly release chain"
  interval  daily 02:00
  cmd       ./build/hexa_stage0 tool/chain_runner.hexa run release build
  why       매일 02:00 release chain 전체 완주 — archive 생성 + 알림. \
            chain "release" = build → verify → fixpoint → archive.
  proof     doc/runbook/raw_chain.md §5.1

loop 4 live "hourly convergence check"
  interval  hourly
  cmd       ./build/hexa_stage0 tool/chain_runner.hexa run daily-check raw_all
  why       시간당 drift 감지. chain "daily-check" = raw_all → fixpoint → audit. \
            기존 loop#1 (daily) 을 대체하지 않음 — hourly 는 추가.
  proof     doc/runbook/raw_chain.md §5.2
```

- `interval daily 02:00` / `interval hourly` 는 `.loop` 기존 문법
  (raw_loader `interval` 키 파싱).
- `cmd` 는 반드시 chain_runner 경유 — 직접 tool/*.hexa 를 부르지 않음.
- `why` 는 chain 의 의도 + 대체/추가 관계 명시.
- `proof` 는 `raw_chain.md` 해당 섹션 링크.

기존 loop#1 (daily fixpoint drift) 는 **남긴다** — chain daily-check
의 sanity fallback. 중복 실행은 idempotent (marker skip) 이므로 문제
없음.

## 3. 적용 절차 / Apply procedure (macOS, L1 기준)

```bash
# 0. 사전 확인 — chain_runner 존재 + .chain 에 release / daily-check 선언됨
test -x ./build/hexa_stage0 && \
test -f tool/chain_runner.hexa && \
grep -E '^chain (release|daily-check) ' .chain

# 1. unlock ceremony
./build/hexa_stage0 tool/hx_unlock.hexa --reason "loop#3 #4 chain integration"

# 2. 편집 — 위 블록을 .loop 끝에 append. 수작업 또는:
cat >> .loop <<'EOF'

loop 3 live "nightly release chain"
  interval  daily 02:00
  cmd       ./build/hexa_stage0 tool/chain_runner.hexa run release build
  why       매일 02:00 release chain 전체 완주 — archive 생성 + 알림. \
            chain "release" = build → verify → fixpoint → archive.
  proof     doc/runbook/raw_chain.md §5.1

loop 4 live "hourly convergence check"
  interval  hourly
  cmd       ./build/hexa_stage0 tool/chain_runner.hexa run daily-check raw_all
  why       시간당 drift 감지. chain "daily-check" = raw_all → fixpoint → audit. \
            기존 loop#1 (daily) 을 대체하지 않음 — hourly 는 추가.
  proof     doc/runbook/raw_chain.md §5.2
EOF

# 3. 문법 검증
./build/hexa_stage0 tool/raw_all.hexa --only raw 16

# 4. chain 실제 dry-run
./build/hexa_stage0 tool/chain_runner.hexa run daily-check raw_all

# 5. 재잠금
./build/hexa_stage0 tool/hx_lock.hexa

# 6. commit — `./hx commit` 이 attestation trailer 자동 부착
./hx commit -m "feat(.loop): add loop#3 #4 — chain-triggered nightly / hourly"
```

## 4. Rollback / 되돌리기

무언가 어긋나면:

```bash
./build/hexa_stage0 tool/hx_unlock.hexa --reason "rollback loop#3 #4"

# loop 3 / loop 4 블록 삭제 (sed 또는 수작업).
#   head -<lineno> .loop > .loop.new && mv .loop.new .loop

./build/hexa_stage0 tool/raw_all.hexa --only raw 16
./build/hexa_stage0 tool/hx_lock.hexa
./hx commit -m "revert(.loop): drop loop#3 #4 — chain integration aborted"
```

audit log (`raw_audit.hexa tail 20`) 에 unlock / edit / rollback
세 이벤트가 순서대로 남아야 함.

## 5. 미해결 / Open questions

- (Q1) hourly convergence 가 실제 drift 를 몇 번/일 발생시키는지 측정
  필요 — 과도하면 daily 로 회귀.
- (Q2) chain-runner 가 stdout 에 뿌리는 marker log 를 `.raw-audit` 으로
  자동 append 할지 결정 필요 (현재는 chain_reset 만 append).
- (Q3) `.loop` cron expr 와 chain 의 `step.X.timeout` 이 충돌하면 누가
  이기나 — 설계 고정 필요.

위 세 질문은 `doc/plans/chain_system_impl_summary_20260420.md` §5
follow-up 에 이미 기재.
