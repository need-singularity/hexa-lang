# raw Emergency Recovery Runbook

SSOT 5종 (`.raw` `.own` `.ext` `.roadmap` `.loop`) 또는 unlock 체인이
깨졌을 때의 복구 절차. 세 가지 툴 (`raw_reset.sh`, `raw_sudo_unlock.sh`,
완전 수동 fallback) 의 선택 기준, 사용법, 사후 검증 체크리스트를 정리한다.

This runbook is bilingual (Korean primary, English inline) — matches the
convention in `doc/seed_unlock_runbook.md`.

## 0. raw#8 예외 (bash-only tools)

이 디렉토리 (`tool/emergency/`) 아래 `.sh` 파일들은 HX7 / raw#8
(HEXA-ONLY `.sh` 금지) 원칙의 **명시적 예외**다. 근거:

- Emergency kit must work **when `hexa` / `stage0` / `raw_loader` /
  `raw_all` is broken** — `.hexa` tooling cannot be assumed runnable.
- bash is pre-installed on every supported dev host (macOS, Linux).
- Scope is strictly limited to 2 scripts, both calling only `git`,
  `chflags`, `uname`, `printf`, `date`, `read`. No logic beyond what
  is strictly required to unbreak the tree.

다른 `.sh` 파일을 이 디렉토리 바깥에 추가하는 것은 여전히 금지.

## 1. Prerequisites

- macOS (Darwin) — `chflags uchg` 전용. Linux sandbox에서는 chflags
  단계가 no-op 로 skip 되고, unlock/lock 은 순수 git 으로만 동작.
- `sudo` 권한 (Darwin: `chflags uchg` 해제에 필요).
- 현재 디렉토리가 `hexa-lang` repo root (`.raw` 존재) 여야 함 — 두
  스크립트 모두 `.raw` 부재 시 exit 3 으로 abort.
- `git` (HEAD 가 건강한 마지막 commit 을 가리키는 상태).

## 2. Decision Tree — 어느 스크립트를 쓸지

```
 SSOT 5개 파일 자체가 깨졌나? (parser 가 거부 / 직접 편집 실수 /
 머지 충돌 찌꺼기 등)
   ├─ YES → tool/emergency/raw_reset.sh        [Scenario 1]
   └─ NO  → raw_all / hx_unlock 이 동작 거부하나?
             ├─ YES, 파일은 멀쩡 → raw_sudo_unlock.sh        [Scenario 2]
             └─ NO, hexa 바이너리 자체가 안 돎 → manual fallback [Scenario 3]
```

## 3. Scenarios

### Scenario 1 — `.raw` malformed (parser rejects)

증상: `raw_loader` 가 valid 해 보이는 내용을 reject, 혹은 직접 편집/머지
중 인코딩 손상. 5개 파일 중 하나 이상을 git HEAD 로 **되돌려도 됨** 이
분명할 때.

명령:

```bash
cd <repo-root>
./tool/emergency/raw_reset.sh
```

효과:
- Darwin: `sudo chflags nouchg` → `git checkout HEAD -- <5>` → `sudo chflags uchg`.
- Linux: chflags skip, git checkout 만.
- `.raw-audit` 에 한 줄 append.

Audit log entry 예시:
```
EMERGENCY: raw_reset to HEAD by alice at 2026-04-20T03:14:22Z
```

주의: 복구 후 **현재 작업 중이던 SSOT 변경분은 전부 소실**된다. 꼭
필요하면 `git stash` 로 먼저 보존하거나, `git diff HEAD -- .raw` 로 덤프를
따로 저장한 뒤 실행할 것.

### Scenario 2 — `raw_all` / `hx_unlock` broken (파일은 정상)

증상: 5개 SSOT 파일 내용은 정상인데 `raw_all` 이 crash / infinite loop
하거나 `hx_unlock` 이 거부. 긴급히 SSOT 을 고쳐야 함.

명령:

```bash
cd <repo-root>
./tool/emergency/raw_sudo_unlock.sh
# prompt: Reason (logged to .raw-audit): <이유 입력, 빈 값 금지>
# ... 편집 ...
./hexa tool/hx_lock.hexa --force --reason "post-emergency"
```

효과:
- `sudo chflags nouchg` 로 5개 SSOT 강제 unlock.
- `.raw-audit` append.
- **tree 를 unlocked 상태로 두고 종료** — 개발자가 수동으로 재lock 해야 함.

Audit log entry 예시:
```
EMERGENCY unlock | alice | raw_all infinite loop fix | forced | 2026-04-20T03:22:10Z
```

사후: 반드시 문제를 먼저 고친 다음 `./hexa tool/hx_lock.hexa` 를 실행.

### Scenario 3 — stage0 binary broken (hexa 를 아예 못 돌림)

증상: `./hexa` 실행조차 실패 (stage0 자체가 꺠짐 / codegen regression /
exec bit 손실). 두 스크립트 모두 hexa 에 의존하지 않으므로 Scenario 1 을
먼저 시도해 볼 수 있으나, 그것마저 불가할 때의 fully-manual fallback:

```bash
# (Darwin only)
sudo chflags nouchg .raw .own .ext .roadmap .loop
git checkout HEAD -- .raw .own .ext .roadmap .loop
sudo chflags uchg .raw .own .ext .roadmap .loop
printf 'EMERGENCY: manual raw_reset by %s at %s\n' "$USER" \
    "$(date -u +%Y-%m-%dT%H:%M:%SZ)" >> .raw-audit

# stage0 복구: doc/seed_unlock_runbook.md 참조
```

`stage0` 자체의 재구성은 이 runbook 의 범위를 벗어난다 —
`doc/seed_unlock_runbook.md` 의 Path (a)/(b)/(c) 를 따르라.

Audit log entry 예시:
```
EMERGENCY: manual raw_reset by alice at 2026-04-20T03:30:05Z
```

## 4. Post-recovery Verification Checklist

복구 스크립트 실행 후 반드시 아래 3가지를 모두 확인:

- [ ] **chflags status** (Darwin only — tree 가 다시 lock 됐는지):
      ```bash
      ls -lO .raw .own .ext .roadmap .loop
      # 각 줄에 "uchg" 플래그가 보여야 정상.
      # raw_sudo_unlock 후라면 uchg 가 없어야 정상 (아직 재lock 전).
      ```
- [ ] **raw_all pass** (SSOT 무결성):
      ```bash
      ./hexa tool/raw_all.hexa
      # exit 0 확인.
      ```
- [ ] **git status clean** (의도하지 않은 변경 없음):
      ```bash
      git status -- .raw .own .ext .roadmap .loop
      # "nothing to commit" 확인 — .raw-audit 은 append-only 라
      # 별도 staged 상태가 될 수 있음.
      ```

세 가지가 모두 OK 면 tree 는 건강한 상태로 복귀했다. 하나라도 실패하면
이 runbook 의 처음으로 돌아가 decision tree 를 다시 밟을 것.

## 5. 관련 문서

- `doc/seed_unlock_runbook.md` — stage0 binary 재구성 절차.
- `doc/anima_raw_adoption.md` — `.raw` SSOT 정의.
- `tool/hx_lock.hexa` / `tool/hx_unlock.hexa` — 정상 경로 lock/unlock.
