---
status: draft
related-raw-rule: 0
related-roadmap: []
date: 2026-04-20
---

# raw Chain System Runbook

`.chain` SSOT 와 `tool/chain_runner.hexa` 로 구동하는 **forward-chain
automation** 의 공식 runbook. 수 개의 step 을 선언적으로 엮어 매번
동일한 순서 · 동일한 guard · 동일한 paper trail 로 실행하게 만든다.

Bilingual (Korean primary, English inline) — matches `doc/runbook/raw_*.md`
convention.

## 1. Overview — 왜 chain 인가 / What this gives you

전통적 Makefile / shell script 는 "어디까지 갔는지" 를 파일시스템에
남기지 않는다. 그래서:

- **이어달리기 불가**: `release` 중간에 죽으면 처음부터 다시.
- **분기 실행 불가**: 같은 script 를 중간 지점부터 재개하려면 사람이
  state 를 기억해야 함.
- **Guard 없음**: 순서를 건너뛰어도 경고 없이 진행 → drift.

`.chain` + `chain_runner` 는 이를 **OS-level marker file** 로 해결한다:

- 각 step 은 완료 시 `.chain-state/<chain>/step_<id>_ok` 파일을 touch.
- Runner 는 step 진입 전 "prev step marker 가 존재하는가?" 를 검사.
- strict mode 는 prev marker 부재 시 **즉시 거부** (exit != 0).
- loose mode 는 허용 — mid-entry 가능.
- 한 번 완료된 step 은 idempotent — marker 있으면 cmd 재실행 skip.

핵심 철학: raw#0 의 "OS-level enforcement > shell hook" 과 동일.
`.chain-state/` marker 는 파일시스템이 기억하는 **진짜 상태**.

## 2. 개념 / Concepts

### 2.1 Step 선언

`.chain` 문법 (flat key 방식 — 파서가 간단):

```
chain <id> <status> "<name>"
  mode strict|loose
  step.<step_id>.cmd       <shell>
  step.<step_id>.next      <next_step_id>
  step.<step_id>.why       <오늘의 한 줄>
```

- `status`: `new live warn deferred convention` (`.raw` 와 동일 4-level
  상태 — raw_loader 가 재사용).
- `mode strict`: prev marker 없으면 해당 entry 거부 (default 권장).
- `mode loose`: mid-entry 허용 — debug / re-run 시.
- `step.X.next`: `Y` 하나면 단순 forward. 생략 시 terminal step.
- (Phase 6) `step.X.parallel`, `step.X.cond` 는 미구현.

### 2.2 Forward trigger + entry 자유도

- Runner 호출: `./hx chain run <chain_id> <entry_step>`.
- entry step 부터 **forward 방향** 으로만 진행. 뒤로 돌아가지 않는다.
- strict mode 에서 mid-entry 는 prev marker 확인 → 없으면 BLOCKED.
- loose mode 에서 mid-entry 는 prev 검사 생략 — 필요하면 `--reason` 권장.

### 2.3 OS-level guard

- marker 파일: `.chain-state/<chain_id>/step_<step_id>_ok`
- size 0, mtime = 완료 시각.
- macOS: `chflags uchg` 로 잠가 실수 제거 방지 (`hx chain lock`).
- Linux: `chattr +i` fallback (root 필요).
- Reset 은 전용 ceremony (§7).

## 3. CLI

`./hx` 가 wrapper. 아래 명령은 repo 루트에서 실행.

| 명령                                          | 의미                                           |
| :-------------------------------------------- | :--------------------------------------------- |
| `./hx chain list`                             | `.chain` 에 선언된 chain 전체 목록 + status    |
| `./hx chain status <chain>`                   | 각 step 의 marker 존재 여부 (완료/대기/진행)   |
| `./hx chain run <chain> <entry>`              | entry 부터 forward 실행                         |
| `./hx chain run <chain> <entry> --loose`      | loose mode one-shot 강제                        |
| `./hx chain reset <chain> [--from <step>]`    | marker 삭제 + audit append (§7 ceremony)       |
| `./hx chain doctor`                           | `.chain` 문법 + 미정의 next 참조 검사          |

`./hx` 없이 직접 호출:

```
./build/hexa_stage0 tool/chain_runner.hexa run <chain> <entry>
./build/hexa_stage0 tool/chain_runner.hexa status <chain>
```

## 4. strict vs loose mode

| 항목              | strict                         | loose                          |
| :---------------- | :----------------------------- | :----------------------------- |
| mid-entry         | prev marker 없으면 거부 (BLOCKED) | 항상 허용                    |
| 의도              | release / ceremony / 돌아갈 수 없는 작업 | debug / 부분 재실행          |
| audit             | BLOCKED 도 `.raw-audit` 에 기록 | loose 진입은 `--reason` 권장  |
| default           | 권장 — 모든 production chain    | 명시적 `mode loose` 만       |

**권장 정책**: chain 은 항상 strict 로 선언. debug 는 `--loose`
one-shot 플래그로만.

## 5. Use cases — 다섯 가지 representative chain

### 5.1 release chain

```
chain release live "nightly release"
  mode strict
  step.build.cmd       ./build/hexa_stage0 tool/build_stage0.hexa
  step.build.next      verify
  step.verify.cmd      ./build/hexa_stage0 tool/raw_all.hexa --fast
  step.verify.next     fixpoint
  step.fixpoint.cmd    ./build/hexa_stage0 tool/verify_fixpoint.hexa --quick
  step.fixpoint.next   archive
  step.archive.cmd     ./build/hexa_stage0 tool/fixpoint_archive.hexa snapshot
```

- `.loop` 에서 매일 02:00 에 `./hx chain run release build` 호출.
- 중간 실패 시 다음 날 runner 는 남은 marker 를 감지해 실패한 step
  부터 재개 — idempotent step 은 skip.

### 5.2 daily-check chain

```
chain daily-check live "hourly drift detector"
  mode strict
  step.raw_all.cmd     ./build/hexa_stage0 tool/raw_all.hexa --fast
  step.raw_all.next    fixpoint
  step.fixpoint.cmd    ./build/hexa_stage0 tool/verify_fixpoint.hexa --quick
  step.fixpoint.next   audit
  step.audit.cmd       ./build/hexa_stage0 tool/raw_audit.hexa tail 10
```

- `.loop 4 live` 에서 시간당 호출 (`loop_chain_integration_20260420.md`
  참조).
- drift 감지 → runner 실패 → audit log 자동 기록.

### 5.3 unlock-ceremony chain

```
chain unlock new "unlock ceremony"
  mode strict
  step.raw_all.cmd     ./build/hexa_stage0 tool/raw_all.hexa
  step.raw_all.next    backup
  step.backup.cmd      ./build/hexa_stage0 tool/raw_backup.hexa snapshot
  step.backup.next     unlock
  step.unlock.cmd      ./build/hexa_stage0 tool/hx_unlock.hexa --reason "$REASON"
  step.unlock.next     timer
  step.timer.cmd       ./build/hexa_stage0 tool/hx_unlock_timer.hexa arm 15m
```

- 대화형 ceremony 를 고정 순서로 강제.
- `$REASON` 은 runner 가 env 로 전달.

### 5.4 rebootstrap chain

```
chain rebootstrap new "self-host rebootstrap"
  mode strict
  step.design.cmd      test -f doc/plans/rebootstrap_*.md
  step.design.next     regression
  step.regression.cmd  ./build/hexa_stage0 test/regression/raw_all_self.hexa
  step.regression.next fixpoint
  step.fixpoint.cmd    ./build/hexa_stage0 tool/verify_fixpoint.hexa --quick
  step.fixpoint.next   hash
  step.hash.cmd        ./build/hexa_stage0 tool/fixpoint_archive.hexa snapshot
  step.hash.next       tag
  step.tag.cmd         git tag -s "rebootstrap-$(date +%Y%m%d)"
```

- `doc/runbook/raw_rebootstrap.md` 의 절차를 자동 묶음.

### 5.5 emergency chain

```
chain emergency new "emergency raw reset"
  mode loose
  step.reset.cmd       bash tool/emergency/raw_reset.sh
  step.reset.next      verify
  step.verify.cmd      ./build/hexa_stage0 tool/raw_all.hexa --fast
  step.verify.next     commit
  step.commit.cmd      ./hx commit -m "emergency raw reset"
  step.commit.next     notify
  step.notify.cmd      ./build/hexa_stage0 tool/raw_audit.hexa append "emergency reset done"
```

- loose mode: 긴급 상황에서 mid-entry 를 허용해야 하므로.
- `raw_emergency.md` 와 짝.

## 6. `.chain` 포맷 규칙

1. 한 파일 안에 여러 chain 을 선언할 수 있음 (blank line 구분).
2. 키는 `.` 세 단계: `step.<id>.<prop>` 만 허용 (parser 단순화).
3. `<prop>` 허용 목록 (Phase 5 기준): `cmd`, `next`, `why`, `timeout`,
   `retry`.
4. 선언되지 않은 step 을 `next` 로 참조하면 `./hx chain doctor` 가 fail.
5. Cycle 금지 (forward 전용). doctor 가 DFS 로 검사.
6. Comment `#` 는 줄 시작에서만 (중간 금지 — raw_loader 와 동일).

## 7. Reset ceremony — marker 폐기 절차

`.chain-state/` 는 함부로 `rm -rf` 하면 안 된다:

1. `chflags uchg` / `chattr +i` 로 잠겨 있을 수 있음 (EPERM).
2. audit 기록이 빠져 drift 로 오인됨.

공식 절차:

```bash
# 1. 이유와 함께 reset 호출
./hx chain reset release --from build --reason "v1.1 rebuild"

# → runner 가 내부적으로:
#    a) chflags nouchg '.chain-state/release/step_build_ok' …
#    b) rm 해당 marker + forward 전부
#    c) raw_audit.hexa append "chain-reset | release | build+ | <reason>"
```

`--from` 없으면 전체 chain reset. `--from <step>` 지정 시 해당 step
이후 forward marker 만 삭제 (앞 marker 는 보존 — 이어달리기 가능).

## 8. Troubleshooting

### 8.1 `BLOCKED — step X missing prev marker`

strict mode 에서 prev step 이 완료되지 않았다. 해결:

```bash
# a) 제대로 entry 부터 다시 실행
./hx chain run release build
# b) 의도적 debug 면 loose one-shot
./hx chain run release verify --loose --reason "debug fixpoint"
```

### 8.2 `marker exists, step skipped` — 원치 않는 skip

한 번 완료된 step 의 marker 가 남아 있음. reset 하라 (§7).

### 8.3 `.chain-state/` 가 chflags 로 잠겨 삭제 불가

macOS:

```bash
chflags -R nouchg .chain-state/<chain>/
rm -rf .chain-state/<chain>/
```

Linux:

```bash
sudo chattr -R -i .chain-state/<chain>/
rm -rf .chain-state/<chain>/
```

가능하면 `./hx chain reset` 을 쓸 것 — audit 자동 append.

### 8.4 `./hx chain doctor` 가 unknown next 지적

`.chain` 에서 `step.a.next b` 라고 썼는데 `step.b.cmd` 선언이 없음.
오타 교정 후 `./hx chain doctor` 재실행.

## 9. 관련 파일 / Related files

- `.chain`                        — chain SSOT (flat key format)
- `tool/chain_runner.hexa`        — forward runner + guard
- `tool/hx.hexa`                  — `./hx chain …` subcommand dispatcher
- `self/raw_loader.hexa`          — `.chain` kind 파서
- `test/regression/chain_linear.hexa`      — happy path 회귀
- `test/regression/chain_mid_entry.hexa`   — loose entry 회귀
- `test/regression/chain_fail_midway.hexa` — 중간 실패 halt 회귀
- `test/regression/chain_prev_guard.hexa`  — strict prev guard 회귀
- `doc/plans/loop_chain_integration_20260420.md` — `.loop` 편입 패치 문서
- `doc/plans/chain_system_impl_summary_20260420.md` — 구현 요약
- `doc/runbook/raw_daily_flow.md` — 일상 편집 워크플로 (chain 호출 예)
- `doc/runbook/raw_emergency.md` — 긴급 복구 (emergency chain 과 짝)
