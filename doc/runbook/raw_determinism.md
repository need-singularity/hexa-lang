---
status: draft
related-raw-rule: 17
related-roadmap: [12, 15, 19]
date: 2026-04-20
---

# raw#17 Determinism Audit Runbook

self-host fixpoint (raw#17 — `v2→v3` byte-identical + `v3→v4` stable) 의
**전제조건**은 컴파일 파이프라인 전체가 결정적 (deterministic) 이어야 한다는
것. 한 군데라도 시계·PID·iteration order 가 새어 들어가면 v3 와 v4 의 SHA256
는 매번 다르게 찍히고 fixpoint 자체가 성립하지 않는다. 본 runbook 은 **어떤
항목이 non-determinism 을 만드는지**, 현재 repo 에서 어디가 걸리는지, 어떻게
고치는지를 체크리스트로 정리한다.

Bilingual (Korean primary, English inline).

## 1. Why determinism — raw#17 의 전제

Fixpoint 정의 자체가 "같은 input → 같은 output" 을 요구한다:

```
hexa_2 = compile(hexa_1, source)
hexa_3 = compile(hexa_2, source)
raw#17 GATE: SHA256(hexa_2) == SHA256(hexa_3)
```

source 가 바뀌지 않았는데 SHA 가 다르면 → 둘 중 하나:

1. compiler 자체가 시계/PID 를 embed 함 (timestamp in ELF header, PE
   TimeStamp, PDB GUID, `__DATE__` / `__TIME__` macro …)
2. map/set iteration 이 insertion order 가 아니라 random order
3. filesystem scan 이 readdir 순서에 의존 (inode 순)
4. random seed 가 고정 안 됨
5. locale / timezone / clang version / opt level 이 빌드마다 다름

전부 **소스 밖** 의 요인이라 git diff 로는 안 보인다. 체크리스트로 **각
의심 지점을 하나씩 열거** 하고, grep 기반 스캐너 (`tool/determinism_scan.hexa`,
`.roadmap#18` 에서 도입 예정) 가 감지하도록 catalog 화 하는 것이 본 문서의
목적.

## 2. Audit checklist — 현 상태 체크

각 항목에 대해 `[ ] not-audited / [~] partial / [x] ok` 를 마킹. 전부 `[x]`
가 될 때까지 raw#17 `live` 승격 (`.roadmap#16`) 은 보류.

### 2.1 Core 10 rules

- [ ] **timestamp** — 빌드 타임스탬프 대신 `SOURCE_DATE_EPOCH` env 사용?
  - 체크: `tool/native_build.hexa` / `self/codegen_*.hexa` 에서
    `time()` / `date` / `Date.now()` 호출 금지
  - ELF / Mach-O 헤더에 build time 이 들어가는 부분은 `SOURCE_DATE_EPOCH=0`
    또는 commit date 고정으로 덮어쓴다
  - Status: 현재 `tool/stability_c3_pbv.hexa:84` 에 `perl ... time()*1000`
    1 건 — 빌드 아티팩트에 반영되는지 확인 필요

- [ ] **pid embed** — 컴파일러 출력에 `getpid()` 결과가 새지 않음?
  - 체크: `grep -rn getpid self/ tool/ stdlib/`
  - Temp 파일명 등에 `$$` / pid 를 쓰더라도, 그게 **output bytes** 에
    섞이지 않으면 OK (intermediate 만 다르면 무방)
  - Status: `tool/*.hexa` 기준 0 건 (grep clean). self/ 는 추가 확인 필요

- [ ] **random seed** — RNG 사용처가 고정 seed 또는 결정적 대체?
  - 체크: `rand()` / `srand()` / `Math.random()` / `/dev/urandom`
  - Symbol 해싱, register coloring tie-breaker 등에서 비결정적 seed 사용
    금지
  - Status: `tool/native_build.hexa:585` (doc comment 내부 설명, actual call
    없음), 그 외 clean

- [ ] **map iteration** — 사전(Map/HashMap) 순회는 **정렬된 key 만**?
  - 체크: `for (k, v) in map` 패턴에서 map 의 iteration order 가
    hash-based (insertion random) 이면 v3/v4 간 다른 순서로 emit 가능
  - Hexa 의 `Map<K,V>` spec 확인 — insertion-ordered 면 OK, hash 순이면
    `.keys().sorted()` 경유 강제
  - Status: **미확인 — scanner 필요** (`.roadmap#18` 범위)

- [ ] **filesystem order** — `find` / `readdir` 결과가 sort 경유?
  - 체크: directory scan 후 바로 순회하면 inode 순 (랜덤). 반드시
    lexicographic sort 필수
  - `tool/ai_native_scan.hexa`, `tool/raw_all.hexa` 등 scan 로직에서
    `listdir(path).sorted()` 확인
  - Status: 샘플링 필요 — 현재 `tool/` 내 scan 코드 대부분이 sort 경유로
    보이나 full audit 미완

- [ ] **locale** — `LC_ALL=C.UTF-8` (or `C`) 고정?
  - 체크: 빌드 스크립트 / CI / 로컬 셸의 locale
  - `sort` / `printf` / `date` 의 출력이 locale 에 의존 → 영향 가능
  - Status: `.github/workflows/raw-verify.yml` 에 `LC_ALL` 명시 없음 —
    runner default (`C.UTF-8`) 에 의존. 명시 필요

- [ ] **endian** — binary serialization 에 `memcpy` raw dump 없음?
  - 체크: AST snapshot, bytecode emit 시 host endian 그대로 쓰면 arm64
    ↔ x86_64 cross-fixpoint (`.roadmap#20`) 붕괴
  - 반드시 little-endian explicit (`u32_le(x)` 등) 으로 직렬화
  - Status: cross-platform fixpoint 아직 planned (2028) — 현재 single-arch
    에서는 숨어 있음

- [ ] **debug symbol** — 바이너리에 `-g` / DWARF 제거 (strip)?
  - 체크: `build/hexa_stage0` 는 release build 이며 strip 처리됨?
  - 디버그 심볼에는 경로·빌드호스트명이 포함될 수 있음
  - Status: `tool/build_stage0.hexa` 내 `strip` / `-s` flag 확인 필요

- [ ] **clang version** — host clang 이 pin 되어 있음?
  - 체크: `clang --version` 이 개발자·CI 간 동일한가?
  - clang major 버전이 바뀌면 register allocator 변경으로 byte shift →
    이건 기술적으론 rebootstrap 이벤트
  - Status: `tool/native_build.hexa` 가 system clang 호출 — 버전 assert
    없음. `.github/workflows/raw-verify.yml` 에 명시 필요

- [ ] **optimization level** — `-O2` (or `-O0`) 로 **고정**?
  - 체크: build path 별로 서로 다른 `-O` 가 적용되면 안 됨
  - Release = `-O2`, stage0 rebuild = `-O2`, 둘 다 동일 원칙
  - Status: `tool/build_stage0.hexa` 확인 필요 — 현재 `-O2` 가정

### 2.2 Extended rules (optional, fixpoint 깊어질수록 필요)

- [ ] **__DATE__ / __TIME__** — C preprocessor macro 가 codegen 에 포함 안 됨
- [ ] **hostname / user** — `hostname()` / `$USER` 가 binary 에 embed 안 됨
- [ ] **cwd** — 상대 경로가 절대 경로로 확장되어 들어가지 않음
- [ ] **thread scheduling** — 병렬 compile 시 결과가 thread 순서에 무관
- [ ] **allocator address** — pointer 값이 iteration tie-breaker 로 쓰이지 않음

## 3. Known non-deterministic places — hexa 코드베이스 현황

2026-04-20 시점 grep 스캔 결과. 각 항목은 **잠재적 위험**이며, 실제로 output
bytes 에 영향이 가는지는 수동 검토 필요.

### 3.1 `time()` / timestamp 계열

| 파일                              | 용도                           | fixpoint 영향 |
| :-------------------------------- | :----------------------------- | :------------ |
| `tool/raw_sync.hexa`              | audit / sync 메타              | 낮음 (로그만) |
| `tool/raw_exemptions.hexa`        | expiry 계산                    | 낮음 (날짜 비교용) |
| `tool/raw_attestation.hexa`       | trailer `@<iso-utc>` 생성      | 높음 (commit message 에 embed) |
| `tool/stability_c3_pbv.hexa:84`   | `perl ... time()*1000`         | **조사 필요** — 빌드 출력에 들어가나? |

> 주의: `raw_attestation` 의 timestamp 는 commit message 의 trailer 로,
> binary 바깥이다. v3/v4 binary 의 SHA 에는 영향 없음. 그러나 commit SHA
> 는 매번 다르므로 (당연히) — fixpoint 는 **commit SHA** 가 아니라
> **컴파일 산출물 SHA** 를 본다.

### 3.2 `rand()` / random

| 파일                              | 용도                                 |
| :-------------------------------- | :----------------------------------- |
| `tool/native_build.hexa:585`      | 주석 (`array.sample(n)` 설명) — no-op |
| `self/ml/*.hexa` (nas, distributional_rl, ...) | ML 실험 (빌드 경로 밖)      |

ML 쪽 RNG 는 실험 실행 시점이라 self-host fixpoint 와 무관. 확인 완료.

### 3.3 Map / dict iteration

`tool/` 내 `Map<K,V>` 사용처 전수 조사 필요. Hexa 의 `Map` 구현이
insertion-ordered 인지 hash-based 인지는 `stdlib/collections.hexa` 확인 선결.
현재 상태: **unknown** (추정: insertion-ordered, but unverified).

### 3.4 Filesystem scan

`tool/raw_all.hexa`, `tool/ai_native_scan.hexa`, `tool/loop_lint.hexa` 등
대부분 `listdir` + 명시 sort 로 구성. 샘플링 결과 clean. 하지만 `self/` 쪽
build-affecting 코드는 미검.

### 3.5 기타

- `__DATE__` / `__TIME__` — `grep -rn '__DATE__\|__TIME__' self/ tool/ stdlib/`
  → 0 건 (clean)
- `hostname` embed — `grep -rn 'hostname()' self/ tool/` → 0 건 (clean)

## 4. Fix 방법 per category

### 4.1 Timestamp

```bash
# SOURCE_DATE_EPOCH 를 commit date 로 고정 (CI 기본 권장)
export SOURCE_DATE_EPOCH=$(git log -1 --pretty=%ct)

# 빌드 스크립트는 이 env 를 clang 에 forwarding
./build/hexa_stage0 tool/build_stage0.hexa
```

Hexa 코드에서 `time()` 호출이 output 에 들어간다면 wrapper 로 대체:

```hexa
// bad
let ts = time()
emit("// built at " + ts)

// good
let ts = env_or("SOURCE_DATE_EPOCH", "0")
emit("// built at " + ts)   // reproducible
```

### 4.2 PID / hostname / user

원칙: **소스에서 제거**. 디버그 용도라도 output path 에 들어가면 안 됨.
꼭 필요한 진단은 stdout/stderr 로만 뱉고, 파일로 쓰지 말 것.

### 4.3 Random seed

```hexa
// bad
let rng = Random()   // /dev/urandom seed

// good — 결정적 seed
let rng = Random.seeded(0xDEADBEEF)
```

Tie-breaker 용도면 seed 대신 **정렬 키 추가**가 더 낫다 (name, id 등
domain-specific).

### 4.4 Map iteration

```hexa
// bad
for (k, v) in symbols { emit(k + "=" + v) }

// good
for k in symbols.keys().sorted() {
    emit(k + "=" + symbols[k])
}
```

`stdlib/collections.hexa` 가 insertion-ordered 임을 공식적으로 보장한다면
`.sorted()` 생략 가능. 현재는 **항상 명시적 sort** 를 권장.

### 4.5 Filesystem order

```hexa
// bad
for entry in listdir(path) { process(entry) }

// good
for entry in listdir(path).sorted() { process(entry) }
```

Directory tree walk 도 depth-first + 각 level sorted. `find` CLI 호출
시 `LC_ALL=C find ... | sort` 로 외부 노이즈 제거.

### 4.6 Locale

```bash
# CI workflow 파일 맨 위
env:
  LC_ALL: C.UTF-8
  LANG:   C.UTF-8

# 로컬 개발: ~/.zshrc / ~/.bashrc
export LC_ALL=C.UTF-8
```

### 4.7 Endian

```hexa
// bad
write_bytes(&value as *u8, 4)   // host endian

// good
write_u32_le(value)   // explicit little-endian
```

### 4.8 Debug symbol

```bash
# build 스크립트 끝에
strip -S build/hexa_stage0       # Darwin: strip -S keeps symbols for backtrace
strip --strip-debug build/...    # Linux
```

### 4.9 Clang version

```bash
# build 스크립트 상단에 guard
REQUIRED_CLANG="17"
CURRENT=$(clang --version | head -1 | grep -oE '[0-9]+' | head -1)
if [ "$CURRENT" != "$REQUIRED_CLANG" ]; then
    echo "clang $REQUIRED_CLANG required, got $CURRENT" >&2
    exit 3
fi
```

CI 에는 `ubuntu-latest` 가 아니라 pinned image (`ubuntu-22.04` 등) 사용.

### 4.10 Optimization level

`tool/native_build.hexa` / `tool/build_stage0.hexa` 에서 `-O2` hard-code.
환경 변수로 override 허용하되 CI 에서는 강제 `-O2`.

## 5. Determinism score — 통과 항목 / 전체

현 상태 (2026-04-20) 요약:

| 카테고리             | 상태    | 비고                                                     |
| :------------------- | :------ | :------------------------------------------------------- |
| timestamp            | partial | `stability_c3_pbv.hexa` 1건 조사 필요                    |
| pid embed            | ok      | tool/ 0건                                                |
| random seed          | ok      | 빌드 경로 내 사용 없음                                   |
| map iteration        | unknown | stdlib Map 순서 보장 여부 확인 필요                      |
| filesystem order     | partial | tool/ clean, self/ 미검                                  |
| locale               | fail    | CI 에 `LC_ALL` 명시 없음                                 |
| endian               | n/a     | single-arch 기준 숨어있음                                |
| debug symbol         | unknown | strip 적용 여부 build_stage0 확인                        |
| clang version        | fail    | pin 없음                                                 |
| opt level            | partial | 코드상 `-O2` 추정, guard 없음                            |

**Score: 2/10 ok, 3/10 partial, 3/10 fail, 2/10 unknown**

`.roadmap#16` (raw#17 warn→live) 는 **최소 8/10 ok** + 30일 fixpoint streak
후 승격 권고.

## 6. Progressive adoption plan — rule 별 우선순위

한 번에 10개를 다 고칠 수는 없다. fixpoint 민감도 + 수정 난이도 조합으로
우선순위 부여:

### Tier 1 (즉시 — `.roadmap#12` 블록 해제 전 필수)

| 우선 | 항목                | 근거                                         |
| :--: | :------------------ | :------------------------------------------- |
|  1   | locale `LC_ALL=C.UTF-8` | CI 설정 1줄. 가장 싸고 가장 자주 깨지는 원인 |
|  2   | clang version pin   | `ubuntu-22.04` + version guard. 환경 drift 차단 |
|  3   | `-O2` 강제          | build script guard. silent `-O0` 유입 방지   |
|  4   | timestamp (`SOURCE_DATE_EPOCH`) | native_build 에 env forwarding           |

### Tier 2 (v3==v4 첫 시도 후 — `.roadmap#15`)

| 우선 | 항목                | 근거                                         |
| :--: | :------------------ | :------------------------------------------- |
|  5   | map iteration audit | stdlib Map 확인 + scanner 추가               |
|  6   | filesystem sort     | `self/` 쪽 scan 전수 조사                    |
|  7   | debug symbol strip  | binary 크기 & 경로 누출                      |

### Tier 3 (live 승격 전 — `.roadmap#16`)

| 우선 | 항목                | 근거                                         |
| :--: | :------------------ | :------------------------------------------- |
|  8   | pid / hostname / user scrub | 전수 grep + deny list                   |
|  9   | random seed 전수     | ML 코드 예외 처리                            |

### Tier 4 (cross-platform — `.roadmap#20`)

| 우선 | 항목                | 근거                                         |
| :--: | :------------------ | :------------------------------------------- |
|  10  | endian explicit     | arm64 ↔ x86_64 canonical match 필요         |

각 Tier 마다 scanner 자동화를 병행 (`.roadmap#18` drift detection cron 과
통합). 최종 목표: determinism_scan.hexa 가 raw_all --full 의 일부로 편입.

## 7. Checklist — audit 실행 전후

**전**:

- [ ] 현 fixpoint SHA 기록 (`cp .raw-fixpoint-hash /tmp/pre-audit.sha256`)
- [ ] `./hx status` green
- [ ] `tool/verify_fixpoint.hexa` 한 번 실행 — baseline 확인

**audit 수행**:

- [ ] §2 의 10개 rule 각각에 `[ ]/[~]/[x]` 마킹
- [ ] §3 의 known places 재확인 (grep 재실행)
- [ ] §5 의 score table 업데이트
- [ ] 바뀐 항목은 `.raw-audit` 에 `determinism-audit | <rule> | <status>` append

**후**:

- [ ] Score ≥ 8/10 이면 `.roadmap#15` new→warn 승격 PR 준비
- [ ] Score < 8/10 이면 Tier 우선순위대로 fix → 재audit

## 8. 관련 파일 / Related files

- `.raw` raw#17                   — self-host fixpoint convergence rule
- `.roadmap#12 / #15 / #16 / #18 / #20` — fixpoint, promotions, cron, cross-arch
- `tool/verify_fixpoint.hexa`     — v2→v3→v4 SHA256 verifier
- `tool/p7_7_fixpoint_check.hexa` — raw#17 proof carrier
- `tool/fixpoint_v3_v4.hexa`      — declarative fixpoint spec
- `tool/native_build.hexa`        — C codegen (clang wrapper)
- `tool/build_stage0.hexa`        — stage0 rebuild entry
- `doc/runbook/raw_rebootstrap.md`— 의도적 fixpoint 변경 ceremony
- `doc/runbook/raw_ci.md`         — CI 서버 gate (LC_ALL / clang pin 반영 지점)
- `doc/plans/p7_7_fixpoint_design_20260419.md` — 설계 근거 (SOURCE_DATE_EPOCH 언급)
- `doc/plans/raw17_convergence_add_20260420.md` — raw#17 추가 패치
