# hexa.real perf-32 Swap — 2026-04-27 (raw#85)

## Trigger
hexa-lang 자체 개선 — `hexa.real` (raw interp 폴백 경로) 가 `hexa` Mach-O
대비 7-8× 느리던 격차 (1.39s vs 0.18s) 의 근본 원인 해소. 캐시 의존 없이
hexa.real 자체가 빠르게 동작하도록 함.

## Root cause
두 binary 모두 `self/main.hexa` 의 AOT 컴파일 결과물이지만, 빌드 시점이
달랐음:

| binary | 빌드 시점 | perf-32 (FLATTEN layer) |
|---     |---        |---                       |
| `~/.hx/packages/hexa/hexa` (Mach-O dispatcher) | 2026-04-27 00:14 | ✅ 포함 |
| `~/.hx/packages/hexa/hexa.real` (legacy/fallback) | 2026-04-26 22:48 | ❌ 누락 |

`perf-32` (commit ~72eb8367 follow-up, main.hexa:1633-1647 FLATTEN layer)
는 AOT 컴파일러가 `use "self/stdlib/ai_err"` 같은 import directive 를
`module_loader` 로 inline 하도록 함. 미적용 (`hexa.real`) 시:

1. `aot_build_slot` 이 transpile 단계에서 import body 를 inline 안 함
2. clang link → `Undefined symbol _ai_err_exit` 등으로 실패
3. AOT 슬롯에 `src.c` 만 남고 `exe` 미생성 → meta 미기록
4. `.aot-link-failed` 마커도 안 남김 (코드 경로상 link 실패 분기 미진입)
5. **다음 호출에서 또 같은 실패 반복** — 매번 2-3초 낭비

이로 인해 stdlib-using 도구 ~70개가 hexa.real 경유 시 5-10× 느리게 동작.

## Fix (atomic binary swap)

`hexa.real` 을 현재 perf-32 적용된 `hexa` Mach-O 로 교체 (둘 다 같은
main.hexa 산출물이므로 ABI/argv 처리 동일):

```
cp -p ~/.hx/packages/hexa/hexa            <repo>/hexa.real.tmp.$$
mv -f <repo>/hexa.real.tmp.$$             <repo>/hexa.real
```

Symlink 가 `~/.hx/packages/hexa/hexa.real → <repo>/hexa.real` 이므로
deploy 측에도 즉시 반영. 백업: `<repo>/hexa.real.bak_pre_perf32_swap_20260427`.

## Verification

### Latency (changelog_parse.hexa, CHANGELOG.md = 4078 lines)
| 호출 | Before swap | After swap |
|---   |---          |---         |
| `hexa.real` 1st (cold .hexa-cache) | **2.65s** | **0.31s** |
| `hexa.real` 2nd+ (warm)            | **2.65s** (still failing AOT) | **0.28s** |
| `hexa` Mach-O (warm)               | 0.18s    | 0.18s (unchanged) |
| `hexa.real` HEXA_CACHE=0 (interp only) | 0.52s | 0.52s (unchanged) |

격차: **8.7×** 단축. 더 이상 hexa.real 이 raw interp 로 falling through
하지 않고 캐시 정상 populate.

### sha256 identity
```
fb3112eb...  ~/.hx/packages/hexa/hexa
fb3112eb...  ~/.hx/packages/hexa/hexa.real
```
(swap 후 두 binary 동일)

### `--version` sanity
```
hexa 0.1.0-dispatch  ✓
```

### Backward compat
- `~/.hx/bin/hexa` resolver 의 `REAL_HEXA="$HOME/.hx/packages/hexa/hexa"`
  기본값은 hexa Mach-O 였으므로 영향 없음.
- `tool/{rebuild_interp,raw_all,build_train_gpu_c}.hexa` 등은 `./build/hexa.real`
  (다른 파일) 참조 — 영향 없음.
- `tool/{law_check,roadmap_integrate,emit_hxi}.hexa` 는 주석/설명용 언급
  뿐 — 영향 없음.

## Why caches are not the answer
caller (`changelog.ts`) 가 `HEXA_RESOLVER_NO_REROUTE=1` 설정 시 resolver
가 `REAL_HEXA` 직접 exec → `hexa` Mach-O 거치므로 hive 자체에는 직접
영향 없음. 그러나 hexa-lang 생태계 전반:

- 70여개 stdlib-using 도구가 hexa.real 경유 시 매 호출 2-3초 낭비
- AOT 캐시는 link 성공 후에만 의미 있음. `hexa.real` 이 link 항상 실패하면
  캐시가 있어도 `cache_hit` false → 매 호출 rebuild 시도
- 즉, 캐시 의존 안 하는 영구 fix = AOT 자체가 동작하도록 binary 교체

## Out-of-scope (별도 워크스트림)
- 첫-호출 cold compile (3.76s, empty `.hexa-cache`): 이건 transpile +
  clang 의 본질적 비용. Pre-warm 또는 incremental compile 로 별도 처리.
- `hexa.real` 도 `build/hexa.real` 도 아닌 `build/hexa_interp.real` (별
  주체 — 1.5MB, hexa_full.hexa 산출물): perf-32 검증 별도.

## Patch sites
- `/Users/ghost/core/hexa-lang/hexa.real` ← swap (sha256 fb3112eb...)
- 백업: `/Users/ghost/core/hexa-lang/hexa.real.bak_pre_perf32_swap_20260427`
