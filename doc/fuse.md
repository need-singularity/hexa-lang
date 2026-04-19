# Phase 3 — FUSE raw-fs 레벨 write intercept

> raw 로드맵 Phase 3 — `git-hook` / `user-hook` 의존 완전 제거.
> 모든 파일 write 가 hexa-native FUSE 레이어를 통과 → 위반 시 OS EACCES.

## 목적

Phase 1 (git-hook) + Phase 2 (compile-time) 은 사용자가 `git commit` 또는 `hexa` 를 호출할 때만 법 적용.  
**어떤 editor / tool / syscall 이든** 통과하는 raw-fs 레벨 law 가 Phase 3 목표.

- Claude Write tool → FUSE layer → open() EACCES (raw 위반 시)
- `vim` / `sed` / 수동 echo → 동일 차단
- `git add` 에서 위반 파일 → 역시 차단 (차상위 동작)

## 아키텍처

```
┌─────────────────────────────────┐
│  application (claude / vim / …) │
└────────────┬────────────────────┘
             │  write(fd, buf, n)
             ▼
┌─────────────────────────────────┐
│  hexa-fuse mount point          │  ← /path/to/repo (overlay)
│  ├ open/write hook              │
│  │    └ law_check(path, content)│
│  │        ├ rule 3 ← @attr 네이밍
│  │        ├ rule 4 ← 폴더명
│  │        ├ rule 5 ← 트리구조
│  │        └ rule 6 ← 파일명
│  └ violation → EACCES           │
└────────────┬────────────────────┘
             │  pass-through
             ▼
┌─────────────────────────────────┐
│  underlying fs (ext4 / APFS)    │
└─────────────────────────────────┘
```

## 구현 경로

### Linux (fuse3)

- `libfuse3` C API 직접 사용 (hexa FFI 로 바인딩)
- 또는 `fuser` (Rust crate) 참고 후 hexa 자체 바인딩
- mount 시 1회 `fusermount3 -u` 언마운트 필요

### macOS (FSKit / macFUSE)

- macFUSE 는 kernel extension — SIP 비활성 필요 (파괴적)
- **FSKit** (macOS 14+) — user-space filesystem, SIP 호환, 2026 사용 권장
- 또는 `nullfs`/`unionfs` 수준의 간단 overlay 로 대체

### 공통 우회 가능성

- `sudo` 사용자는 FUSE 바이패스 가능 (mount 상위로 직접 접근)
- 해결: `chattr +i` 와 조합하여 raw-fs + OS immutable 적층

## 구현 범위 (최소 viable)

### 1차 PoC (하나 rule, Linux only)

- libfuse3 바인딩 via FFI
- `rule 6 파일명` 만 검사 (F1/F2/F3 substring 매칭)
- violation → `-EACCES` 반환
- 200-300 LOC

### 2차 (full 6 rules, Linux + macOS FSKit)

- 3 / 4 / 5 / 6 모두 적용
- hexa 재사용: `self/raw_cli.hexa check` 내부 호출
- overlay 전용 (repo 외부 경로는 pass-through)
- 500-800 LOC

### 3차 (hexa 네이티브 FUSE)

- hexa 로 libfuse 완전 포팅 (no C dep)
- self-hosting 완성도 ↑
- 수 주 작업

## 파일 구조 (예정)

```
self/fs/
  fuse.hexa              ← libfuse3 바인딩 + main loop
  law_intercept.hexa     ← open/write 훅, raw check 호출
  mount_cli.hexa         ← hexa fs mount / umount / status
tool/
  fuse_install.hexa      ← libfuse3 탐지 + fstab 스니펫 제안 (선택)
```

## 성능 / 오버헤드

- 모든 write 가 user-space 경유 → ~10-50% 페널티 전형
- 검사 대상 제한 (`.hexa` / `.json` / `.md` 만) 로 완화 가능
- kernel-bypass 옵션 (eBPF LSM) 은 Phase 3.5

## 의존 최소화 판정

| 요소 | 의존도 |
|------|--------|
| libfuse3 (linux) | OS 표준 패키지 (apt install fuse3) |
| FSKit (macos) | OS 내장 (14+) |
| hexa 자체 FFI | ✅ |
| kernel module | ❌ (FSKit 사용 시 불필요) |

→ **Phase 3 구현 시 의존 = OS FUSE 표준만**. git / Claude Code / 외부 hook 전부 제거.

## 위험 / 주의

1. **마운트 포인트 유실**: 시스템 reboot / 크래시 후 `hexa fs mount` 수동 실행 필요
2. **CI/CD**: CI 환경에서 FUSE 불가 → pre-commit safety net 유지 필수 (폐기 X)
3. **루트 우회**: `sudo` 로 언더라잉 FS 직접 접근 가능 — `chattr +i` 와 적층 필요
4. **IDE 호환**: VS Code / vim / emacs 등 file watcher 가 FUSE stat 반복 호출 → 로그 노이즈

## 대체 경로 (Phase 3 대신)

FUSE 복잡도 회피:
- **Phase 3-alt**: `inotify` (linux) / `fsevents` (macos) 로 **사후 감지 + 자동 revert** (race 위험 있음)
- **Phase 3-alt-2**: `LD_PRELOAD` 로 open(2) 훅 (glibc 의존, per-process)

## 다음 단계

1. libfuse3 apt install → PoC skeleton (1-2 일)
2. PoC 에 rule 6 만 적용 → 수동 테스트
3. hexa_init install 에 "FUSE 선택 옵션" 추가
4. 성능 측정 (hot compile 경로)
5. 정식 릴리스

Phase 2 → Phase 3 이행은 **필수 아님** — pre-commit + compile gate 만으로 80-90% 커버. FUSE 는 추가 안전망.
