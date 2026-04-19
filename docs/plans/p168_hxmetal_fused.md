# hxmetal — Fused SiLU × Matmul kernel PoC (ROI #168)

Status: **design only** (2026-04-19). Depends on ROI #157 (objc_msgSend live wiring) and ROI #160 (matmul PoC). Both are currently `running` in `shared/hexa-lang/roi.json`; this document locks the fused-kernel design so that as soon as #157+#160 land, #168 can be implemented by swapping the stub body and flipping the test's `HXMETAL_FFI_LIVE` gate.

Related:
- ROI #147 — FFI scaffold (9-function surface, landed 728b8202).
- ROI #157 — objc_msgSend + vadd E2E (running, unlanded).
- ROI #160 — tiled matmul PoC (running, unlanded).
- Upstream: `$NEXUS/shared/roadmaps/m4_edge_deploy.json` → E2-4 `metal_fused.hexa`.

## 1. Why a fused SiLU × matmul?

Per-layer cost analysis on M4 16GB (100 GB/s shared BW, 33 tok/s physics floor) shows:

- FFN `up_proj` and `gate_proj` each touch ~7 GB/s at seq=1 decode.
- A separate SiLU pass rereads the gate-projection output **twice** (once to
  apply SiLU, once to multiply by `up_proj`). On a unified-memory device the
  rereads are not free — they re-enter the L2$ and cost `~0.8 ms/layer`.
- Fusing `out = silu(A @ B) * C` into a single MSL kernel keeps the gate-proj
  result in **threadgroup memory / registers** and halves the memory traffic
  on the intermediate tensor. At 32 layers × 0.8 ms = **25.6 ms per token**
  saved. This is ~6% of the 33 tok/s envelope.

The PoC validates the kernel shape and numerical equivalence against a naive
CPU reference at a small size (`M=N=K=64`). Scale-up to the real FFN shape
(`4096×14336×4096` for Llama-class) is follow-up work under its own ROI.

## 2. Dependency audit (why "design only" today)

```
ROI #147  ✅ landed  (stub + ABI surface)
ROI #157  ⏳ running (objc_msgSend, MTLCreateSystemDefaultDevice live)
ROI #160  ⏳ running (MSL matmul compile + dispatch3 + naive tiling)
ROI #168  → blocked until #157 AND #160 land
```

Reason #157 is required:
- fused kernel needs `hxm_library_from_source()` to actually compile MSL,
  `hxm_pipeline()` to return a live `MTLComputePipelineState`, and
  `hxm_dispatchN()` to encode+commit+wait. The stub returns sentinel handles
  that cannot execute a real shader.

Reason #160 is required:
- fused kernel reuses the matmul launch geometry (tile size, threadgroup
  shape, buffer-binding convention at indices `[A=0, B=1, C=2, out=3, dims=4]`).
  Defining these twice (once under #160, once under #168) would risk drift.
  #168 extends the matmul dispatcher; it does not reinvent it.

Until both land, #168's code cannot be exercised end-to-end, so we only lock
the design (this document) and add a compile-checked stub signature.

## 3. MSL kernel — full text

The kernel computes `out[m,n] = silu(Σ_k A[m,k] * B[k,n]) * C[m,n]`. `silu(x) = x / (1 + exp(-x))`. Tile shape `16×16`, one threadgroup per output tile. `K` is looped inside the threadgroup with threadgroup-memory staging of A and B tiles — same structure as #160's matmul, with the SiLU and C-multiply folded into the final writeback.

```msl
// hxmetal_fused_silu_matmul.metal — ROI #168
// Compile-time parameters (baked into the MSL source we pass to
// hxm_library_from_source): TILE=16.
#include <metal_stdlib>
using namespace metal;

constant uint TILE = 16;

kernel void silu_matmul(
    device const float* A   [[ buffer(0) ]],
    device const float* B   [[ buffer(1) ]],
    device const float* C   [[ buffer(2) ]],
    device       float* out [[ buffer(3) ]],
    constant     uint4& dims [[ buffer(4) ]],    // (M, N, K, _pad)
    uint2  gid  [[ threadgroup_position_in_grid ]],
    uint2  tid  [[ thread_position_in_threadgroup ]],
    uint2  tpg  [[ threads_per_threadgroup ]])
{
    const uint M = dims.x;
    const uint N = dims.y;
    const uint K = dims.z;

    // Thread's output coordinate
    const uint row = gid.y * TILE + tid.y;
    const uint col = gid.x * TILE + tid.x;

    // Threadgroup-memory staging for A and B tiles
    threadgroup float As[TILE][TILE];
    threadgroup float Bs[TILE][TILE];

    float acc = 0.0f;

    // Walk K in TILE-sized chunks
    const uint nTiles = (K + TILE - 1) / TILE;
    for (uint t = 0; t < nTiles; ++t) {
        const uint kA = t * TILE + tid.x;   // col inside A
        const uint kB = t * TILE + tid.y;   // row inside B

        As[tid.y][tid.x] = (row < M && kA < K) ? A[row * K + kA] : 0.0f;
        Bs[tid.y][tid.x] = (kB < K && col < N) ? B[kB * N + col] : 0.0f;

        threadgroup_barrier(mem_flags::mem_threadgroup);

        // Inner K-loop: dot product inside the tile
        for (uint k = 0; k < TILE; ++k) {
            acc += As[tid.y][k] * Bs[k][tid.x];
        }

        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    // Fuse: SiLU on the accumulator, then elementwise * C
    if (row < M && col < N) {
        // silu(x) = x / (1 + exp(-x))
        const float s = acc / (1.0f + exp(-acc));
        out[row * N + col] = s * C[row * N + col];
    }
}
```

Notes:
- `metal::exp` is the built-in; no fast-math flag needed for the PoC. A
  later ROI will swap in `precise::exp` vs `fast::exp` benchmarks.
- `threadgroup_barrier` calls straddle the tile-stage so reads of `As`/`Bs`
  only see writes completed by all lanes in the group.
- Launch guards (`row < M`, `col < N`, `kA < K`, `kB < K`) make the kernel
  safe for non-multiple-of-`TILE` shapes without a separate "tail" kernel.
  PoC shape (`64×64×64`) is a clean multiple and exercises the fast path;
  future shape-sweep will stress the guards.

## 4. Dispatch pseudocode (pure hexa)

```hexa
// The PoC live path (once #157 + #160 land) looks like:
//
//   let dev  = hxm_device_default()
//   let q    = hxm_queue(dev)
//
//   let a_buf = hxm_buffer(dev, M * K * 4)       // float = 4 bytes
//   let b_buf = hxm_buffer(dev, K * N * 4)
//   let c_buf = hxm_buffer(dev, M * N * 4)
//   let o_buf = hxm_buffer(dev, M * N * 4)
//   let d_buf = hxm_buffer(dev, 16)              // uint4 dims
//
//   hxm_buffer_write_f32(a_buf, A_host)          // added in #157
//   hxm_buffer_write_f32(b_buf, B_host)
//   hxm_buffer_write_f32(c_buf, C_host)
//   hxm_buffer_write_u32x4(d_buf, [M, N, K, 0])
//
//   let src = SILU_MATMUL_MSL                     // from this doc, string literal
//   let lib = hxm_library_from_source(dev, src)
//   let pipe = hxm_pipeline(lib, "silu_matmul")
//
//   // 5-buffer dispatch (A, B, C, out, dims)
//   //   threadgroup = (TILE=16, TILE=16, 1)
//   //   grid        = (ceil(N/TILE), ceil(M/TILE), 1)  — in threadgroups
//   let rc = hxm_dispatch5(pipe, a_buf, b_buf, c_buf, o_buf, d_buf,
//                          /*tg_x*/ 16, /*tg_y*/ 16,
//                          /*grid_x*/ (N + 15) / 16,
//                          /*grid_y*/ (M + 15) / 16)
//
//   hxm_buffer_read_f32(o_buf, out_host)          // added in #157
//
//   hxm_release(o_buf)
//   hxm_release(d_buf)
//   hxm_release(c_buf)
//   hxm_release(b_buf)
//   hxm_release(a_buf)
//   hxm_release(pipe)
//   hxm_release(lib)
//   hxm_release(q)
//   hxm_release(dev)
```

New surface required (to be added under #157 for vadd, reused here):

| signature                                                                   | role                                   |
|-----------------------------------------------------------------------------|----------------------------------------|
| `hxm_buffer_write_f32(buf_h: int, data: *Float) -> int`                     | host → device copy (Shared is zero-copy) |
| `hxm_buffer_write_u32x4(buf_h: int, a,b,c,d: int) -> int`                   | scalar dims → buffer                   |
| `hxm_buffer_read_f32(buf_h: int, out: *Float) -> int`                       | device → host copy                     |
| `hxm_dispatch5(pipe, b0,b1,b2,b3,b4, tgx,tgy, gx,gy) -> int`                | 5-buffer compute encode+commit+wait    |

`hxm_dispatch5` is the 5-buffer specialization of the generic `dispatchN` family
(#160 will add `hxm_dispatch3` for A,B,out matmul; this ROI extends it).

## 5. Test plan — `self/test_hxmetal_fused.hexa`

Two modes behind a `HXMETAL_FFI_LIVE` env flag (same pattern as
`self/test_hxcuda.hexa`):

### 5.1 Default (stub) mode — runs everywhere, CI-safe

- `hxm_version() == HXMETAL_ABI_VERSION`
- `hxm_library_from_source(dev, SILU_MATMUL_MSL)` returns non-zero handle
- `hxm_pipeline(lib, "silu_matmul")` returns non-zero handle
- `hxm_dispatch5(...)` on sentinel handles returns `0` (success contract)
- `hxm_dispatch5(0, ..., ..., ..., ..., ..., 16,16, 4,4)` returns `-1` (null-propagation)
- String literal `SILU_MATMUL_MSL` contains the kernel body and is non-empty
  (guards against accidental MSL text loss at stub authoring time)

### 5.2 Live mode (`HXMETAL_FFI_LIVE=1`, macOS only) — numerical PoC

Small shape `M=N=K=64`, float32.

1. Build `A`, `B`, `C` host-side:
   - `A[m,k] = (m + k) * 0.01`
   - `B[k,n] = (k - n) * 0.005 + 1.0`
   - `C[m,n] = 1.0 + 0.1 * (m - n)`
2. Compute reference on CPU (triple-nested loop, fused SiLU+mul):
   ```hexa
   for m in 0..M:
       for n in 0..N:
           let mut s = 0.0
           for k in 0..K: s = s + A[m*K+k] * B[k*N+n]
           ref[m*N+n] = (s / (1.0 + exp(-s))) * C[m*N+n]
   ```
3. Dispatch the Metal kernel with the same buffers.
4. Read back `out_host` and check `max(abs(out_host[i] - ref[i])) < 1e-3`.
5. Release every handle in reverse-alloc order.

Tolerance rationale: SiLU's derivative near zero is ~0.5 and blows up to ~1
away from zero, so relative error of 1e-4 on the accumulator compounds to
~1e-3 in the final product. Tighter than that risks false failures from
Metal's default fast-math on large K.

### 5.3 Performance envelope (informational, no assertion)

For `M=N=K=64`, expected `~0.02 ms/kernel` (128 KB working set, ~5 GFLOP, not BW-limited at this size). The scale-up to `4096×14336×4096` will come under a follow-up ROI and is **not** tested by #168.

## 6. Stub signature added under this ROI (compile-lock only)

To prevent API drift while #157/#160 land, we add one forward-declared
stub signature in `self/runtime/hxmetal_stub.hexa`:

```hexa
// hxm_dispatch5 — 5-buffer compute dispatch (ROI #168 stub)
// Real: encode pipeline with 5 buffers at indices 0..4, threadgroup
//       (tgx,tgy,1), grid in threadgroups (gx,gy,1), commit, wait.
// Stub: returns 0 on all-non-null handles + positive shape, -1 otherwise.
pub fn hxm_dispatch5(pipe_h: int, b0: int, b1: int, b2: int, b3: int,
                     b4: int, tgx: int, tgy: int, gx: int, gy: int) -> int
```

No live body. The stub follows the exact pattern of #147's `hxm_dispatch`
(null-propagation returns `-1`, success returns `0`).

## 7. Acceptance for ROI #168

| item                                                                  | done-as of commit |
|-----------------------------------------------------------------------|-------------------|
| This design document (kernel text, dispatch, test plan)               | **yes** (design-only commit) |
| `hxm_dispatch5` stub signature in `hxmetal_stub.hexa`                 | **yes** (compile-lock only) |
| `self/test_hxmetal_fused.hexa` scaffolded with stub-mode checks       | **yes** |
| Live-mode numerical PoC (MSL compile + dispatch + 1e-3 tolerance)     | **blocked on #157+#160** |
| ROI status transition to `done`                                       | only after live-mode PoC passes |

Until #157+#160 land, ROI #168 stays `running` in `shared/hexa-lang/roi.json`
with `note: "design only, blocked on #157+#160"`.

## 8. Pitfalls honoured

- **HX3** — grammar.jsonl consulted: no semicolons, `let mut` for reassigned,
  same-line braces (K&R), no `arr+[x]` accumulation.
- **HX4** — SELF-HOST FIRST: no `.m` shim. All bindings arrive via
  `@link("Metal")` extern declarations in hexa under #157.
- **HX8** — no stage0 rebuild during this ROI. `FORCE=1 rebuild_stage0.hexa`
  forbidden until hexa_v2 circular rebuild regression clears.
- **HX11** — project-local rules (none specific to hxmetal yet).
- **HX12** — strict mode: exception literals (-1, 0, 1) only, K&R braces,
  no in-loop string concat.

## 9. References

- `docs/plans/hxmetal_ffi_design.md` — #147 FFI scaffold design
- `self/runtime/hxmetal_stub.hexa` — stub module (extended by this ROI)
- `self/test_hxmetal_stub.hexa` — #147 tests (template for fused tests)
- Apple docs — *Metal Shading Language Specification* v3.2, §5 threadgroups
- Llama FFN structure — `out = (silu(x @ W_gate) * (x @ W_up)) @ W_down`
