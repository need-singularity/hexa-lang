---
status: draft
related-raw-rule: 17
related-roadmap: [12, 15, 19]
date: 2026-04-20
---

# raw#17 Rebootstrap Ceremony Runbook

self-host fixpoint (`.roadmap#12` v3==v4 byte-identical) 이 달성된 뒤
**의도적으로** 컴파일러 출력이 바뀌어야 하는 경우 (언어 문법 확장, codegen
재작성, 런타임 ABI 변경 등) 의 공식 절차. drift (실수로 깨짐) 와 구분해
paper trail 을 남기는 "rebootstrap ceremony" 를 정의한다.

Bilingual (Korean primary, English inline) — matches `doc/runbook/raw_*.md`
convention.

## 1. Overview — 의도적 fixpoint 변경이란?

raw#17 은 `v2→v3` byte-identical + `v3→v4` stable 을 요구한다. 일단 fixpoint
가 한 번 관찰되면 `tool/verify_fixpoint.hexa` 는 이후 매 커밋마다 동일
SHA256 을 기대한다. 그런데 진짜로 출력이 바뀌어야 하는 변경도 있다:

- **Lexer / parser 확장** — 신규 keyword, operator precedence 조정
- **AST 스키마 추가** — 신규 node kind, deserializer 변경
- **Codegen 재작성** — 레지스터 할당 알고리즘 교체, 신규 target
- **Runtime ABI 변경** — calling convention, struct layout 수정
- **Self-host stage 변경** — stage0 교체 (`.roadmap#13`)

이 경우 **모든 hexa 바이너리의 SHA256 이 통째로 달라진다**. drift 로 오인되면
raw#17 이 commit 을 거부한다 (`.roadmap#16` live 승격 후). rebootstrap 은
이 상황을 **공식 이벤트로 선언**하고, 새 fixpoint 값을 기록·서명·봉인하는
절차다.

## 2. vs. drift — 둘을 구분하는 원칙

| 축              | drift (실수)                              | rebootstrap (의도)                        |
| :-------------- | :---------------------------------------- | :---------------------------------------- |
| 트리거          | 날짜/PID/iter-order 우연 포함             | 설계 문서에 선언된 의도적 변경            |
| Design doc      | 없음                                      | `doc/plans/rebootstrap_<reason>_<date>.md` 선행 |
| Actor intent    | "왜 바뀌었지?" (모름)                     | "X 를 위해 바꾼다" (명시)                 |
| Audit entry     | `violation` / `fail`                      | `REBOOTSTRAP by <actor>: reason=...`      |
| Approval        | 필요 없음 (자동 reject)                   | 2-person review + release tag             |
| Frequency       | 0 goal                                    | 연 1~2회 이하                             |
| 복구            | 원인 수정 + 재측정                        | 새 fixpoint 봉인 + archive                |

핵심: **출력 바이트가 바뀐 것만으로는 둘을 구분할 수 없다.** 차이는 전적으로
paper trail (design doc + audit + tag) 의 존재 여부다. ceremony 를 밟지
않은 byte change 는 정의상 drift 이며 `.roadmap#16` 하에서 commit 이
막힌다.

## 3. Ceremony — 7 Steps

rebootstrap 은 아래 7 단계를 **순서대로** 완료해야 한다. 중간 단계 스킵 금지
— audit 에서 missing step 이 발견되면 next rebootstrap 때 `--strict` 검사가
이전 ceremony 를 reject 한다.

### Step 1 — Design doc 작성

```bash
# 변경 이유·범위·예상 SHA shift 를 사전 기술
cp doc/plans/TEMPLATE_rebootstrap.md \
   doc/plans/rebootstrap_<reason>_<YYYYMMDD>.md
$EDITOR doc/plans/rebootstrap_<reason>_<YYYYMMDD>.md
```

문서 필수 섹션:

- [ ] **Motivation** — 왜 byte 가 달라져야 하는가
- [ ] **Scope** — 영향받는 파일 (`self/`, `tool/`, `stdlib/`, runtime ABI)
- [ ] **Non-goals** — 이 기회에 묻어가지 **않을** 변경들
- [ ] **Expected SHA shift** — v3/v4 둘 다 바뀌는가, v4만 바뀌는가
- [ ] **Rollback plan** — 실패 시 archive 에서 어떻게 복구할지
- [ ] **Reviewer sign-off** — 2인 이상 명시 (raw#17 ceremony 요건)

### Step 2 — Test suite 전면 PASS 확인

Rebootstrap 전, **현재 fixpoint** 에서 전체 regression 이 깨끗한지 확인.
새 baseline 은 과거의 green state 위에서만 봉인한다.

```bash
# 1) raw_all full — 규칙 위반 없음
./build/hexa_stage0 tool/raw_all.hexa
# expected: exit 0

# 2) verify_fixpoint — 구 fixpoint 재확인 (마지막 green SHA)
./build/hexa_stage0 tool/verify_fixpoint.hexa
# expected: SHA256(hexa_2) == SHA256(hexa_3) == <구 fixpoint SHA>

# 3) native regression — 있으면 전부
./build/hexa_stage0 tool/fixpoint_check.hexa
# expected: exit 0

# 4) test/ directory 전체 (프로젝트별)
./build/hexa_stage0 tool/run_tests.hexa  # 있으면
```

하나라도 FAIL 이면 **ceremony 중단**. 먼저 기존 fixpoint 에서 녹색을
확보한 뒤 재시작.

### Step 3 — New fixpoint 측정 + hash 기록

변경된 소스로 **3-stage pipeline** 을 재수행해 새 fixpoint 를 확정.

```bash
# 깨끗한 build 디렉토리에서 재측정 (캐시 오염 방지)
rm -rf build/.fixpoint-scratch
mkdir -p build/.fixpoint-scratch

./build/hexa_stage0 tool/verify_fixpoint.hexa --scratch build/.fixpoint-scratch
# expected output (JSON):
# {
#   "hexa_1_sha": "...",
#   "hexa_2_sha": "aaaa...",
#   "hexa_3_sha": "aaaa...",       ← 새 fixpoint
#   "converged": true
# }

# hash 를 파일로 저장 (Step 5 에서 씀)
./build/hexa_stage0 tool/verify_fixpoint.hexa --print-hash \
    > /tmp/new-fixpoint.sha256
```

`converged: false` 면 이번 변경이 **자체적으로 non-deterministic** 이라는
뜻 — 먼저 `doc/runbook/raw_determinism.md` 체크리스트를 돌려 원인을 제거한
뒤 재시도. 그 상태로 봉인하면 안 된다.

### Step 4 — Archive freeze — 구 fixpoint 보존

새 값으로 덮어쓰기 **전에** 구 fixpoint snapshot 을 `~/.hexa-fixpoint-archive/`
로 이관하고 immutable 화한다.

```bash
# 아카이브 루트 (유저 홈 아래, repo 밖)
ARCH=~/.hexa-fixpoint-archive
mkdir -p "$ARCH"

# 이번 rebootstrap 의 slot
SLOT="$ARCH/$(date -u +%Y%m%dT%H%M%SZ)_pre_<reason>"
mkdir -p "$SLOT"

# 구 fixpoint artifacts 복사
cp build/hexa_stage0                  "$SLOT/stage0.bin"
cp .raw-fixpoint-hash                  "$SLOT/fixpoint.sha256"
cp .raw-audit                          "$SLOT/audit.log"
git rev-parse HEAD                     > "$SLOT/HEAD.sha"
git log -1 --pretty=fuller             > "$SLOT/HEAD.meta"

# chflags uchg 로 immutable 화 (Darwin)
#   Linux: chattr +i 대체
if [ "$(uname)" = "Darwin" ]; then
    sudo chflags -R uchg "$SLOT"
else
    sudo chattr -R +i "$SLOT" 2>/dev/null || true
fi

# 상위 index 갱신 (append-only)
printf '%s | pre-rebootstrap | %s | %s\n' \
    "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "<reason>" "$SLOT" \
    >> "$ARCH/index.log"
```

주의:

- Archive 는 **repo 밖** (`~/.hexa-fixpoint-archive/`) 에 둔다 — repo 잠김
  / 이동 / 삭제와 독립.
- `chflags uchg` 는 user 수준 immutable — root 없이 풀 수 있음. 더 강한
  봉인이 필요하면 `chflags schg` (`.roadmap#13` 이후 권장).
- Linux 에서 `chattr +i` 가 실패해도 ceremony 는 계속 — WARN 후 index 에
  `no-immutable` flag 만 기록.

### Step 5 — `.raw-fixpoint-hash` 갱신

실제 SSOT-adjacent hash 파일을 unlock → edit → lock 사이클로 갱신.

```bash
# 1) unlock ceremony
./build/hexa_stage0 tool/hx_unlock.hexa \
    --reason "rebootstrap: <reason> — SHA shift from <old> to <new>"

# 2) 새 hash 적용
cp /tmp/new-fixpoint.sha256 .raw-fixpoint-hash

# 3) verify (raw_all 은 raw#17 enforce 로 새 hash 를 사용)
./build/hexa_stage0 tool/raw_all.hexa --only raw 17
# expected: [raw#17 live] PASS tool/verify_fixpoint.hexa

# 4) re-lock
./build/hexa_stage0 tool/hx_lock.hexa
```

`.raw-fixpoint-hash` 는 `.raw` 와 별도 파일이지만 raw#17 enforcer 가 참조
하므로 unlock 대상에 포함된다 (`tool/os_level_lock.hexa` 의
`_optional_targets()` 에 등재되어 있음 — 2026-Q3 이후).

### Step 6 — Audit log append

```bash
./build/hexa_stage0 tool/raw_audit.hexa append \
    --event rebootstrap \
    --actor "$USER" \
    --reason "<reason>: SHA shift <old-short>..<new-short>" \
    --result ok
```

결과 (`.raw-audit` 한 줄):

```
2026-04-20T03:14:22Z | rebootstrap | ghost | syntax: add raw-string literal — SHA shift a3f…→b7c… | ok | a3f… | b7c…
```

수동 append 가 필요하면 (`raw_audit.hexa` 가 깨진 경우):

```bash
./build/hexa_stage0 tool/hx_unlock.hexa --reason "manual audit append"
printf 'REBOOTSTRAP by %s: reason=%s | %s\n' \
    "$USER" "<reason>" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
    >> .raw-audit
./build/hexa_stage0 tool/hx_lock.hexa
```

### Step 7 — Release tag + attestation trailer

```bash
# 1) ceremony 전체를 묶는 tag
git tag -a v<X.Y.Z> -m "$(cat <<'EOF'
rebootstrap: <reason>

- old fixpoint: <old-sha256-full>
- new fixpoint: <new-sha256-full>
- design doc:   doc/plans/rebootstrap_<reason>_<date>.md
- archive slot: ~/.hexa-fixpoint-archive/<timestamp>_pre_<reason>/
- reviewers:    <name1>, <name2>

raw-all-attest: v1:<hmac>@<iso-utc>
EOF
)"

# 2) 최종 commit 은 반드시 ./hx 경유 (trailer 자동 부착)
./build/hexa_stage0 tool/hx.hexa commit -m "rebootstrap(raw#17): <reason>"

# 3) tag push 는 별도 — release 가 준비됐을 때만
git push origin v<X.Y.Z>
```

Tag message 의 attestation trailer (`raw-all-attest:`) 는 `tool/hx.hexa` 가
자동 부착한다. **bare `git tag` 는 금지** — trailer 없는 tag 는 server-side
pre-receive hook (`raw-verify.yml` 의 확장) 이 reject 할 수 있다.

## 4. Frequency target — 연 1~2회 이하

rebootstrap 은 **희귀 이벤트**여야 한다. 기준:

| 빈도            | 해석                                                 |
| :-------------- | :--------------------------------------------------- |
| 연 0회          | 이상적 (언어가 stable)                               |
| 연 1~2회        | 건강한 evolution                                     |
| 분기 1회 이상   | 경고 — design 이 수렴하지 않았거나 drift 를 rebootstrap 으로 위장 중 |
| 월 1회 이상     | raw#17 의 의미 붕괴 — ceremony 가 formality 로 전락 |

월 1회 임계를 넘으면 `.roadmap#19` 가 재검토를 요구하고, `.roadmap#16`
live → warn 강등을 권고한다. 빈번한 변경이 정당하면 rule 자체를 재설계.

## 5. Rejection criteria — rebootstrap 불승인 경우

Design doc reviewer 는 아래 조건 중 **하나라도** 해당하면 ceremony 를
거부해야 한다:

- [ ] **기술부채 청산만 목적** — 새 기능 없이 "코드 정리" 로 byte shift
      유도. 작은 refactor 는 fixpoint 안에서 하라.
- [ ] **의도 불명확** — "빌드 했더니 바뀌었는데 rebootstrap 하자" ←
      drift 를 위장하는 전형. 원인부터 규명.
- [ ] **non-determinism 미해소** — Step 3 에서 `converged: false` 인
      채로 진행 요청. `raw_determinism.md` 체크리스트 선행 필수.
- [ ] **archive 건너뛰기** — "구 fixpoint 는 어차피 git 에 있으니까"
      ← archive 는 chflags-immutable 사본을 요구 (git 은 writable).
- [ ] **single reviewer** — 2인 이상 sign-off 미달. 단독 rebootstrap
      금지 (raw#17 의 설계 의도).
- [ ] **30일 cool-down 미충족** — 직전 rebootstrap 으로부터 30일 이내
      2차 rebootstrap 시도. 배치 merge 후 재개.

거부 시 actor 에게 reason 을 명시하고 design doc 에 `REJECTED: <reason>`
섹션을 append 하여 history 를 남긴다.

## 6. Rollback — 실패 시 복구 절차

Step 5 이후 test suite 가 깨졌거나 downstream consumer 가 붕괴한 경우:

```bash
# 1) archive 에서 구 fixpoint slot 확인
ARCH=~/.hexa-fixpoint-archive
ls -lt "$ARCH" | head -5
# 가장 최근 pre-rebootstrap slot 선택

# 2) SSOT unlock (ceremony actor 권한)
./build/hexa_stage0 tool/hx_unlock.hexa --reason "rollback rebootstrap <reason>"

# 3) 구 hash 복원
SLOT="$ARCH/<timestamp>_pre_<reason>"
if [ "$(uname)" = "Darwin" ]; then
    sudo chflags nouchg "$SLOT/fixpoint.sha256"
else
    sudo chattr -i "$SLOT/fixpoint.sha256" 2>/dev/null || true
fi
cp "$SLOT/fixpoint.sha256" .raw-fixpoint-hash

# 4) (옵션) 구 stage0 binary 도 복원
cp "$SLOT/stage0.bin" build/hexa_stage0
chmod +x build/hexa_stage0

# 5) verify
./build/hexa_stage0 tool/verify_fixpoint.hexa
./build/hexa_stage0 tool/raw_all.hexa --only raw 17

# 6) re-lock + audit
./build/hexa_stage0 tool/hx_lock.hexa
./build/hexa_stage0 tool/raw_audit.hexa append \
    --event rebootstrap \
    --actor "$USER" \
    --reason "ROLLBACK of <reason>: restored from $SLOT" \
    --result ok

# 7) git tag 는 삭제하지 말고 별도 rollback tag 추가
git tag -a v<X.Y.Z>-rollback -m "rollback rebootstrap: <reason>"
```

Archive slot 을 **절대 삭제하지 말 것** — 다음 ceremony 에서 이전 두
fixpoint 를 전부 소급 검증해야 할 수 있다. 용량이 부담이면 stage0 binary
만 제외하고 해시/로그는 유지.

## 7. Audit example — 로그 한 줄 예시

`.raw-audit` 에 남는 정상적 rebootstrap 이벤트 예시:

```
2026-07-15T09:42:11Z | rebootstrap | ghost | syntax: raw-string literal r"..." | ok | a3f2b8…c1 | b7cd0e…42
2026-07-15T09:42:14Z | rebootstrap-archive | ghost | slot=~/.hexa-fixpoint-archive/20260715T094211Z_pre_rawstr | ok | - | -
2026-07-15T09:42:20Z | rebootstrap-tag | ghost | v0.12.0 | ok | b7cd0e…42 | b7cd0e…42
```

Rejected 예시 (design doc reviewer 가 거부):

```
2026-07-20T11:05:00Z | rebootstrap-rejected | mallory | reason="tech debt cleanup only" | blocked | - | -
```

Rollback 예시:

```
2026-07-18T02:30:00Z | rebootstrap-rollback | ghost | slot=...20260715T094211Z_pre_rawstr | ok | b7cd0e…42 | a3f2b8…c1
```

## 8. Checklist — ceremony 시작 전후

시작 **전**:

- [ ] Design doc 작성 (`doc/plans/rebootstrap_<reason>_<date>.md`)
- [ ] 2인 이상 reviewer sign-off 확보
- [ ] 30일 cool-down 확인 (직전 rebootstrap 시점 대비)
- [ ] 현 fixpoint 에서 regression 전부 green
- [ ] `~/.hexa-fixpoint-archive/` 쓰기 권한 / 충분한 디스크

ceremony **중**:

- [ ] Step 1~7 를 **순서대로** 진행
- [ ] Step 3 `converged: true` 확인 (false 면 중단)
- [ ] Step 4 archive slot 이 immutable 인지 `ls -lO` / `lsattr` 로 검증

**후**:

- [ ] `./hx status` — audit 에 `rebootstrap` 이벤트 표시
- [ ] `git tag -l v*` — release tag 존재
- [ ] 30일 관찰 — drift 없이 새 fixpoint 유지되는지

## 9. 관련 파일 / Related files

- `.raw` raw#17                  — self-host fixpoint convergence rule
- `.roadmap#12 / #15 / #16 / #19` — P7-7 fixpoint, new→warn→live, ceremony design
- `tool/verify_fixpoint.hexa`    — v2→v3→v4 SHA256 verifier
- `tool/fixpoint_compare.hexa`     — declarative fixpoint declaration
- `tool/fixpoint_check.hexa`— raw#17 proof carrier
- `tool/raw_audit.hexa`          — `.raw-audit` append gate
- `tool/hx.hexa`                 — `./hx commit / tag` wrapper
- `doc/runbook/raw_determinism.md` — ceremony 진입 조건 체크리스트
- `doc/runbook/raw_emergency.md` — SSOT 복구 (rebootstrap 과 **별개**)
- `doc/plans/p7_7_fixpoint_design_20260419.md` — P7-7 설계 근거
- `doc/plans/raw17_convergence_add_20260420.md` — raw#17 추가 패치 log
