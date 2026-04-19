# raw#0 Multi-Repo `.raw` Canonical Sync Runbook

`.raw` 는 **cross-project L0 universal rule** — 모든 Hexa-family repo 가
동일하게 따라야 하는 불변 규칙 집합. 반면 `.own`, `.ext`, `.roadmap`,
`.loop` 는 per-repo (프로젝트마다 고유). 본 runbook 은 *여러 repo* 간
`.raw` drift 를 감지하고 동기화하는 워크플로를 설명한다.

Bilingual (Korean primary, English inline).

Related:
- `tool/raw_sync.hexa` — CLI 구현
- `.raw-ref.example` — follower repo 템플릿
- `doc/runbook/raw_install.md` — raw#0 전체 설치
- `doc/runbook/raw_daily_flow.md` — 일상 편집 (single repo)

## 1. Canonical Model — 왜 hexa-lang 이 단일 SOT 인가

| SSOT file | scope | multi-repo 취급 |
|-----------|-------|-----------------|
| `.raw`    | universal laws | **shared** — canonical = hexa-lang |
| `.own`    | project identity | **per-repo** — 각자 관리 |
| `.ext`    | extensions | **per-repo** |
| `.roadmap`| phases | **per-repo** |
| `.loop`   | scheduled tasks | **per-repo** |

`.raw` 만 공유인 이유:
1. 언어 설계 불변성 (indent, quote, comment, banned constructs) 은
   프로젝트에 독립적이어야 한다 — 한 repo 에서 `indent=4` 로 바뀌면
   코드 포터빌리티가 파괴.
2. L0 rule 은 구성원 간 **동등 보호** (equal protection) 를 요구 —
   어느 프로젝트도 자기 편의로 rule 을 약화시키면 안 됨.
3. `.own/.ext/.roadmap/.loop` 는 프로젝트의 정체성/로드맵 — 동일성
   강제가 오히려 해롭다.

따라서 `.raw` 변경은 **canonical repo (hexa-lang) 에서만** 이루어지고,
다른 repo 는 pull-only follower.

## 2. Follower Repo 등록 — 신규 프로젝트가 canonical 추종 시작

canonical 의 `.raw` 를 복제한 뒤 본인 repo root 에:

```bash
# 1) 템플릿 복사
cp /path/to/hexa-lang/.raw-ref.example .raw-ref

# 2) 첫 sync — pinned-hash 자동 채움
./hexa tool/raw_sync.hexa check

# 3) 확인
./hexa tool/raw_sync.hexa status
# 기대: SYNCED
```

`.raw-ref` 예:

```
ref 1 live "hexa-lang canonical"
  source github.com/need-singularity/hexa-lang
  branch main
  path .raw
  pinned-hash 3f4a...e91c
  checked-at 2026-04-20T12:34:56Z
```

NOTE: `.raw-ref` 자체는 per-repo 파일 — commit 해도 무방하다 (오히려
추적 감사 용도로 권장).

## 3. Daily Drift Check — 정기 점검

### 3.1 수동

```bash
./hexa tool/raw_sync.hexa status
```

- `SYNCED` — 아무 조치 불필요.
- `DRIFT` — 로컬 `.raw` 가 pinned-hash 와 다름. 로컬 편집 실수 or
  canonical 이 갱신됨. `check` 로 원인 확인.
- `UNINITIALIZED` — pinned-hash 가 템플릿 값. 최초 `check` 필요.
- `NO_REF` — 본 repo 는 canonical 추종자가 아님 (hexa-lang 자체 또는
  독립 fork).

### 3.2 자동 (crontab)

```crontab
# 매일 09:00 drift 감지; DRIFT 시 메일 알림
0 9 * * * cd ~/projects/my-repo && \
  ./hexa tool/raw_sync.hexa check 2>&1 | \
  mail -s "raw drift: my-repo" -E me@example.com
```

또는 CI 에 주간 cron job:

```yaml
# .github/workflows/raw-drift.yml (follower repo)
on:
  schedule:
    - cron: "0 9 * * 1"   # 매주 월 09:00
jobs:
  drift:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: ./build/hexa_stage0 tool/raw_sync.hexa check
```

`check` 는 BEHIND 시 exit 1 — CI 가 자동으로 red 된다.

## 4. Pull — canonical 반영

```bash
# 1) unlock (chflags uchg 해제) — .raw 는 기본 잠겨 있음
./hx unlock --reason "sync .raw from canonical"

# 2) pull — backup + overwrite + .raw-ref 갱신 + audit append
./hexa tool/raw_sync.hexa pull

# 3) 재 lock
./hx lock

# 4) commit
./hx commit -m "sync(raw#0): pull canonical .raw"
```

`pull` 은 다음을 자동 수행:
- `.raw.bak.YYYYMMDD_HHMMSS` 백업
- canonical `.raw` 로 덮어쓰기
- `.raw-ref` 의 `pinned-hash` + `checked-at` 갱신
- `.raw-audit` 에 `sync-pull | <user> | canonical pull from ...` 기록

WARNING: `.raw` 가 chflags uchg 인데 unlock 을 안 했다면 `pull` 은
친절한 에러로 중단한다 — 데이터 손실 없음.

## 5. Push — 역방향 제안 (희귀)

follower repo 에서 canonical 에 rule 을 제안해야 할 때 (거의 없음):

```bash
./hexa tool/raw_sync.hexa push
```

이 명령은 **아무것도 실행하지 않는다**. 대신 수동 수행할 `git` / `gh`
명령 시퀀스를 출력한다:

```
git clone https://github.com/need-singularity/hexa-lang /tmp/canonical-<ts>
cp .raw /tmp/canonical-<ts>/.raw
cd /tmp/canonical-<ts>
./hx unlock --reason "sync .raw from follower"
./hx lock
./hx commit -m "sync(raw#0): update from follower repo"
gh pr create --title "raw sync" --body "..."
```

원칙: **canonical 은 hexa-lang 에서 먼저 진화**한다. follower 가 먼저
바뀌면 일단 로컬에서 검증 후, 인간 리뷰 프로세스로 PR 올리는 게
맞다. 자동화는 일부러 하지 않는다.

## 6. Status / Diff Interpretation

| command | SYNCED | DRIFT | BEHIND | IN_SYNC | NO_REF |
|---------|--------|-------|--------|---------|--------|
| status  | local==pinned | local!=pinned | — | — | no .raw-ref |
| check   | — | — | local!=remote | local==remote | no .raw-ref |

`status` 는 **network 없음** — pinned-hash 만 비교.
`check` 는 **network 필요** — curl 로 canonical 다운로드 후 diff.

## 7. Edge Cases

### 7.1 canonical repo 자신 (hexa-lang)
`.raw-ref` 를 두지 않는다. `raw_sync.hexa status` 실행 시 `NO_REF` 출력
후 exit 2 — intentional.

### 7.2 fork 후 divergence 허용
일시적으로 canonical 과 다르게 가야 한다면 `.raw-ref` 의 status 를
`paused` 로 변경. `status` 는 여전히 hash 비교하지만 이 때의 DRIFT 는
"의도된 것" 이라 해석하면 된다. 영구적이면 `.raw-ref` 삭제 → 독립 fork.

### 7.3 pinned-hash 첫 채움
템플릿의 `TO_BE_FILLED_ON_FIRST_SYNC` 는 `check` 또는 `pull` 후
자동 치환된다. 처음 드라이 런:
```bash
./hexa tool/raw_sync.hexa check   # pinned 자리는 empty 상태 — UNINITIALIZED 표시
./hexa tool/raw_sync.hexa pull    # 실제 덮어쓰기 + pinned 채움
```

### 7.4 canonical 이 hash-identical 일 때
`pull` 이 사전 체크에서 `already IN_SYNC — nothing to pull` 출력 후
no-op 으로 끝난다 — 단, `.raw-ref` 의 `checked-at` 은 새 timestamp 로
갱신된다 (drift 모니터링 최신화).

## 8. Security

- `pull` 은 **network 로 받은 콘텐트로 `.raw` 덮어쓰기** — GitHub
  raw URL 에 의존. MITM 방지를 위해 TLS cert 검증 (`curl -fsSL`) 은
  기본 on.
- 추가 보안: `.raw-ref` 에 `pinned-hash` 를 기록해두므로 `pull` 직후
  다시 `status` 로 검증 가능.
- 적대적 canonical 변조 대비: 정책적으로 canonical repo push 권한을
  신뢰된 maintainer 로 제한하고, `tool/raw_attestation.hexa` HMAC
  키를 CI secret 으로 관리.

## 9. FAQ

**Q. `.own` / `.ext` 도 공유하면 안 되나?**
A. No. `.own` 은 프로젝트 identity — 공유하면 각 repo 의 고유성 상실.

**Q. 한 follower 가 여러 canonical 을 추종할 수 있나?**
A. 이론적으로는 여러 `ref` 블록 허용 — 현재 구현은 첫 `live` ref 만
사용. 다중 canonical 시나리오는 설계상 discouraged.

**Q. pull 후 raw_all 이 깨진다면?**
A. canonical 쪽이 불량 — 백업 파일 (`.raw.bak.*`) 로 즉시 복구
가능. canonical maintainer 에게 리포트.
