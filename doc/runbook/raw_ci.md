# raw#0 CI Runbook — server-side attestation enforcement

`.github/workflows/raw-verify.yml` 운영 가이드.

---

## 1. 개요

- **로컬 gate** (`./hx commit`) — raw_all 통과 후 `raw-all-attest:v1:...`
  trailer 를 commit message 에 embed. 로컬 chflags(uchg) 는 friction 수준.
- **서버 gate** (이 workflow) — PR/push 시 각 commit 의 trailer 를 HMAC 재계산
  으로 검증. trailer 가 없거나 HMAC 불일치 → CI fail → merge 차단.

**Strategy (c) — bash-only**. `hexa_stage0` 는 macOS Mach-O 바이너리라서
ubuntu-latest runner 에서 실행 불가. 따라서 CI 는 raw_all 자체를 재수행하지
않고 trailer 검증만 수행한다. 로컬에서 raw_all 을 돌렸다는 _증명_ (HMAC 서명된
token) 을 서버가 재검증하는 구조.

---

## 2. 선행 작업 — `REPO_ATTEST_KEY` secret 등록

### 2.1. 로컬 키 생성

raw_attestation.hexa 가 첫 실행 시 `.raw-attest-key` 를 자동 생성한다
(`openssl rand -hex 32`). 이미 있으면 재사용.

```bash
./hexa tool/raw_attestation.hexa generate  # 첫 호출 시 .raw-attest-key 생성
cat .raw-attest-key                         # 64-hex 문자열
```

> `.raw-attest-key` 는 `.gitignore` 에 등록되어 있어야 한다. **절대 commit 금지**.

### 2.2. GitHub Secret 등록

1. GitHub 저장소 → **Settings** → **Secrets and variables** → **Actions**
2. **New repository secret** 클릭
3. Name: `REPO_ATTEST_KEY`
4. Secret: `.raw-attest-key` 의 64-hex 내용 **그대로** 붙여넣기 (trailing newline 제외)
5. **Add secret**

### 2.3. 동기화 확인

```bash
# 최신 commit 의 attestation 을 local 에서 먼저 검증
./hexa tool/raw_attestation.hexa check HEAD
# → "attestation: OK (HEAD)"
```

push 후 GitHub Actions 탭에서 `attestation` job 이 녹색이면 secret 동기화 OK.

---

## 3. workflow 각 job

### 3.1. `attestation` (hard gate)

- `push` / `pull_request` 범위 내 각 non-merge commit 을 walk
- `tool/raw_attestation.hexa` 의 `emit-verify` 출력과 동일한 bash 스크립트를
  runner tmpfs 에 생성 (raw#8 HEXA-ONLY — repo 에 `.sh` 불커밋)
- 각 commit 에 대해 HMAC 재계산 + claimed HMAC 비교
- trailer 없음 → `missing-attestation` 에러
- HMAC 불일치 → 에러

**실패 시 대응**:

| 증상 | 원인 | 조치 |
|------|------|------|
| `REPO_ATTEST_KEY secret 미등록` | GitHub Secret 누락 | §2.2 다시 진행 |
| `missing-attestation on <sha>` | `git commit` 만으로 작업 | `./hx commit` 으로 rewrite, 또는 local 에서 `./hexa tool/raw_attestation.hexa trailer "<msg>"` 후 amend |
| `HMAC MISMATCH` | 로컬 `.raw-attest-key` 와 GitHub secret 불일치 | §2.2 재등록 (둘 다 같은 값) |
| `SSOT drift` (warn) | PR 중 `.raw`/`.own`/`.ext` 수정 | warn 만 — fail 하지 않음 |

### 3.2. `raw_all` (skipped — note-only)

현재 strategy (c) 에서는 Linux runner 에서 `hexa_stage0` 가 돌지 않아 skip.
`attestation` job 이 로컬 raw_all 통과를 간접 증명한다.

- 향후 Linux cross-build 가 준비되면 strategy (a) 로 전환:
  - `tool/rebuild_stage0.hexa` 를 Linux target 으로 수정
  - artifact cache (`actions/cache@v4`) 로 build 재사용
  - `./build/hexa_stage0 tool/raw_all.hexa` 실행

### 3.3. `own_inherit` (skipped — note-only)

`raw_all` 이 `own_inherit_lint` 를 포함하므로 attestation trailer 가 간접 gate.

### 3.4. `regression` (soft gate)

`test/regression/raw_*.hexa` fixture 가 있으면 목록 출력, 없으면 warn.
`continue-on-error: true` — test-suite agent 병행 작업 중이라 fixture 부재를
허용. fixture 실제 실행은 stage0 부재로 skip (attestation 으로 간접 검증).

---

## 4. 개발자 워크플로우

### 4.1. 정상 경로

```bash
# 변경 작업...
./hx verify          # raw_all 로컬 실행
./hx commit -m "..." # 통과 시 trailer 자동 embed → git commit
git push
# GitHub Actions → attestation job 녹색 확인
```

### 4.2. attestation 없는 commit 을 뒤늦게 발견했을 때

```bash
# local 에서 trailer 재생성 + amend
./hexa tool/raw_attestation.hexa trailer "$(git log -1 --format=%B)" > /tmp/msg
git commit --amend -F /tmp/msg
git push --force-with-lease   # 주의: 공유 브랜치면 팀원에게 알림
```

또는 여러 commit 에 걸쳐 있으면:

```bash
git rebase -x './hexa tool/raw_attestation.hexa trailer "$(git log -1 --format=%B)" | git commit --amend -F -' <base>
```

### 4.3. `REPO_ATTEST_KEY` 회전 (rotate)

1. 로컬: `rm .raw-attest-key && ./hexa tool/raw_attestation.hexa generate`
2. GitHub: §2.2 로 secret 업데이트
3. **주의**: 기존 commit 의 trailer 는 구 key 로 서명되어 있으므로,
   rotation 이후 열려있는 PR 은 trailer 재생성 (§4.2) 필요.
4. 가능하면 release 직후에만 rotate.

---

## 5. 보안 주의사항

- `.raw-attest-key` 는 **repo-local secret** — 어느 파일에도 echo/log 금지.
- workflow 로그에 key 를 출력하는 `echo $REPO_ATTEST_KEY` 같은 step 추가 금지
  (GitHub Actions 는 masking 하지만 의존하지 말 것).
- fork 에서 오는 PR 은 secret 에 접근 불가 — 이는 설계상 의도 (fork 공격 차단).
  신뢰된 기여자의 PR 만 attestation job 이 정상 동작한다. 이는
  raw#0 의 permission model 과 일치 — 외부 기여자는 maintainer 가 trailer 붙여
  rebase 후 merge.
- HMAC 입력은 `${ssot_sha}|${ts}` — ssot fingerprint 는 `.raw .own .ext
  .roadmap .loop` 의 concat sha256. key 길이 = 64 hex (256-bit).

---

## 6. 참고 파일

- `.github/workflows/raw-verify.yml` — workflow 정의
- `tool/raw_attestation.hexa` — token 포맷, emit-verify 정본
- `tool/raw_all.hexa` — 로컬 gate (CI 는 재실행하지 않음)
- `.raw` line 96 — `.github/workflows/` 는 허용 (`copilot-instructions.md` 만 금지)
- `doc/runbook/raw_install.md` — 초기 설치 가이드
- `doc/runbook/raw_emergency.md` — lock 해제 절차
