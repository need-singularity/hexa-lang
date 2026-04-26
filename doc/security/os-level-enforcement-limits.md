# OS-level Enforcement 한계 (raw#1 os-lock)

> **TL;DR** — `chflags uchg` 는 **ceremony-friendly friction** 이다. kernel enforcement 가 아니며, owner 본인이 언제든 해제할 수 있다. "실수" 와 "충동적 편집" 을 막는 speed bump 이지 vault 가 아니다.

---

## 1. 목적 & 범위

### 무엇을 보호하는가

- `.raw` 로 선언된 **5 SSOT 파일 + 핵심 시드** 가 의도치 않게 수정되는 것.
- AI agent / IDE / grep+sed 파이프라인 / `git add -A` 의 무차별 휩쓸기.
- 사용자 본인의 **반사적/충동적 편집** (unlock ceremony 를 강제하여 자기 관찰 유도).

### 무엇을 보호하지 않는가

- **외부 공격자** (root 권한, 물리 접근, 파일시스템 exploit).
- **의도적 악의** (사용자가 마음먹고 bypass 하려는 경우).
- **커널/드라이버 레벨 침해**.

### 공격 모델 (threat model)

| 가정 | 보호 여부 |
|---|---|
| owner 가 `git add -A` 로 실수 staging | O |
| IDE autoformat 이 파일 저장 | O |
| AI agent 가 `Edit` 호출 | O |
| grep+sed 치환 스크립트 | O |
| sudo 권한자 의도적 해제 | X |
| root 공격자 | X |
| 파일시스템 레벨 exploit | X |
| Linux 타겟 (chflags 없음) | X |

---

## 2. 실제 메커니즘

### `chflags uchg` 동작

- POSIX fs flag (BSD-derived).
- 파일 inode 의 `st_flags` 에 `UF_IMMUTABLE` 비트 set.
- 커널이 `write(2)` / `unlink(2)` / `rename(2)` 호출 시 `EPERM` 반환.
- **owner 가 `chflags nouchg <path>` 한 번이면 즉시 해제**.
- 재부팅 후에도 flag 유지 (ondisk).

### 적용 범위

- macOS / FreeBSD / Darwin 계열.
- Linux `chattr +i` 는 이름만 유사, semantics 다름 + 포팅 안 됨.

### 현재 구현 (참고)

- `tool/os_level_lock.hexa` — `chflags uchg/nouchg` wrapper.
- `.raw` 선언된 파일만 lock 대상.
- `chflags -R` 재귀 옵션은 **사용 안 함** (디렉토리 단위 lock 의 부작용 방지).

---

## 3. 명시적 한계 (L1 ~ L6)

### L1: User-removable (owner bypass)

- owner 가 `chflags nouchg <path>` 로 즉시 해제.
- **외부자 공격 방어 아님**. owner 는 root 이든 일반 user 든 언제든 풀 수 있다.
- 의도: owner 에게 "지금 내가 보호된 파일을 건드리려 한다" 는 명시적 순간을 강제.

### L2: sudo 한 번으로 무력화

```bash
sudo chflags nouchg /path/to/file
```

- `sudo` 획득 시 zero friction 으로 해제.
- **방어 범위 밖**. 이를 막으려면 SIP / SecureBoot / MDM 레벨이 필요.
- ceremony 의 가치는 "sudo 를 입력했다" 는 **사용자 본인의 자각 순간** 에 있다.

### L3: 파일 교체 회피 (rm + new)

- `chflags uchg` 는 inode flag. 파일 **삭제** 에도 `EPERM` 이 난다.
- 그러나 디렉토리 자체가 locked 가 아니면 새 파일을 만들 수는 있다.
- 파이프라인:
  1. `rm <locked_file>` → `EPERM` 차단 (OK).
  2. 그러나 공격자가 `chflags nouchg` 먼저 → `rm` → 새 파일 생성 → 새 파일은 flag 없음.
- 따라서 **`.raw-audit` 해시 체인 + relock daemon 이 2차 방어**.

### L4: 디렉토리 단위 수정

- 디렉토리 자체가 locked 아니면:
  - 내부의 locked 파일을 `unlink` 는 `EPERM` (inode flag 보호).
  - 하지만 `chflags nouchg` 후 rm → 재생성 이 가능.
- 전체 디렉토리 lock 은 적용하지 **않음** (daemon / tool 편집 체인 전체를 막게 되어 운용 불가).

### L5: git 은 영향 없음

- `.git/objects/` 는 보호 **안 함**. object 자체는 content-addressable 이므로 수정 의미가 없다.
- **`git checkout -- <path>`** 는 workdir 쓰기. locked 이면 `EPERM` 으로 실패한다 (수동 unlock 필요).
- 그러나 `git reset --hard` / `git stash pop` / branch switch 등 일부 git 내부 경로가 EPERM 에 부딪히면 **중간 실패 상태** 로 남을 수 있다.
- git 으로부터의 우회는 기본적으로 막히지만, 상태가 꼬이는 비용이 있음을 인지.

### L6: Linux 이식 불가

- `chflags` 는 BSD/Darwin 전용 시스템 콜.
- Linux 는 `chattr +i` (ext4/xfs) 이나 FS 종속이고 root 만 설정 가능.
- **현재 구현은 macOS 전용**.
- Linux 포팅 시에는 별도 레이어 (LSM / userfaultfd / fuse overlay) 가 필요.

---

## 4. 위협 모델 상세

### 방어 대상 (in-scope)

- **실수로 5 SSOT 편집**
  - `git add -A` 가 의도치 않게 .raw 파일을 staging.
  - IDE 자동 포맷터 / linter autofix 가 파일 저장.
  - `grep -r ... | xargs sed -i` 치환이 .raw 파일을 포함.
- **AI agent 의 충동적 편집**
  - bg agent 가 `Edit` 호출 시 write syscall EPERM 으로 즉시 실패 → 명시적 unlock 필요.
  - 병렬 agent race condition 에서 "먼저 편집" 을 방지.

### 방어 제외 (out-of-scope)

- **의도적 악의**: owner 가 작정하고 bypass 하면 막을 방법 없음.
- **root 공격자**: 이미 파일시스템 전체를 통제.
- **커널 exploit**: fs flag 자체가 커널 레벨에서 무력화.
- **물리 접근**: 외장 연결로 마운트하면 flag 무시 가능.

---

## 5. Ceremony 모델 명시

### 핵심 전제

이 시스템은 **"vault" 가 아니다**. **"speed bump"** 다.

### Friction 의 목적

unlock 행위 자체를 **4단계 ceremony** 로 만들어 자기 관찰을 유도:

1. **Unlock** — `hx_unlock.hexa <path> <reason>` 호출.
2. **Reason** — 사유 기입 (roadmap ref + 왜 지금 필요한지).
3. **Edit** — 실제 편집 수행.
4. **Relock** — 즉시 재잠금 또는 daemon 이 300s 후 자동 재잠금.

### Paper trail

- `.raw-audit` 에 timestamp + path + reason + hash 기록.
- 사후 회수 가능: "언제 누가 왜 무엇을 편집했는가" 전부 추적.
- 해시 체인으로 audit log 자체의 변조도 탐지 가능.

---

## 6. 실제 방어 체인 (5-layer)

| 레이어 | 파일 | 역할 |
|---|---|---|
| (a) 선언 | `.raw` | 어떤 파일이 raw#1 os-lock 대상인지 명시 |
| (b) 잠금 | `chflags uchg` (tool/os_level_lock.hexa) | 커널 레벨 write 차단 (EPERM) |
| (c) 감사 | `.raw-audit` (tool/raw_audit.hexa) | unlock/edit 사건 + 해시 체인 |
| (d) 재잠금 | relock_daemon (tool/relock_daemon.hexa) | 300s 후 자동 재잠금 |
| (e) 회수 | `tool/emergency/raw_reset.sh` | 비상 해제 (audit 에 명시적 emergency 기록) |

각 레이어가 단독으로는 완전하지 않으며, 조합으로 ceremony friction 을 만든다.

---

## 7. 사용자 의무

### 해야 할 것

- **`hx_unlock.hexa` 경유 편집**.
  ```bash
  hx_unlock.hexa <path> "roadmap-raw#1 P2 — audit seed 업데이트"
  <edit>
  hx_lock.hexa <path>  # 또는 daemon 에 맡김
  ```
- **unlock reason** 에 roadmap ref 와 실제 이유 둘 다 명시.
- 편집 후 **즉시 relock** 또는 daemon 자동 대기.

### 하지 말아야 할 것

- **`chflags nouchg` 직접 호출 금지**. audit 에 기록되지 않음.
- `sudo chflags` 우회 금지 (기술적으로 가능하지만 ceremony 파괴).
- 여러 파일 한꺼번에 unlock 해두고 방치 금지 (window 확대).

---

## 8. FAQ

### Q1. sudo chflags nouchg 로 bypass 되는데 의미 있나?

**A.** 있다.

1. **sudo 입력 자체가 ceremony** — "지금 내가 보호를 해제한다" 는 자각을 강제.
2. **audit 부재 = 흔적 없음** — 스스로 관찰 가능한 증거가 남지 않음을 인지하게 됨.
3. **일상 실수 방어** — 대부분의 오염은 실수에서 온다. 악의적 bypass 는 소수.
4. **자기 통제 프레임** — owner 자신을 대상으로 한 friction 시스템. 외부 공격 대비가 아님.

### Q2. 재부팅하면 풀리나?

**A.** 풀리지 않는다. `chflags uchg` 는 디스크에 영구 저장된다. 재부팅 후에도 flag 유지.

### Q3. AirDrop / Time Machine / Dropbox 등 백업 시에도 flag 유지되나?

**A.** 상황에 따라 다름.
- Time Machine: macOS 네이티브 → flag 보존.
- Dropbox / rsync 기본: flag **유실**.
- `rsync -aX` + `--fileflags`: flag 보존 가능 (BSD rsync 에서).
- 따라서 **복원 후 relock 필요**.

### Q4. git clone 직후에는?

**A.** git 은 flag 를 복원하지 않는다. clone 직후에는 전부 writable. `hx_install_os.hexa` 로 초기 lock 필요.

### Q5. 왜 디렉토리 단위 lock 이 아닌가?

**A.** 디렉토리 lock 은 내부 파일 생성/삭제 전체 차단. tool/raw_audit.hexa 등 daemon 이 `.raw-audit` 에 append 하지 못해 운용 불가. 파일 단위 lock + relock daemon 조합이 현실적.

### Q6. Windows 는?

**A.** 미지원. `attrib +r` 은 read-only 이지 `EPERM` 강제 아님. 향후 필요 시 NTFS ACL + DACL 레이어 검토.

### Q7. 이 시스템이 실효 없다면 왜 있나?

**A.** "실효" 의 정의에 달렸다.
- **kernel enforcement 방어력**: 낮음 (sudo 로 뚫림).
- **실수 / autoformat / agent 방어력**: 매우 높음 (write syscall 자체가 EPERM).
- **자기 관찰 / paper trail**: 완전함 (audit chain).

---

## 9. 참고 파일 & 커밋

### 구현 파일

| 파일 | 역할 |
|---|---|
| `tool/os_level_lock.hexa` | lock/unlock 기본 명령 (`chflags uchg/nouchg` wrapper) |
| `tool/hx_unlock.hexa` | ceremony wrapper (reason 기입 강제 + audit append) |
| `tool/relock_daemon.hexa` | 300s 후 자동 재잠금 daemon |
| `tool/raw_audit.hexa` | 감사 로그 append + 해시 체인 검증 |
| `tool/emergency/raw_reset.sh` | 비상 회수 스크립트 (모든 .raw 파일 일괄 해제) |
| `.raw` | raw#1 os-lock rule 선언 (대상 파일 목록) |
| `.raw-audit` | append-only audit log (timestamp + path + reason + hash) |

### 관련 로드맵

- `raw#1 os-lock` — OS-level enforcement 진입점.
- `raw#1 P1` — relock_daemon 300s 재잠금.
- `raw#1 P2` — .raw-audit 해시 체인.
- `raw#1 P3` — emergency raw_reset 회수 경로.

### 테스트

- `hx_unlock.hexa` 경유 편집이 audit 에 기록되는지.
- `chflags nouchg` 직접 호출 시 audit 누락되는지 (의도된 검출 우회 경로).
- relock_daemon 300s 후 재잠금 여부.
- `.raw-audit` 해시 체인 변조 탐지.

---

## 10. 요약

- **이것은 vault 가 아니다. speed bump 다.**
- **방어 대상**: 실수, autoformat, AI agent 충동, 사용자 반사.
- **방어 제외**: sudo, root, kernel exploit, 의도적 악의.
- **핵심 가치**: 편집 ceremony 로 자기 관찰 유도 + audit chain 으로 paper trail 확보.
- **기술적 한계**: user-removable, macOS 전용, 파일 단위 (디렉토리 아님).
- **사용자 의무**: `hx_unlock.hexa` 경유 편집, reason 명시, 즉시 relock.

실효는 "뚫을 수 없는 금고" 에 있지 않고, **"뚫기 전에 한 번 더 멈춰 생각하게 만드는 구조"** 에 있다.

---

## 11. Enforcement Layer Ladder (raw#7 OS-enforce)

### 핵심 전제

raw#7 의 OS-level enforcement 는 **단일 메커니즘** 이 아니라 **여러 레이어의 합** 이다. 각 레이어는 자기 책임 범위 안에서만 작동하며, 한 레이어의 한계를 다음 레이어가 메우는 구조.

### 레이어 정의

| 레이어 | 메커니즘 | 책임 범위 | 한계 |
|---|---|---|---|
| **L0**  | agent-layer `pre_tool_guard` (PreToolUse hook) | 모든 hexa subprocess 진입 직전 정책 검사 (cross-platform) | hook 우회 (`--no-verify`, 직접 syscall) 가능 |
| **L0'** | macOS `sandbox-exec` + `self/sbpl/native.sb` | darwin host 에서 hexa binary 자체 file IO | SIP 서명된 binary 외 macOS 한정 |
| **L1**  | `self/native/native_gate.so` + `LD_PRELOAD` (Linux container) | hexa 가 spawn 하는 **dynamic 자식 process** (curl, wget, busybox `sh -c`, popen 외부 툴) | **hexa CLI 자체는 무영향** (아래 §11.1) |
| **L2**  | `chflags uchg` (macOS) / launchd sweep | filesystem-level — `.raw` 등 SSOT 파일 inode flag | §1~§10 에 상세 (sudo bypass 가능) |

### §11.1 — L1 의 한계: hexa CLI static musl

hexa CLI 는 `build/hexa_linux_{arm64,x86_64}` 모두 `ELF 64-bit … statically linked, stripped` 로 빌드된다. 이는 의도된 architectural choice:

- **장점**: distroless container 에 의존성 없이 단일 ELF 로 ship, glibc 버전 불일치 무관, cold start 빠름.
- **단점**: `LD_PRELOAD` 가 **dynamic loader** 단계에서 .so 를 inject 하는 메커니즘이라, static link binary 에는 inject 지점이 없다 → hexa 자체 file IO (raw_loader 의 `.raw open`, codegen 산출물 write) 는 `native_gate.so` shim 을 **우회**.

따라서 L1 은 "hexa 가 spawn 하는 dynamic 자식 process" 만 enforce 한다. hexa CLI 자신의 write 는:

- **L0** (agent-layer pre_tool_guard) 가 모든 호출을 hook 시점에 검사 — hexa 가 static 이든 dynamic 이든 무관.
- **L0'** (sandbox-exec, macOS only) 가 hexa binary 를 sandbox profile 로 감싼다 — `DYLD_INSERT_LIBRARIES` 가 SIP 로 차단되는 macOS 의 LD_PRELOAD 등가물.
- **CLI 내부 raw_loader assertion** — hexa 가 자기 자신을 검증하는 in-process check (raw#1 os-lock 위반 시 즉시 abort).

### §11.2 — 왜 굳이 L1 을 두는가

hexa 가 spawn 하는 자식 process 가 더 위험하다:
- `curl` / `wget` 은 임의 URL 에서 임의 파일을 download 가능.
- `busybox sh -c "echo … > /etc/passwd"` 같은 shell-out 은 hexa 정책 외 모든 syscall 을 자유롭게 수행.
- `popen()` 으로 띄운 외부 툴은 hexa 의 raw_loader 를 거치지 않는다.

이 자식들은 dynamic linking 이므로 `LD_PRELOAD` 가 정상 작동 → L1 의 책임 범위가 정확히 이 gap 을 cover.

### §11.3 — Defense-in-depth 지도

```
hexa CLI 호출
  ├─ [L0] pre_tool_guard hook 검사 ← cross-platform, 모든 hexa 호출
  ├─ [L0'] sandbox-exec wrap (macOS) ← hexa 자체 file IO 차단
  ├─ hexa 본체 실행 (static musl)
  │    ├─ raw_loader self-assertion ← in-process check
  │    └─ codegen / writeall syscall ← L0/L0' 로 cover (L1 우회)
  └─ spawn dynamic 자식 process (curl/wget/sh)
       └─ [L1] LD_PRELOAD native_gate.so ← VFS syscall EPERM
            └─ filesystem (target file)
                 └─ [L2] chflags uchg / launchd sweep ← inode-level
```

각 레이어 단독으로는 완전치 않다. ceremony friction (§5) 과 동일한 원리 — 조합으로 "뚫기 전에 한 번 더 멈춰 생각하게 만드는 구조" 를 형성.

### §11.4 — 구현 파일 매핑

| 레이어 | 파일 | 비고 |
|---|---|---|
| L0  | `pre_tool_guard` (agent-layer hook) | hive/.claude/hooks/pre_tool_guard 등 |
| L0' | `self/sbpl/native.sb` | macOS sandbox profile |
| L1  | `self/native/native_gate.c` → `native_gate.so` | Linux 전용, static musl 한계 §11.1 |
| L1  | `docker/runner/Dockerfile` (`ENV LD_PRELOAD`) | container 내 자동 활성 |
| L2  | `tool/os_level_lock.hexa` | §9 참조 |

### §11.5 — 미래 reader 를 위한 안내

> **Q. 왜 LD_PRELOAD enforce 가 hexa 직접 호출엔 안 되나?**
>
> **A.** hexa CLI 가 static musl link 라 dynamic loader 단계가 없다. `LD_PRELOAD` 는 dynamic loader 가 .so 를 inject 하는 메커니즘이므로 static binary 에는 적용 자체가 불가능. 이는 distroless container 호환과 빠른 cold start 를 위한 의도된 선택. hexa 자체 file IO 는 L0 (pre_tool_guard) + L0' (sandbox-exec, macOS) + in-process raw_loader assertion 으로 cover. L1 (LD_PRELOAD) 은 **dynamic 자식 process** (curl/wget/busybox `sh -c`) 의 enforce 만 책임진다.

### §11.6 — 관련 SSOT

- `hive/.raw` raw#7 entry — OS-enforce ladder 정의 (SSOT).
- `Mac→docker hard-landing policy 2026-04-25` — Linux 컨테이너만이 darwin 의 cgroup 등가물.
- `self/native/native_gate.c` 헤더 LIMITATION 블록 — 본 §11 의 in-source mirror.
- `docker/runner/Dockerfile` `ENV LD_PRELOAD` 위 주석 — container 빌드 시 same caveat.
