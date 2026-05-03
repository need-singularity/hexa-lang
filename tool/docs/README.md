# hexa-lang stdlib documentation

Auto-generated from `stdlib/**/*.hexa` docstrings by [`tool/docs_gen.py`](../docs_gen.py).

- Modules covered: **58**
- Cross-links rendered: **7**
- Modules without docstrings: **0**

## Modules

### `(root)`

| Module | Title | Public fns |
|---|---|---|
| [`anima_audio_worker`](anima_audio_worker.md) | persistent python audio worker (raw 9 hexa-only) | 0 |
| [`bytes`](bytes.md) | byte/uint conversions (P1 — qmirror per-shot perf) | 6 |
| [`collections`](collections.md) | collections | 11 |
| [`consciousness`](consciousness.md) | Hexa Consciousness Standard Library (Anima-native) | 0 |
| [`http`](http.md) | HTTP GET with custom headers (P1 — qmirror ANU x-api-key) | 2 |
| [`json`](json.md) | JSON write-side helpers (P1 — qmirror calibration cache) | 4 |
| [`json_object`](json_object.md) | json_object | 18 |
| [`math`](math.md) | math | 14 |
| [`nn`](nn.md) | Hexa Neural Network Standard Library | 6 |
| [`optim`](optim.md) | Hexa Optimizer Standard Library | 0 |
| [`parse`](parse.md) | parse | 2 |
| [`portable_fs`](portable_fs.md) | BSD/Linux 공통 file/time helpers | 0 |
| [`proc`](proc.md) | supervised OS-process spawn + .resource registration | 2 |
| [`qrng_anu`](qrng_anu.md) | qrng_anu | 5 |
| [`registry_autodiscover`](registry_autodiscover.md) | registry_autodiscover | 1 |
| [`string`](string.md) | string | 9 |
| [`yaml`](yaml.md) | yaml | 4 |

### `cert`

| Module | Title | Public fns |
|---|---|---|
| [`cert/dag`](cert__dag.md) | cert/dag | 8 |
| [`cert/factor`](cert__factor.md) | aggregate a list<CertScore> → FactorResult | 1 |
| [`cert/json_scan`](cert__json_scan.md) | minimal JSON field scanners (interp-safe) | 2 |
| [`cert/loader`](cert__loader.md) | cert / state file loaders + sha256 digest | 3 |
| [`cert/meta2_schema`](cert__meta2_schema.md) | Meta^2 breakthrough certificate schema | 13 |
| [`cert/meta2_validator`](cert__meta2_validator.md) | Meta^2 cert parser + validator + emitter | 10 |
| [`cert/mod`](cert__mod.md) | re-export surface for the cert module | 0 |
| [`cert/reward`](cert__reward.md) | soft reward-shaping multiplier | 2 |
| [`cert/selftest`](cert__selftest.md) | triplet verification + determinism probe | 2 |
| [`cert/verdict`](cert__verdict.md) | verdict vocabulary → satisfaction score | 2 |
| [`cert/writer`](cert__writer.md) | canonical raw#10 proof-carrying JSON writer | 1 |

### `ckpt`

| Module | Title | Public fns |
|---|---|---|
| [`ckpt/chunk_store`](ckpt__chunk_store.md) | ckpt/chunk_store | 10 |
| [`ckpt/format`](ckpt__format.md) | .pcc proof-carrying checkpoint on-disk layout | 17 |
| [`ckpt/merkle`](ckpt__merkle.md) | Binary Merkle tree over xxh64 u64 leaves | 2 |
| [`ckpt/mod`](ckpt__mod.md) | re-export surface for the proof-carrying ckpt module | 0 |
| [`ckpt/verifier`](ckpt__verifier.md) | load-time tamper-aware .pcc verification | 1 |
| [`ckpt/writer`](ckpt__writer.md) | produce a .pcc (proof-carrying checkpoint) file | 1 |

### `hash`

| Module | Title | Public fns |
|---|---|---|
| [`hash/xxhash`](hash__xxhash.md) | hash/xxhash | 2 |

### `linalg`

| Module | Title | Public fns |
|---|---|---|
| [`linalg/dispatch`](linalg__dispatch.md) | linalg/dispatch | 4 |
| [`linalg/ffi`](linalg__ffi.md) | linalg/ffi | 3 |
| [`linalg/mod`](linalg__mod.md) | linalg (roadmap 57 / B16) | 0 |
| [`linalg/reference`](linalg__reference.md) | linalg/reference | 3 |

### `math`

| Module | Title | Public fns |
|---|---|---|
| [`math/eigen`](math__eigen.md) | math/eigen | 3 |
| [`math/permille`](math__permille.md) | math/permille | 17 |
| [`math/rng`](math__rng.md) | math/rng | 5 |
| [`math/rng_ctx`](math__rng_ctx.md) | math/rng_ctx | 6 |

### `matrix`

| Module | Title | Public fns |
|---|---|---|
| [`matrix/construct`](matrix__construct.md) | matrix/construct (Wave A) | 4 |
| [`matrix/mod`](matrix__mod.md) | matrix (Wave A of numeric stdlib) | 4 |
| [`matrix/stack`](matrix__stack.md) | matrix/stack (Wave A) | 3 |

### `net`

| Module | Title | Public fns |
|---|---|---|
| [`net/concurrent_serve`](net__concurrent_serve.md) | net/concurrent_serve | 13 |
| [`net/http_client`](net__http_client.md) | net/http_client | 3 |
| [`net/http_request`](net__http_request.md) | net/http_request | 3 |
| [`net/http_response`](net__http_response.md) | net/http_response | 8 |
| [`net/http_server`](net__http_server.md) | net/http_server | 9 |
| [`net/mod`](net__mod.md) | net | 0 |
| [`net/socket`](net__socket.md) | net/socket | 7 |

### `optim`

| Module | Title | Public fns |
|---|---|---|
| [`optim/cpgd`](optim__cpgd.md) | optim/cpgd | 5 |
| [`optim/projector`](optim__projector.md) | optim/projector | 4 |

### `policy`

| Module | Title | Public fns |
|---|---|---|
| [`policy/closure_roadmap`](policy__closure_roadmap.md) | policy/closure_roadmap | 5 |
| [`policy/dual_track`](policy__dual_track.md) | policy/dual_track | 6 |

### `tokenize`

| Module | Title | Public fns |
|---|---|---|
| [`tokenize/tokenizer_spec`](tokenize__tokenizer_spec.md) | tokenize/tokenizer_spec | 6 |

## Docstring convention

```hexa
// stdlib/<path>.hexa — <one-line title>  (or: HEXA Standard Library — <name>)
//
// @module(slug="<short-slug>", desc="<one-line description>")
// @usage(import stdlib/<slug>; let x = <example_call>(...))
//
// <free-form narrative — problem, design, caveats, …>
//
// Cross-links: write `module::symbol` (e.g. `linalg/mod::matmul`) — both
// backticked and bare forms (http_client::http_get) are auto-linked.

// <docstring for fn — one or more // lines immediately above the decl>
pub fn name(args) -> ret { … }
```

Regenerate with `python3 tool/docs_gen.py`.
