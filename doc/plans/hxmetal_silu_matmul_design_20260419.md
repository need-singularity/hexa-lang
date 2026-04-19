# hxmetal — Fused SiLU × Matmul kernel design (2026-04-19)

ROI: `doc/roi.json` #168 (`ffi_backend`, P1, 32h, impact 6.0×, status `done` as design-only).
Target: M-series GPU (M4 16GB, 100 GB/s shared BW, 33 tok/s physics floor).
Companion: `doc/plans/p168_hxmetal_fused.md` (full text, 275 LOC). This note is the
focused 2026-04-19 design snapshot requested by the T8d research task.

## 1. Problem

Llama-class FFN is `out = (silu(x @ W_gate) * (x @ W_up)) @ W_down`. On a unified-memory
M4, a separate SiLU pass re-reads the `gate_proj` intermediate through L2$ twice
(apply, multiply). Per-layer cost at seq=1 decode: `~0.8 ms`. At 32 layers that is
`25.6 ms/tok`, or ~6% of the 33 tok/s envelope.

Fuse `out = silu(A @ B) * C` into one MSL kernel. Gate-proj value stays in register
across the SiLU + elementwise multiply. Halves traffic on the intermediate tensor.

## 2. Dependency gate

```
#147  landed   FFI scaffold (9-fn surface, 728b8202)
#157  running  objc_msgSend live, MTLCreateSystemDefaultDevice
#160  running  matmul PoC (tiled, dispatch3)
#168  design   blocked on #157 + #160 for live mode
```

Design locked now so a swap-in lands same-day once #157+#160 flip.

## 3. MSL kernel (compile-time TILE=16)

```msl
#include <metal_stdlib>
using namespace metal;
constant uint TILE = 16;

kernel void silu_matmul(
    device const float* A   [[ buffer(0) ]],
    device const float* B   [[ buffer(1) ]],
    device const float* C   [[ buffer(2) ]],
    device       float* out [[ buffer(3) ]],
    constant     uint4& dims [[ buffer(4) ]],    // (M, N, K, _pad)
    uint2 gid [[ threadgroup_position_in_grid ]],
    uint2 tid [[ thread_position_in_threadgroup ]])
{
    const uint M = dims.x, N = dims.y, K = dims.z;
    const uint row = gid.y * TILE + tid.y;
    const uint col = gid.x * TILE + tid.x;

    threadgroup float As[TILE][TILE];
    threadgroup float Bs[TILE][TILE];
    float acc = 0.0f;

    const uint nTiles = (K + TILE - 1) / TILE;
    for (uint t = 0; t < nTiles; ++t) {
        const uint kA = t * TILE + tid.x;
        const uint kB = t * TILE + tid.y;
        As[tid.y][tid.x] = (row < M && kA < K) ? A[row * K + kA] : 0.0f;
        Bs[tid.y][tid.x] = (kB < K && col < N) ? B[kB * N + col] : 0.0f;
        threadgroup_barrier(mem_flags::mem_threadgroup);
        for (uint k = 0; k < TILE; ++k) acc += As[tid.y][k] * Bs[k][tid.x];
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    if (row < M && col < N) {
        const float s = acc / (1.0f + exp(-acc));   // silu
        out[row * N + col] = s * C[row * N + col];  // * up_proj
    }
}
```

Design choices:
- TILE=16 matches #160 matmul; keeps 256 lanes/threadgroup (Apple-recommended).
- Threadgroup stage of A,B tiles → one global load per element per tile pass.
- Barrier pair straddles the tile stage; guards (`row<M`, etc.) cover tail shapes.
- SiLU folded into writeback: no extra kernel launch, no extra global RW on `acc`.
- `metal::exp` for PoC; `precise::exp` vs `fast::exp` sweep = follow-up ROI.

## 4. Launch geometry

- threadgroup = `(TILE=16, TILE=16, 1)` → 256 threads/group.
- grid (threadgroups) = `(ceil(N/16), ceil(M/16), 1)`.
- Buffer binding: `[A=0, B=1, C=2, out=3, dims=4]` — inherited from #160, extended by +1.

## 5. Dispatch (pure hexa)

```hexa
let dev  = hxm_device_default()
let q    = hxm_queue(dev)
let a    = hxm_buffer(dev, M * K * 4)
let b    = hxm_buffer(dev, K * N * 4)
let cb   = hxm_buffer(dev, M * N * 4)
let ob   = hxm_buffer(dev, M * N * 4)
let db   = hxm_buffer(dev, 16)
hxm_buffer_write_f32(a, A_host)
hxm_buffer_write_f32(b, B_host)
hxm_buffer_write_f32(cb, C_host)
hxm_buffer_write_u32x4(db, M, N, K, 0)

let lib  = hxm_library_from_source(dev, SILU_MATMUL_MSL)
let pipe = hxm_pipeline(lib, "silu_matmul")
let rc   = hxm_dispatch5(pipe, a, b, cb, ob, db,
                         16, 16, (N + 15) / 16, (M + 15) / 16)
hxm_buffer_read_f32(ob, out_host)
// release in reverse-alloc order
```

## 6. FFI surface delta

Already under #157: `hxm_buffer_write_f32`, `hxm_buffer_read_f32`, `hxm_buffer_write_u32x4`.
New under #168 (compile-lock stub only today):

| signature                                                              | role                         |
|------------------------------------------------------------------------|------------------------------|
| `hxm_dispatch5(pipe, b0,b1,b2,b3,b4, tgx,tgy, gx,gy) -> int`           | 5-buffer encode+commit+wait  |

Stub body: null-propagation → `-1`, all-non-null + positive geometry → `0`.
Exact pattern of #147 `hxm_dispatch`. Lands in `self/runtime/hxmetal_stub.hexa`.

## 7. Test plan (`self/test_hxmetal_fused.hexa`)

Stub mode (default, CI-safe everywhere):
- `hxm_version() == HXMETAL_ABI_VERSION`.
- `hxm_library_from_source(dev, SILU_MATMUL_MSL)` non-zero handle.
- `hxm_pipeline(lib, "silu_matmul")` non-zero handle.
- `hxm_dispatch5(...)` returns 0 on valid handles, -1 on null-propagation.
- `SILU_MATMUL_MSL` non-empty + contains `kernel void silu_matmul`, `buffer(4)`,
  `threadgroup`, `exp(-acc)` — drift-guard against accidental text loss.

Live mode (`HXMETAL_FFI_LIVE=1`, macOS, M=N=K=64 f32):
```
A[m,k] = (m + k) * 0.01
B[k,n] = (k - n) * 0.005 + 1.0
C[m,n] = 1.0 + 0.1 * (m - n)
ref[m,n] = silu(Σ_k A[m,k] * B[k,n]) * C[m,n]   # scalar CPU
```
Tolerance: `max |out - ref| < 1e-3`. Rationale: SiLU derivative spans 0.5..1,
so 1e-4 accumulator error → ~1e-3 output. Tighter risks Metal fast-math false-fails.

Perf (informational): `~0.02 ms/kernel` at 64³ (128 KB set, ~5 GFLOP, register-bound).

## 8. Scale-up (out of #168 scope)

Llama-7B FFN is `4096 × 14336 × 4096`. At `~1.1 GB` per intermediate, the fuse saves
BW, not FLOPs. Follow-up ROIs:
- tile sweep 16/32/64, simdgroup matrix ops (`simdgroup_float8x8`).
- `precise::exp` vs `fast::exp` accuracy/throughput matrix.
- fp16 / bf16 variant (M-series has native bf16 from M3).
- `gate_proj + up_proj` single kernel (reads `x` once for both).

## 9. Pitfalls honoured

- HX3 — no semicolons, K&R braces, `let mut`, no `arr+[x]` accumulation.
- HX4 — SELF-HOST FIRST: no `.m` shim. All bindings via `@link("Metal")` under #157.
- HX8 — no stage0 rebuild during this ROI (hexa_v2 circular regression).
- HX11 — matmul binding convention `[A,B,C,out,dims]` extends #160 (no re-invention).
- HX12 — strict-mode exception literals only (`-1 | 0 | 1`), no in-loop string concat.

## 10. Acceptance

| item                                                          | status |
|---------------------------------------------------------------|--------|
| Design doc (this file + p168_hxmetal_fused.md)                | done   |
| `hxm_dispatch5` stub signature                                | done (compile-lock) |
| `test_hxmetal_fused.hexa` scaffold (stub mode)                | done   |
| Live-mode numerical PoC (1e-3 tolerance)                      | blocked on #157 + #160 |
| ROI #168 → `running` (live)                                   | pending gate flip |

## 11. References

- `doc/plans/hxmetal_ffi_design.md` — #147 FFI scaffold (265 LOC).
- `doc/plans/p160_hxmetal_matmul.md` — #160 matmul PoC (175 LOC, stage 1 landed).
- `doc/plans/p168_hxmetal_fused.md` — #168 full spec (275 LOC).
- `self/runtime/hxmetal_stub.hexa` — stub module (extended by this ROI).
- `$NEXUS/shared/roadmaps/m4_edge_deploy.json` → E2-4 `metal_fused.hexa`.
- Apple — *Metal Shading Language Specification* v3.2, §5 threadgroups.
