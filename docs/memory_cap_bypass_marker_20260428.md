# @memory-cap-bypass marker class proposal (2026-04-28)

## 문제

`tool/resolver_bypass_marker_audit.hexa` (raw 82 강화 lint, hive 2026-04-28) 이 32 markers 스캔 → **21 violations** 검출. 그 중 **16x** 이 `darwin-native byte-canonical pipeline` 동일 reason 으로 HXC 압축 family (`hxc_a9_tokenizer.hexa`, `hxc_a11..a22`, `hxc_base94_codec.hexa`, `hxc_d1_canary_watcher.hexa`) 에 일괄 부착됨.

raw 82 정의는 **darwin-native syscall** (chflags / launchctl / sandbox-exec / DYLD / SBPL / plutil / codesign) 에 한정. HXC 압축 알고리즘은 어느 syscall 도 쓰지 않음 → marker 오용.

진짜 이유: docker `hexa-exec` cgroup 메모리 cap **4GB**. HXC 압축은 corpus 크기에 따라 1-4GB+ peak RSS 필요. docker 안에서 OOM-kill 당하기 때문에 `@resolver-bypass` 로 docker 우회 → bare-Mac 실행. 결과: 2026-04-27 사용자 관측 `hxc_a18_lz_ppm_order4.hexa` 1.3GB peak (PID 38616) — Mac jetsam 위험.

## 해법: 별도 marker class

`@memory-cap-bypass(reason="<why>", min-mem-mb=<N>)` 신규 marker 도입. raw 82 의 darwin-native scope 와 분리.

### 의미론

| marker | bypass 사유 | 강제 routing |
|---|---|---|
| `@resolver-bypass(reason=...)` | darwin-native syscall (kernel/dyld/launchd) | bare-Mac, docker skip |
| `@memory-cap-bypass(reason=..., min-mem-mb=N)` | docker cap 초과 메모리 필요 | bare-Mac OR cap-raised docker container |

### 런타임 동작 (resolver)

`~/.hx/bin/hexa` 가 첫 100 lines 에서 marker 검출 시:

1. `@memory-cap-bypass(min-mem-mb=N)` 발견:
   - `N <= docker.mem_cap_mb` (현재 4096): docker 로 정상 routing
   - `N > docker.mem_cap_mb`:
     - 환경변수 `HEXA_DOCKER_MEM_CAP_MB` override 가능하면 docker container 재시작 (`docker stop hexa-exec && docker run --memory ${N}m`)
     - override 불가능하면 bare-Mac 실행 + Mac watcher (`raw_cli_bin_runaway_watcher.sh`) 가 RSS > N+512MB 시 SIGKILL
2. `@resolver-bypass(reason=...)`: 기존 raw 82 동작 유지

### lint 변경 (`resolver_bypass_marker_audit.hexa`)

기존 lint:
- Tier-1: raw 82 syscall 키워드
- Tier-2: host-fs-bound (host filesystem / bind-mount / ~/.hive 등) — 2026-04-28 추가

신규:
- `@memory-cap-bypass(reason=..., min-mem-mb=N)` marker 는 **별도** 검사:
  - `min-mem-mb` 필드 필수 (정수)
  - `reason` 텍스트는 압축/계산/메모리 관련 키워드 (`compression` / `corpus` / `dictionary` / `pcfg` / `coder` / `encoder` 등) 확인
  - 둘 중 하나 누락 = violation

### HXC family 21 markers 마이그레이션

각 file 에서:

```hexa
// @resolver-bypass(reason="darwin-native byte-canonical pipeline")  // OLD - misuse
```

다음으로 교체:

```hexa
// @memory-cap-bypass(reason="LZ-PPM order-4 dictionary + arithmetic coder peak RSS, byte-canonical pipeline", min-mem-mb=2048)
```

`min-mem-mb` 값은 measurement 필요:
- `hxc_a9_tokenizer.hexa`: BPE vocab 32K @ 100MB corpus → ~512MB 추정
- `hxc_a18_lz_ppm_order4.hexa`: PPM order-4 trellis @ 16K bin → 1.3GB 측정값 (2026-04-27)
- `hxc_a17_ppm_order3.hexa`: 더 작음 → ~768MB 추정
- 기타 a11-a22, base94, d1_canary: 알고리즘 별 측정 필요

## 마이그레이션 순서

1. `tool/resolver_bypass_marker_audit.hexa` — `@memory-cap-bypass` 인식 추가 (Tier-3)
2. `~/.hx/bin/hexa` resolver — `@memory-cap-bypass(min-mem-mb=N)` parse + routing 분기
3. HXC family 21 files 의 marker 교체 — `min-mem-mb` 측정값 일괄 갱신 (kick `measure-hxc-family-peak-rss-by-algorithm` 권장)
4. raw 82 entry 강화 — scope 에서 "memory-headroom" 명시적 제외 + `@memory-cap-bypass` 별도 raw 등록 검토
5. cron `dev.hexa-lang.hexa-runtime-watcher-audit` 의 verdict 에 marker 분류 통계 추가

## raw 등록 후보

`@memory-cap-bypass` 가 `@resolver-bypass` 와 동급 contract 라면 별도 raw 필요. raw 82 가 darwin-native scope 만 다루므로, 신규 raw (예: raw 160 `execution-memory-headroom-bypass`) 등록 + 트라이어드 (cli-lint + os-level + hive-agent) 적용 권장. 단, 사용자 directive 받은 후 진행.

## 폐쇄 루프 매핑 (raw 141)

- step 1 root-cause-cascade: docker 4GB cgroup → HXC OOM → 개발자가 잘못된 marker 사용 → bare-Mac 누수
- step 2 fix-hardest-layer: 별도 marker class + resolver 분기 (이 문서)
- step 3 attach-measurement: `min-mem-mb` 필드 = 명시적 메모리 contract
- step 4 emit-ledger: marker 변경 시 raw-audit row
- step 5 dashboard+cron: hexa-runtime-watcher-audit 에 marker 통계 추가
- step 6 audit-self-validating: lint Tier-3 검증

## 미해결 (사용자 결정 필요)

1. docker hexa-exec 의 cap 을 4GB → 8GB 로 올릴지, 아니면 marker 기반 동적 routing 만 채택할지
2. HXC family 의 algorithm-specific min-mem-mb 측정 일괄 진행 여부
3. raw 160 등록 여부 (yes 라면 closed-loop step 6 lint 추가)

## 참조

- raw 82 (execution-darwin-resolver-bypass)
- raw 141 (agent-closed-loop-problem-solving-methodology)
- `tool/resolver_bypass_marker_audit.hexa` (hive 2026-04-28)
- 2026-04-27 incident: `hexa_interp.real` 1.3GB on Mac (PID 38616, hxc_a18_lz_ppm_order4.hexa encode)
- 사용자 directive 2026-04-27 "관련 실행 모두 docker 로 빠지도록이 1차방어"
