---
schema: hexa-lang/stdlib/qrng_anu/ai-native/1
last_updated: 2026-05-02
module: stdlib/qrng_anu.hexa
depends_on: stdlib/net/http_client.hexa
sister_doc: /Users/<user>/core/anima/anima/modules/rng/README.ai.md
marker: state/markers/hexa_lang_qrng_anu_upstream_complete.marker
status: landed
---

# stdlib/qrng_anu (AI-native)

Thin REST client for ANU public Quantum Random Number Generator. One function per call mode, fixture path for CI, env-bypass live path for callers that gate liveness themselves.

## TL;DR

- `qrng_anu_uint8(N)` — fixture by default; live only when `ANIMA_QRNG_LIVE=1` env visible.
- `qrng_anu_uint8_live(N)` — explicit live, **no env check**, for callers that gate liveness upstream.
- `qrng_anu_chunked(total, sleep_ms_between)` — auto-split when `total > 1024`; honors `sleep_ms_between` between chunks.
- `qrng_anu_parse_response(text)` — JSON → `{success, data, length}` map; network-free, unit-test friendly.
- ANU rate-limit ~**1 req/min**; `sleep_ms_between` is caller's responsibility (FAQ recommends 60000).

## Endpoint

`https://qrng.anu.edu.au/API/jsonI.php?length=N&type=uint8`

Response: `{"type":"uint8","length":N,"data":[v0..vN-1],"success":true}` where `vi ∈ [0,255]`.

Override base via env `ANU_QRNG_BASE` (mirror/proxy).

## API surface

```hexa
use "stdlib/qrng_anu"

// Fixture by default; live only with env gate
pub fn qrng_anu_uint8(length: int) -> [int]              // length ∈ [1,1024], else []

// Explicit live, no env gate
pub fn qrng_anu_uint8_live(length: int) -> [int]

// Multi-chunk; pace responsibility on caller
pub fn qrng_anu_chunked(total_bytes: int, sleep_ms_between: int) -> [int]

// JSON unit-test helper
pub fn qrng_anu_parse_response(text: string) -> map      // {success: bool, data: [int], length: int}

// Env probe
pub fn qrng_anu_live_enabled() -> bool                    // ANIMA_QRNG_LIVE == "1"
```

## Constants

- `QRNG_ANU_DEFAULT_BASE = "https://qrng.anu.edu.au/API/jsonI.php"`
- `QRNG_ANU_MAX_PER_REQ = 1024`
- `QRNG_ANU_DEFAULT_TIMEOUT_SEC = 30`

## Invocation matrix

| Goal | Function | Env |
|------|----------|-----|
| CI mock (deterministic, entropy ZERO) | `qrng_anu_uint8(N)` | (default; `ANIMA_QRNG_LIVE` unset) |
| CI/dev gated live | `qrng_anu_uint8(N)` | `ANIMA_QRNG_LIVE=1` |
| Caller gates liveness explicitly | `qrng_anu_uint8_live(N)` | (none — bypass) |
| > 1024 bytes total | `qrng_anu_chunked(N, 60000)` | follows above per chunk |
| Unit test (no network) | `qrng_anu_parse_response(fixture_json)` | (none) |

## Failure modes

- Length out-of-range (≤0 or > 1024) → `[]`. Caller checks `len(...) == requested`.
- Network failure / timeout → `http_get` returns `""` → parser sees empty → `[]`.
- ANU rate-limit hit → server returns plain text "limited to 1 request per minute"; `qrng_anu_parse_response` sees no `success:true` → `[]`.
- ANU maintenance HTML page → `json_parse_object` may panic. **caveat 3 in marker.**

## Sister consumer

anima imports this module via `anima/modules/rng/anu.hexa`. The 1-line swap point lives there: `qrng_anu_uint8_live(want)` per chunk, with anima-side pacing of 60s. Do not change this module's API without coordinating that consumer.

## Caveats (full list in marker)

1. **`curl` external binary dependency.** Minimal Alpine / airgap may lack `curl`; `http_get` returns `""` silently in that case.
2. **Rate-limit responsibility on caller.** `qrng_anu_chunked(N, 0)` is allowed but will get 429s after the first request.
3. **Strict JSON parse.** Builtin `json_parse_object` may abort on HTML error pages — treat ANU outage as catastrophic.
4. **Length silent reject.** `qrng_anu_uint8(0)` → `[]` with no exception. Caller validates.
5. **`env()` visibility race.** Observed 2026-05-02: nested-call after `exec(...)` may return empty for `env("X")`. Workaround: read env once at top of `main()`, or use `qrng_anu_uint8_live` to bypass entirely.
6. **Fixture has zero entropy.** `qrng_anu_uint8(N)` mock returns `[0,1,2,...,255,0,1,...]`. Never use for security.

## Selftest

- `stdlib/test/test_http_client.hexa` (5 cases): empty-url, invalid addr, DNS fail, status field, ok-flag.
- `stdlib/test/test_qrng_anu.hexa` (20 cases): out-of-range reject (3), fixture (3), chunked multi (6), uint8 range (1), JSON parse (5), live skipped under CI (2).
- Both byte-identical across 2 reruns.

## Verified live (2026-05-02)

`qrng_anu_uint8_live(8)` returned 8 uint8 values in range; b0=33, b3=175 — non-fixture (entropy positive). Sister-repo anima driver: `[44,38,14,157,148,53,203,250]`.

## File index

| Path | sha256 |
|------|--------|
| `stdlib/qrng_anu.hexa` | `8c408ddbdc56ec45203fb3a054c9cebb213dbb7dbf3660cc26370d17e3359784` |
| `stdlib/net/http_client.hexa` | `7333d283f1b1f5118a63c4200e0033066ad916461b14a805ebc8e13996ba1fc8` |
| `stdlib/test/test_http_client.hexa` | `44d173486f96f316c1a3d435fe6918a74ebbd0c0acb3fa72d340f31569ce20de` |
| `stdlib/test/test_qrng_anu.hexa` | `6707318f9786eb68c7525e80607f28dbe0197fa5e1bfe4a9f73be0e610f9c131` |
| `docs/qrng_anu_upstream_landing.md` | `d27d9b5ec10c418d493e80afe44b30f604167abf251b9148c8528a4926af3bc0` |
| `state/markers/hexa_lang_qrng_anu_upstream_complete.marker` | `2d72cff543b29e6de17064b9eef83296db7a9aa756c19573604030d7248acda8` |
