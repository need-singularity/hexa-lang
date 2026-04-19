---
status: summary
related-raw-rule: 0
related-roadmap: []
date: 2026-04-20
---

# Chain system — 구현 요약 / implementation summary

2026-04-20 세션에 두 agent (core + test/docs) 가 병렬로 구현한 `.chain`
forward-chain automation 전체 overview.

## 1. 목표 / Goal

- `.chain` SSOT + `tool/chain_runner.hexa` 로 declarative forward-chain
  automation 을 제공.
- OS-level marker (`.chain-state/<chain>/step_<id>_ok`) 로 진행 상태를
  파일시스템에 기록.
- `./hx chain <subcommand>` 로 run / status / reset / list 제공.
- `.loop` 는 chain 을 trigger 하는 얇은 래퍼로만 사용.

## 2. 파일 목록 / Files

Agent A (core — `a1ace534768f42e76`):

- `.chain`                      — SSOT, release + daily-check 샘플 선언
- `self/raw_loader.hexa`        — `chain` kind + `step.<id>.<prop>` flat
                                  key 파서 확장
- `tool/chain_runner.hexa`      — forward runner + marker logic +
                                  strict/loose guard
- `tool/hx.hexa`                — `chain` subcommand dispatcher
- `.chain-state/`               — runtime marker directory (gitignored,
                                  chflags-managed)

Agent B (본 commit — test + docs + loop proposal):

- `test/regression/chain_linear.hexa`       — happy path 회귀
- `test/regression/chain_mid_entry.hexa`    — loose mid-entry 회귀
- `test/regression/chain_fail_midway.hexa`  — 중간 실패 halt 회귀
- `test/regression/chain_prev_guard.hexa`   — strict prev guard 회귀
- `doc/runbook/raw_chain.md`                — 공식 runbook
- `doc/plans/loop_chain_integration_20260420.md` — `.loop` 확장 패치 문서
- `doc/plans/chain_system_impl_summary_20260420.md` — 본 문서

## 3. Architecture — ASCII diagram

```
                      ┌──────────────────┐
     user / cron ───▶ │   ./hx chain     │ ── dispatches ──┐
                      └──────────────────┘                 │
                                                           ▼
                                            ┌───────────────────────────┐
                                            │ tool/chain_runner.hexa    │
                                            │   · parse .chain          │
                                            │   · forward walk          │
                                            │   · strict/loose guard    │
                                            │   · touch marker on ok    │
                                            │   · raw_audit on reset    │
                                            └──────────┬────────────────┘
                                                       │
                         ┌─────────────────────────────┼──────────────────────────┐
                         │                             │                          │
                         ▼                             ▼                          ▼
                ┌────────────────┐         ┌──────────────────────┐      ┌─────────────────┐
                │   .chain SSOT  │         │  .chain-state/<id>/  │      │  .raw-audit     │
                │  (raw_loader)  │         │    step_<X>_ok       │      │ (append-only)   │
                │                │         │   (chflags uchg)     │      │                 │
                └────────────────┘         └──────────────────────┘      └─────────────────┘
                         ▲                             ▲
                         │                             │
                         │                             │
                ┌────────┴─────────┐         ┌─────────┴────────────┐
                │  .loop loop#3/#4 │         │   forward walk        │
                │  nightly+hourly  │         │   halts on first FAIL │
                │  chain trigger   │         │   (strict)            │
                └──────────────────┘         └───────────────────────┘
```

- 세로축: declarative (`.chain`) ↔ runtime (`.chain-state`) ↔
  paper trail (`.raw-audit`).
- 가로축: trigger (cron / CLI) → runner → state writes.

## 4. 회귀 매트릭스 / Regression matrix

| Test                     | Chain shape   | Mode    | Entry | Expect                                    |
| :----------------------- | :------------ | :------ | :---- | :---------------------------------------- |
| `chain_linear`           | a→b→c         | strict  | a     | 전 step PASS + 3 markers                 |
| `chain_mid_entry`        | a→b→c→d→e     | loose   | c     | c/d/e PASS, a/b 실행 안 됨               |
| `chain_fail_midway`      | a→b(fail)→c→d | strict  | a     | a PASS, b FAIL, 이후 halt, exit != 0     |
| `chain_prev_guard`       | a→b→c         | strict  | c     | BLOCKED, marker 무생성, exit != 0        |

모두 Linux sandbox 에서 stage0 미실행 시 자동 SKIP — macOS 회귀 runner
전용.

## 5. Follow-up — 미구현 목록 (Phase 6+)

- **Phase 6a — parallel steps**: `step.X.parallel [Y Z]` 로 동시 실행.
  Runner 는 goroutine-style worker 또는 shell `&` wait 로 구현.
- **Phase 6b — conditional steps**: `step.X.cond <expr>` true 일 때만
  실행, else skip (marker 는 `step_X_skip` 로 기록).
- **Phase 7 — cross-chain dependency**: `step.X.requires <other-chain>:<step>`
  로 다른 chain 의 marker 를 prerequisite 로 삼기.
- **Phase 8 — shell tab completion**: `./hx chain run <TAB>` 에서
  `.chain` parse 해 chain id + step id 자동 완성 제공.
- **Follow-up Q1 — hourly drift frequency**: `loop#4 hourly` 가 실제로
  몇 번/일 drift 를 잡는지 2주 관찰 후 daily 로 완화 여부 결정.
- **Follow-up Q2 — marker log → raw_audit**: chain 진행 marker 를
  자동으로 `.raw-audit` append 할지 여부. 현재는 reset 만 기록.
- **Follow-up Q3 — timeout / cron 충돌 정책**: `.loop interval` 과
  `step.X.timeout` 이 동시에 제약할 때 우선순위 결정.
- **Follow-up Q4 — chain doctor cycle detection**: 현재는 간단 DFS —
  large chain 에서는 iterative + memoization 필요.

## 6. 관련 문서 / See also

- `doc/runbook/raw_chain.md`                    — 공식 runbook
- `doc/plans/loop_chain_integration_20260420.md` — `.loop` 확장 패치
- `doc/plans/raw17_convergence_add_20260420.md`  — raw#17 (fixpoint)
                                                   과 release chain 연결
- `doc/plans/raw_os_level_enforcement_brainstorm_20260420.json` —
  OS-level enforcement 설계 맥락 (chain marker 도 동일 철학)
