# raw#0 Daily Flow Runbook

설치 (`doc/runbook/raw_install.md`) 를 마친 뒤의 **일상 편집 워크플로**.
`./hx` 래퍼를 통한 unlock → edit → lock → commit → push 사이클,
exemption / grandfather 관리, audit log 읽는 법, 그리고 “bare `git`
은 쓰지 말라” 권고.

Bilingual (Korean primary, English inline).

## 1. Core Principle — 왜 래퍼를 쓰는가 / Why the wrapper

맨 `git commit` / `git push` 는 **`.raw / .own / .ext / .roadmap / .loop`
가 일관 상태인지 전혀 모른다**. `./hx commit` 과 `./hx push` 는 다음을
강제한다:

1. `tool/raw_all.hexa` 전체 통과 (exit 0) — 깨진 rule 은 commit 자체
   거부
2. SSOT fingerprint 계산 + commit message 에 **`raw-all: <sha>@<iso-utc>`
   attestation trailer** 자동 부착
3. 모든 state 전이를 `.raw-audit` 에 append — unlock / lock / commit /
   push / harden / downgrade 전부

Server side 는 trailer 가 없는 push 를 refuse 하도록 구성될 수 있다
(CI hook, pre-receive). 그러니 **`./hx` 를 통하지 않는 commit 은 만들지
말 것**.

## 2. Scenario 1 — 새 rule 추가 / Editing SSOT

`.raw` 에 `raw 18` 신규 rule 을 추가하는 표준 시퀀스:

```bash
# 1) unlock — audit 에 "unlock" 이벤트 append + 300s re-lock 예약
./build/hexa_stage0 tool/hx_unlock.hexa --reason "add raw#18 for X"

# 2) edit — 이 시점부터 SSOT 는 writable. 잊어도 300s 후 자동 relock.
$EDITOR .raw

# 3) lock — raw_all 재실행 + Level 1 재적용 (원래 L2 였으면 L2 복귀)
./build/hexa_stage0 tool/hx_lock.hexa

# 4) commit — raw_all gate + attestation trailer + audit
./build/hexa_stage0 tool/hx.hexa commit -m "feat(raw#18): <summary>"

# 5) push — 누적 commit 전부 trailer 검증 + audit
./build/hexa_stage0 tool/hx.hexa push
```

또는 4단계를 한 번에 (편집기만 띄웠다 닫기):

```bash
# unlock → $EDITOR → lock 을 하나로. raw_all 재검증도 자동.
./build/hexa_stage0 tool/hx.hexa edit .raw
```

### 2.1 Lock 이 raw_all FAIL 을 만나면

`hx_lock` 은 **tree 를 unlocked 로 남겨두고** 종료한다 — 수정 후
재시도하라는 뜻. 로스트 워크를 자동으로 되돌리지 않음.

```
hx_lock: raw_all exit=1 — tree remains UNLOCKED.
  Fix violations, then: ./hexa tool/hx_lock.hexa
  Force (audited):     ./hexa tool/hx_lock.hexa --force --reason "..."
```

정말 긴급하면 `--force --reason "..."` 로 강제 lock — `FORCED` 태그가
audit 에 찍힌다. 남용하지 말 것.

## 3. Scenario 2 — enforce script 만 수정 (`.raw` 미변경)

SSOT **밖** 의 hexa 파일을 편집하는 경우 (예: `tool/ai_native_scan.hexa`
에 F7 메소드 추가). unlock 불필요.

```bash
# SSOT 밖 → 바로 편집
$EDITOR tool/ai_native_scan.hexa

# commit 은 여전히 attestation trailer 자동 부착
./build/hexa_stage0 tool/hx.hexa commit -m "fix(enforce): F7 false positive"

./build/hexa_stage0 tool/hx.hexa push
```

주의: `tool/` 내부에서도 **`raw_loader.hexa`, `raw_all.hexa`,
`hx_unlock.hexa`, `os_level_lock.hexa`, `build/hexa_stage0`** 등은
raw#0 bootstrap 보호 대상이라 L1 lock 이 걸려 있다 (`.raw` 의 raw#12
scope + `tool/os_level_lock.hexa` 의 lock target 확장). 그런 파일을
편집할 때는 **Scenario 1** 과 동일한 unlock ceremony 를 거치라.

현 시점 (2026-04-20) 기준 `tool/os_level_lock.hexa` 의 `_optional_targets()`
리스트를 확인해 정확히 어떤 파일이 잠겨 있는지 파악할 것.

## 4. Scenario 3 — Emergency (raw_loader 또는 SSOT 깨짐)

### 4.1 `.raw` 가 malformed (parser reject)

```bash
# git HEAD 로 되돌림 (5 SSOT 전부) + chflags 자동 관리
./tool/emergency/raw_reset.sh
```

효과:

- Darwin: `sudo chflags nouchg` → `git checkout HEAD -- <5>` →
  `sudo chflags uchg`.
- Linux: chflags skip, git checkout 만.
- `.raw-audit` 에 `EMERGENCY: raw_reset to HEAD by <user> at <ts>` append.

**현재 작업 중이던 SSOT 변경분 전부 소실**. 꼭 필요하면 미리
`git diff HEAD -- .raw` 로 덤프 저장.

### 4.2 `raw_all` / `hx_unlock` 은 깨졌는데 파일은 정상

```bash
./tool/emergency/raw_sudo_unlock.sh
# prompt 에 reason 입력 → sudo chflags nouchg 로 5개 SSOT 강제 unlock
# → tree 가 unlocked 상태로 종료 (수동 편집 후…)
./build/hexa_stage0 tool/hx_lock.hexa --force --reason "post-emergency"
```

자세한 시나리오 / decision tree 는 `doc/runbook/raw_emergency.md`.

## 5. Scenario 4 — Grandfather exemption 추가

기존 violation 을 한시적으로 면제. **expiry 는 필수** — indefinite
grandfather 는 raw#0 설계 금지.

```bash
# raw#14 (no hardcoding) 규칙에 대해 self/legacy/foo.hexa 전체 면제
# 2026-06-30 까지만 유효, 그 후 자동 FAIL
./build/hexa_stage0 tool/raw_exemptions.hexa add \
    14 self/legacy/foo.hexa '*' "legacy BLAS glue — 2026-Q2 rewrite pending" 2026-06-30

# 목록 확인
./build/hexa_stage0 tool/raw_exemptions.hexa list 14

# 만료 체크 (CI / hx status 에서도 자동 호출됨)
./build/hexa_stage0 tool/raw_exemptions.hexa check
```

인자 의미:

| 위치 | 값                   | 비고                                |
| :--: | :------------------- | :---------------------------------- |
|  1   | `14`                 | rule id (`.raw` / `own:3` 도 가능)  |
|  2   | `self/legacy/foo.hexa` | 면제할 파일 경로                  |
|  3   | `'*'`                | line_range: 정수 / `10-20` / `*`    |
|  4   | `"..."`              | reason (audit 에 기록)              |
|  5   | `2026-06-30`         | expiry `YYYY-MM-DD` — 필수          |

Expiry 7일 전부터 `raw_all` 이 WARN 표시. 지나면 FAIL — 재심 or 수정
강제.

## 6. `./hx` Subcommand Reference

`./hx` 는 `tool/hx.hexa` 가 제공하는 top-level wrapper.
`./build/hexa_stage0 tool/hx.hexa <sub>` 와 동치. 현재 구현된 subcommand:

| cmd       | effect                                                           |
| :-------- | :--------------------------------------------------------------- |
| `commit`  | raw_all (full) → `git commit` → attestation trailer + audit      |
| `push`    | raw_all → per-commit trailer 검증 → `git push` → audit           |
| `edit`    | `hx_unlock` → `$EDITOR <file>` → `hx_lock` (원샷)                |
| `unlock`  | `tool/hx_unlock.hexa` 위임 (`--reason` 필수, 300s window)        |
| `lock`    | `tool/hx_lock.hexa` 위임 (raw_all 재검증 후 재잠금)              |
| `status`  | chflags status + last 3 audit entries + raw_all --fast           |
| `verify`  | `raw_all --fast` 만                                              |
| `help`    | 사용법 출력                                                      |

Level 관련 (`harden` / `downgrade`) 과 최초 설치 (`init`) 는 현재
별도 스크립트로 직접 호출:

| task       | 명령                                                             |
| :--------- | :--------------------------------------------------------------- |
| 최초 설치  | `./build/hexa_stage0 tool/hx_init.hexa`                          |
| L1→L2 승격 | `./build/hexa_stage0 tool/hx_harden.hexa --reason "..."`         |
| L2→L1 강등 | `./build/hexa_stage0 tool/hx_downgrade.hexa --reason "..."`      |

(차후 버전에서 `./hx harden` / `./hx downgrade` / `./hx init` 으로
dispatch 될 예정 — `tool/hx.hexa` main dispatcher 참조.)

## 7. Audit Log 읽는 법 / Reading `.raw-audit`

format (one line per event):

```
timestamp | event | actor | reason | result | sha_before | sha_after
```

필드:

- `timestamp` — ISO-8601 UTC
- `event`     — `unlock | lock | commit | push | harden | downgrade | init | violation | emergency | downgrade-on-relock`
- `actor`     — `$USER` 또는 `--actor` override
- `reason`    — unlock / harden / emergency 에서는 필수
- `result`    — `ok | fail | blocked | FORCED | partial (...) | warn`
- `sha_before / sha_after` — 5 SSOT 를 합친 sha256 (바뀌면 변경 있음)

예시 (hex digest 축약):

```
2026-04-20T02:15:03Z | init     | ghost | hx_init first-run bootstrap | ok    | a3f… | -
2026-04-20T02:45:11Z | unlock   | ghost | add raw#18 for X [prior_levels=.raw=1,.own=1,...] | ok | a3f… | -
2026-04-20T02:47:02Z | lock     | ghost | manual lock | ok | a3f… | b7c…
2026-04-20T02:47:33Z | commit   | ghost | feat(raw#18): …  | ok | b7c… | b7c…
2026-04-20T02:48:00Z | push     | ghost | branch=main commits=1 | ok | - | -
```

편리한 조회:

```bash
./build/hexa_stage0 tool/raw_audit.hexa tail 20    # 최근 20줄
./build/hexa_stage0 tool/raw_audit.hexa verify     # 포맷 sanity
./build/hexa_stage0 tool/raw_audit.hexa rotate     # 월별 rotation (10k 줄 초과 시 자동)
```

`.raw-audit` 는 **append-only** — `uchg` 에 의해 기존 줄은 변경
불가. raw_audit 는 append 직전 잠깐만 window 를 연 뒤 즉시 닫는다.

## 8. Re-lock Window — 300s, 잊어도 안전

`hx_unlock` 은 백그라운드로 detach 된 sleep→lock 데몬을 spawn 한다.
기본 window = 300s, 최대 3600s (`--window <sec>` 로 조정).

```bash
./build/hexa_stage0 tool/hx_unlock.hexa --reason "quick typo" --window 60
```

window 이 만료되면:

1. raw_all 재실행
2. **PASS** → Level 1 재적용 + 원래 L2 였던 target 을 sudo 로 L2 복귀
   (sudo 캐시가 만료되었으면 L1 으로 fallback + audit `downgrade-on-relock`
   WARN)
3. **FAIL** → tree 를 unlocked 로 두고 audit 에 `fail` 기록
   (개발자가 수정하도록)

이게 의도 — **잊고 편집해도 안전** 이지만, rule 이 깨져 있으면
수정 전까지 재잠금하지 않는다. “half-broken 상태로 자동 잠금” 은
최악의 UX.

수동 즉시 재잠금:

```bash
./build/hexa_stage0 tool/hx_lock.hexa
```

## 9. “Bare `git` 금지” 권고 / Don’t call git directly

다음은 **피해야** 할 패턴:

```bash
# BAD — raw_all gate 우회, attestation trailer 없음, audit 누락
git commit -m "quick fix"
git push
```

왜 안 되나:

1. **깨진 rule 을 감지하지 못한다** — `.raw` 에 invalid edit 이 있어도
   그대로 commit. 다음 CI run 에서야 발각.
2. **attestation trailer 부재** — server 쪽 pre-receive hook 이
   `raw-all: …` 이 없는 commit 을 refuse 하도록 구성되면 push 가 실패
   (에러 메시지: `missing raw-all trailer`).
3. **audit 공백** — `.raw-audit` 에 이벤트 없음 → 누가 무엇을 언제
   했는지 추적 불가. paper trail 붕괴.

`./hx` 경로를 **항상** 사용하라. Shell alias / git wrapper 로 강제하고
싶다면 `~/.bashrc` 또는 `~/.zshrc` 에:

```bash
alias gc='echo "use: ./hx commit -m \"...\"" >&2; false'
alias gp='echo "use: ./hx push" >&2; false'
```

(실수 방지. 의도적으로 bare `git` 이 필요하면 `command git ...` 으로
bypass.)

## 10. Checklist — 일상 편집 전후

편집 **전**:

- [ ] `./hx status` — chflags / last audit / raw_all --fast 상태 확인
- [ ] SSOT 편집이 필요하면 `./hx unlock --reason "..."` 또는
      `./hx edit <file>`

편집 **후**:

- [ ] `./hx verify` — raw_all --fast pass 확인
- [ ] `./hx lock` — 재잠금 (혹은 300s 자동 relock 대기)
- [ ] `./hx commit -m "..."` — gate + trailer + audit
- [ ] `./hx push` — trailer 검증 + audit

Release / stable checkpoint 후:

- [ ] `sudo -v && ./build/hexa_stage0 tool/hx_harden.hexa --reason "..."`
      — `.raw / .own` 을 L2 로 승격 (paper trail 강화)

Active dev 로 복귀 시:

- [ ] `sudo -v && ./build/hexa_stage0 tool/hx_downgrade.hexa --reason "..."`
      — L2 → L1 (friction 완화)

## 11. 관련 파일 / Related files

- `tool/hx.hexa`             — `./hx` top-level wrapper (commit/push/edit/…)
- `tool/hx_unlock.hexa`      — blessed unlock gate (raw_all + audit + 300s)
- `tool/hx_lock.hexa`        — re-lock path (raw_all 재검증 + level 복원)
- `tool/raw_all.hexa`        — rule orchestrator
- `tool/raw_audit.hexa`      — `.raw-audit` append-only
- `tool/raw_exemptions.hexa` — per-rule grandfather (expiry required)
- `tool/emergency/raw_reset.sh`      — `.raw` HEAD 복원
- `tool/emergency/raw_sudo_unlock.sh`— force unlock (파일 정상 시)
- `doc/runbook/raw_install.md`       — 최초 설치 (read first)
- `doc/runbook/raw_emergency.md`     — 긴급 복구
- `doc/plans/raw_os_level_enforcement_brainstorm_20260420.json` —
  설계 근거 / idea pool
