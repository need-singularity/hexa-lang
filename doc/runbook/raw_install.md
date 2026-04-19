# raw#0 Install & Initialization Runbook

hexa-lang SSOT OS-level self-enforcement 의 최초 설치 및 초기화 절차.
`.raw / .own / .ext / .roadmap / .loop` 다섯 파일을 커널 수준 immutable
flag 로 잠그고, `./hx` 래퍼와 attestation / audit 인프라를 준비하는
one-shot bootstrap 가이드.

Bilingual (Korean primary, English inline) — matches `doc/` convention.

## 1. Overview — 왜 이걸 하는가 / What this gives you

- **5 SSOT (single source of truth) 파일**:
  - `.raw`     — L0 universal (17 rules)
  - `.own`     — L1 project-local (3 rules)
  - `.ext`     — external resources (SSH hosts, API endpoints, …)
  - `.roadmap` — phase / track / milestone SSOT
  - `.loop`    — recurring tasks (cron, interval, ScheduleWakeup)
- **L0 의 raw#0** 은 위 5개 파일 (+ raw#12 AI-tool config bans)
  을 **OS-level immutable** 로 만들라고 명령한다. Shell hook, lint,
  pre-commit 은 전부 우회 가능 — 유일한 진짜 enforcement 는 kernel 의
  `EPERM` (Operation not permitted).
- **Tiered lock (2026-04-20)** — 세 단계:
  | Level | Sudo | 보호 강도                              | 언제 쓰나                |
  | :---: | :--: | :------------------------------------- | :----------------------- |
  |   0   |  ✗   | CI-only, no kernel flag                | platform unsupported     |
  |   1   |  ✗   | macOS `uchg` / Linux `+i` best-effort  | **default**, 친화적 friction |
  |   2   |  ✓   | macOS `schg` / Linux root `+i`         | 숙련자, 안정 tree, paper trail |

- **`./hx` 래퍼** — `./hx commit`, `./hx push`, `./hx edit` 는 raw_all
  을 gate 로 통과한 뒤에만 git 을 부른다. 모든 commit 은 자동으로
  `raw-all: <sha>@<iso-utc>` attestation trailer 를 달고, 모든 state
  전이는 `.raw-audit` append-only 로그에 찍힌다.

## 2. Prerequisites — 요구사항

- **OS**: macOS (Darwin) 가 full feature. Linux 는 `chattr` fallback —
  L1/L2 가 의미상 동일 (`+i` 는 둘 다 root 요구). 기타 플랫폼은
  Level 0 no-op.
- **Sudo**: 기본 Level 1 설치에는 **불필요**. L2 승격 (`./hx harden`)
  시에만 passwordless sudo 필요.
- **Binaries**:
  - `./build/hexa_stage0` 실행 가능 (Release build — `tool/build_stage0.hexa`
    로 재빌드 가능)
  - `openssl` (attestation HMAC key 생성)
  - `git`, `date`, `shasum` (또는 `sha256sum`) — 표준 POSIX
- **Repo shape**: 5 SSOT 파일 전부 존재해야 함. `hx_init` 이 부재시
  `"this is not a hexa-lang repo?"` 로 abort.

## 3. Quick Start — Level 1 (권장 기본)

Fresh clone 후 한 번만 실행하면 된다.

```bash
git clone <hexa-lang-repo-url>
cd hexa-lang

# stage0 binary 가 없다면 먼저 빌드 (doc/seed_unlock_runbook.md 참조)
./build/hexa_stage0 --version || {
    echo "stage0 없음 — seed_unlock_runbook 절차로 재구성" >&2
    exit 1
}

# 1) Level 1 uchg/+i 적용 + .raw-audit + .raw-attest-key 생성
./build/hexa_stage0 tool/hx_init.hexa

# 2) 현황 확인 — 각 SSOT 옆에 L1 표시가 뜨면 OK
./build/hexa_stage0 tool/os_level_lock.hexa status
```

기대 출력 (macOS):

```
── hx init (tiered lock, Level 1 default) ──
root:  /Users/you/Dev/hexa-lang
actor: you

── step 1: verify repo shape ──
  ok — 5 SSOT present

── step 2: apply Level 1 ──
  L1        .raw
  L1        .own
  L1        .ext
  L1        .roadmap
  L1        .loop
  ...

── step 3: .raw-audit bootstrap ──
  ok — audit log ready

── step 4: .raw-attest-key bootstrap ──
  created   .raw-attest-key (600, 256-bit HMAC)
  updated   .gitignore (+ .raw-attest-key)

── OK ──
기본 Level 1 설치 완료.
  · All 5 SSOT + raw#12 bans locked at Level 1 (user-removable).
  · Editing: `./hx unlock --reason "..."`
강화 원하면 `./hx harden` (sudo 필요). Level 2 opt-in.
```

이 시점에 `.raw` 에 직접 `echo x >> .raw` 를 시도하면 kernel 이
`Operation not permitted` (EPERM) 을 반환한다 — 그게 바로 raw#0 의
존재 이유다.

## 4. Optional Hardening — Level 2 (sudo 필요)

Stable checkpoint (릴리스, 장기 체크인 등) 에서 `.raw / .own` 을
더 강하게 잠그고 싶다면 L2 로 승격한다.

```bash
# passwordless sudo 캐시
sudo -v

# .raw / .own 을 schg / root-+i 로 승격. --reason 필수 (audit 에 기록)
./build/hexa_stage0 tool/hx_harden.hexa --reason "v1.0 release checkpoint"

# 승격 확인 — .raw / .own 옆에 L2-schg 표시
./build/hexa_stage0 tool/os_level_lock.hexa status
```

L2 승격 후 `./hx unlock` 을 부르면 자동으로 sudo 가 요구된다
(passwordless 캐시가 있으면 무감각). 캐시가 만료되면 다음 메시지가
뜬다:

```
hx_unlock: one or more targets are at Level 2 (schg / root-immutable).
  Passwordless sudo is unavailable (sudo -n true failed).

  Options:
    1) sudo -v       # cache credentials, then retry hx unlock
    2) ./hx downgrade --reason "temp edit"   # L2 → L1, then unlock normally
```

주의: `hx_harden` 은 현재 `.raw / .own` 만 승격한다. 다른 3 SSOT
(`.ext / .roadmap / .loop`) 는 L1 유지 — 활발히 편집되는 layer 라
L2 friction 이 적합하지 않다는 판단 (`tool/os_level_lock.hexa` 의
`_critical_targets()` 참조).

## 5. Downgrade — Level 2 → Level 1

```bash
sudo -v   # 크리덴셜 캐시
./build/hexa_stage0 tool/hx_downgrade.hexa --reason "active dev — L1 friction ok"

# 확인 — L2-schg 가 L1-uchg 로 떨어져야 함
./build/hexa_stage0 tool/os_level_lock.hexa status
```

Downgrade 는 L2 플래그 해제 → 즉시 L1 재적용 을 원자적으로 수행한다.
**중간에 Level 0 (전혀 안 잠긴 상태) 를 잠시도 거치지 않음** — 실패
시에도 경고 후 수동 `./hexa tool/hx_lock.hexa` 를 권유.

## 6. Level 선택 가이드

| Level | Sudo | 보호                              | 언제                                  |
| :---: | :--: | :-------------------------------- | :------------------------------------ |
|   0   |  ✗   | CI-only (flag 없음)               | platform 미지원 (BSD/Linux 아닌 host) |
|   1   |  ✗   | user-removable, 친화적 friction   | **기본** — 대부분의 dev workflow      |
|   2   |  ✓   | root-immutable, sudo ceremony     | 숙련자, stable tree, paper trail 필요 |

핵심 사항:

- **L1 도 충분하다.** macOS `uchg` 는 “root 없이 제거 가능” 이지만
  실수로는 절대 못 건드린다. `$EDITOR .raw` 는 저장 시 `EPERM` 으로
  거부됨 — 의도적인 `chflags nouchg` 우회만 가능.
- **L2 는 ceremony**. Sudo 매번 요구 → 변경 의도가 git history 에
  강제로 기록됨 (paper trail).
- **L0 은 fallback**. unsupported OS 에서도 raw_all 은 여전히 돌지만
  kernel enforcement 는 없다. CI 는 이걸로 충분 (writable tree 필요).

권장: **개발 시 L1**, **릴리스 / 장기 정지 시 L2**.

## 7. Verification — 설치가 제대로 됐는지 확인

```bash
# 1. 전체 rule pass 확인 (fast path)
./build/hexa_stage0 tool/raw_all.hexa --fast

# 2. chflags / chattr 상태
./build/hexa_stage0 tool/os_level_lock.hexa status

# 3. audit log 헤더 + 첫 init 이벤트
./build/hexa_stage0 tool/raw_audit.hexa tail 10

# 4. 통합 status (위 세 가지를 한 번에)
./build/hexa_stage0 tool/hx.hexa status
```

모두 exit 0 이면 설치 완료. `status` 출력에서 기대할 것:

- chflags status: `.raw / .own / .ext / .roadmap / .loop` 각 L1 이상
- last audit entry: `init | <user> | hx_init first-run bootstrap | ok | <sha> | -`
- raw_all --fast: `PASS` + rule count

## 8. Troubleshooting — 흔한 이슈

### 8.1 `raw_all FAIL` — 설치 시점에 rule 위반

`hx_init` 이전에 repo 가 이미 dirty 하다면 rule 이 깨질 수 있다
(예: `.raw` 는 있지만 `.loop` 가 비어 있어 `.loop` lint 가 실패).

```bash
# 어떤 rule 이 깨졌는지 전부 보기
./build/hexa_stage0 tool/raw_all.hexa
# → 수정 → 재시도
./build/hexa_stage0 tool/hx_init.hexa --force
```

`--force` 는 Level 1 apply 가 부분 실패해도 계속 진행한다 (idempotent —
이미 잠긴 파일은 no-op).

### 8.2 `sudo -n true failed` — 패스워드 없이 sudo 불가

Level 2 승격 / 해제 시 `passwordless sudo unavailable` 메시지. 해결:

```bash
sudo -v           # 크리덴셜 캐시 (기본 5분)
./build/hexa_stage0 tool/hx_harden.hexa --reason "..."
```

캐시 없이 L2 를 쓰려는 시도는 (설계상) 거부된다 — 중간에 stdin
prompt 가 뜨면 audit log 가 깨지기 때문. 캐시가 없으면 **Level 1 을
유지하라** — L1 이 충분하다.

### 8.3 `.raw-attest-key` 가 이미 존재

`hx_init` 이 재실행되면 기존 key 를 **절대 덮어쓰지 않는다**
(idempotent). 손상된 key 를 재생성하려면 수동으로:

```bash
rm -f .raw-attest-key
./build/hexa_stage0 tool/hx_init.hexa
```

주의: key 가 바뀌면 기존 commit 의 attestation 서명은 더 이상
검증되지 않는다 — rotate 는 신중히.

### 8.4 `./hexa` vs `./build/hexa_stage0`

이 문서에서는 일관성을 위해 `./build/hexa_stage0` 를 쓴다. `./hexa`
는 역사적 shim 이며 동일 binary 로 symlink 되어 있는 경우가 많다 —
둘 중 어느 쪽을 써도 된다.

### 8.5 Linux sandbox 에서 chflags 가 skip 된다고 뜸

정상. Linux 는 `chattr +i` fallback 을 쓴다 — 권한이 있으면 적용,
없으면 warning 만 남기고 계속 진행한다. CI / container 환경에서는
Level 0 (no-op) 이 실질적으로 강제된다.

### 8.6 Emergency — `.raw` 자체가 깨짐 / unlock 불가

`doc/runbook/raw_emergency.md` 참조. 세 시나리오:

1. `tool/emergency/raw_reset.sh` — `.raw` 파일이 malformed
2. `tool/emergency/raw_sudo_unlock.sh` — `raw_all` 이 동작 거부
3. 완전 수동 fallback — `stage0` 자체가 안 돎

## 9. 다음 단계

설치가 끝나면 `doc/runbook/raw_daily_flow.md` 를 읽고 일상 편집
워크플로 (unlock / edit / lock / commit / push) 를 익혀라. 직접
`git commit` / `git push` 를 호출하는 건 **권장하지 않음** — raw_all
gate 와 attestation trailer 가 빠지고, server side 정책이 commit 을
거부할 수 있다.

## 10. 관련 파일 / Related files

- `.raw` — L0 17 rules + 포맷 spec
- `.own` — L1 3 rules
- `tool/hx_init.hexa`       — 본 runbook 의 구현체 (first-run bootstrap)
- `tool/hx_harden.hexa`     — L1 → L2 승격
- `tool/hx_downgrade.hexa`  — L2 → L1 강등
- `tool/os_level_lock.hexa` — chflags / chattr 관리 (low-level)
- `tool/raw_all.hexa`       — rule orchestrator (gate)
- `tool/raw_audit.hexa`     — `.raw-audit` append-only 로그
- `tool/hx.hexa`            — `./hx` top-level wrapper
- `doc/runbook/raw_daily_flow.md`  — 일상 편집 워크플로
- `doc/runbook/raw_emergency.md`   — 긴급 복구 절차
