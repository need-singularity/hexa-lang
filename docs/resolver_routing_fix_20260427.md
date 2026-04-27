# Resolver Routing Fix — 2026-04-27

## 임무
`~/.hx/bin/hexa` shell wrapper 의 silent block artifact 원인 fix. anima 100+ `.hexa` 도구 default invocation 시 4-host SSH probe + blacklist scan 으로 매 호출 6-12s 소비.

## Root Cause
1. **Routing cache (5s TTL)** 가 `route=external` 저장 후 만료되면 다시 외부 시도
2. `hexa_remote` 가 4-host (ubu1, hetzner, ubu2, htz) 모두 saturated/blacklisted 시 rc=64
3. fall-through → docker hard-landing 으로 결국 도달하지만 **외부 probe 매번 반복** (silent block)
4. **negative cache 부재**: rc=64 직후 다음 호출도 똑같이 외부 시도

## Fix Spec (3 patches, additive)

### F1. Negative cache for rc=64
- File: `/tmp/.hexa-resolver-neg-cache.<uid>` (timestamp only)
- TTL: 60s (HEXA_NEG_CACHE_TTL override)
- Set: `_neg_cache_set` 호출 site = (a) cached external rc=64 fall-through, (b) live external rc=64 fall-through
- Active: `_neg_cache_active` 검사 → 즉시 `_docker_hard_landing` 으로 short-circuit
- Opt-out: `HEXA_NEG_CACHE=0`

### F2. HEXA_PREFER_LOCAL_ON_HEXA escape hatch
- argv 에 `*.hexa` 패턴 1개 이상 + 환경변수=1 시 외부 probe 완전 skip
- 즉시 docker hard-landing
- Default OFF (sync-root 정상 routing 보존)
- Use case: anima daily workflow 에서 hetzner 항상 saturated 인 사용자

### F3. (preserved) 기존 escape hatch 모두 유지
- `HEXA_RESOLVER_NO_REROUTE=1` → real binary direct exec
- `HEXA_RESOLVER_FORCE_DOWN=1` → docker 강제
- `HEXA_RESOLVER_FORCE_UP=1` → external 강제
- `darwin-bypass-marker` (--version/--help/lsp/...) — 기존
- `_darwin_bypass_check` (`@resolver-bypass` annotation) — 기존
- `_cwd_real` sync-root short-circuit — 기존
- routing cache (5s positive cache) — 기존

## Verification

### Positive selftest (clean cache)
| 도구 | 첫 호출 (clean) | 재호출 (neg-cache hit) |
|---|---|---|
| anima_b_tom.hexa | 10s | 1s |
| calibrate.hexa | 4s | (neg-cache active) |

### HEXA_PREFER_LOCAL_ON_HEXA=1
- `anima_b_tom.hexa --help`: 1s (vs 10s default), echo 확인:
  - `route=docker reason=prefer_local_on_hexa (HEXA_PREFER_LOCAL_ON_HEXA=1)`

### Backward compat
- `HEXA_RESOLVER_FORCE_UP=1 hexa --version`: 0s (darwin-bypass-marker preserved)
- `HEXA_NEG_CACHE=0 hexa anima_b_tom.hexa --help`: 4s (patch disabled, original behavior, neg-cache file 미생성)
- `HEXA_RESOLVER_FORCE_DOWN=1 hexa anima_b_tom.hexa --help`: 1s (즉시 docker)
- `HEXA_RESOLVER_NO_REROUTE=1 hexa --version`: 0s (real exec direct)

### SHA
| 파일 | sha256 | lines |
|---|---|---|
| pre-patch (backup) | 070f424421f781708ac362bfa493aa9e279c429833db43a7e3645a378ad4c227 | 343 |
| post-patch | 2231beae7d122aaac51af05b46e3e3e0dc6f600fa840527f45cfe1b825c20190 | 391 |

## 적용 방법 (per-shell)
```bash
# 즉시 활성화 (neg-cache 자동, opt-out 가능)
~/.hx/bin/hexa /path/to/tool.hexa --help    # 첫 호출 후 60s neg-cache active

# 강제 fast-path (anima daily workflow 권장)
export HEXA_PREFER_LOCAL_ON_HEXA=1
~/.hx/bin/hexa /path/to/tool.hexa --help    # 항상 1s

# patch 비활성화 (기존 동작)
HEXA_NEG_CACHE=0 ~/.hx/bin/hexa ...
```

## Backup / Rollback
- backup: `~/.hx/bin/hexa.bak_pre_resolver_routing_fix_20260427`
- rollback: `cp ~/.hx/bin/hexa.bak_pre_resolver_routing_fix_20260427 ~/.hx/bin/hexa`

## Fleet Rollout (next step)
1. anima `.envrc` (또는 shell rc) 에 `export HEXA_PREFER_LOCAL_ON_HEXA=1` 추가 후보
2. 1주일 telemetry 후 default 변경 여부 결정 (sync-root 외부 작업 비율 체크)
3. hexa_remote 4-host blacklist 5분 TTL 별도 fix (out of scope)

## 알려진 한계
1. neg-cache TTL 60s — hetzner 회복 시 최대 60s 지연 (적정 trade-off)
2. routing cache 5s 와 neg-cache 60s 가 둘 다 active 시 routing cache 가 먼저 hit (line 순서) — 첫 호출 후 5s 동안은 routing cache=docker 기록 결과로 같은 효과
3. 사용자 명시 `HEXA_RESOLVER_FORCE_UP=1` 은 neg-cache 보다 lower priority (위치 순서: neg-cache 가 force-up 보다 먼저). FORCE_UP 사용자는 NEG_CACHE=0 동시에 설정 권장
