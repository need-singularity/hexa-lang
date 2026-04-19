# hxmetal matmul PoC — 3-stage plan (ROI #160)

Status: stage 1 landed (this PR).
Date: 2026-04-19.
ROI item: `shared/hexa-lang/roi.json` → id 160 (`ffi_backend`, P0, 24h budget).
Depends on: ROI #147 (stub, done) + ROI #157 (objc_msgSend live wiring, running).
Upstream consumer: `$NEXUS/shared/roadmaps/m4_edge_deploy.json` → E2-3 (`metal_matmul.hexa`).

## 1. Why

ROI #147 landed the 9-function Metal FFI surface and the C-codegen hooks
for `rt_target = "metal"`, but with stubbed bodies. ROI #157 is wiring
`objc_msgSend` through those stubs so that `hxm_device_default()` /
`hxm_queue()` / `hxm_library_from_source()` actually reach the Metal
framework.

#160 is the **next link in the chain**: a real GEMM (C = A·B) kernel
that exercises the end-to-end dispatch path (library compile →
pipeline state → buffer bind × 3 → dispatchThreads(N,M) → wait →
read). Matmul is the primitive the M4-edge bottleneck analysis
(physics ceiling 33 tok/s, 100 GB/s shared BW) wants accelerated for
FFN compute-bound overlap with CPU attn + prefetch.

We split the work into three stages so the first stage can LAND TODAY
against the stub FFI (proving the kernel source, dispatch scaffold
and correctness harness) and the GPU-live pieces drop in behind the
HXM_MATMUL_LIVE knob as #157 + stage 2 land.

## 2. Stage 1 (this PR) — kernel source + dispatch scaffold + 128³ test

**Landed in this PR. Budget used: ≈ 4h of the 24h ROI total.**

What ships:

1. `self/runtime/hxmetal_stub.hexa` — appended (no ABI version bump):
   - `hxm_matmul_msl_source() -> string` — full MSL kernel text.
   - `hxm_matmul_cpu_ref(a, b, m, n, k)` — O(M·N·K) scalar reference.
   - `hxm_matmul(a, b, m, n, k)` — driver that validates shape then
     routes to CPU-ref while `HXM_MATMUL_LIVE == 0`. When stage 2
     flips the knob to 1, the same driver will bind buffers, dispatch,
     and read back from Metal.
   - `hxm_matmul_tile() -> int` — exposes tile dim (16 today) for
     stage 2 sizing contracts.
   - `hxm_matmul_live() -> int` — exposes the flag for host-aware
     tests (Linux / pre-#157 macOS).

2. `self/test_hxmetal_matmul.hexa` — 20+ assertion test:
   - Shape contract (empty return on bad dims / length mismatch).
   - MSL source substring checks (`kernel void matmul`, `buffer(0)`
     … `buffer(5)`, `thread_position_in_grid`, output-write pattern).
   - Identity `I · I = I` (4×4).
   - Hand-computed `(2×3) · (3×2)` against `[[58,64],[139,154]]`.
   - 128³ driver vs CPU-reference self-consistency (`max-abs-err < 1e-4`).
   - ABI version intact at 1 (matmul additive over #147 surface).

3. MSL kernel source (locked for stage 1, **naive per-thread** —
   register-resident accumulator, no threadgroup memory):

   ```metal
   #include <metal_stdlib>
   using namespace metal;
   kernel void matmul(device const float* A [[buffer(0)]],
                      device const float* B [[buffer(1)]],
                      device       float* C [[buffer(2)]],
                      constant uint& M [[buffer(3)]],
                      constant uint& N [[buffer(4)]],
                      constant uint& K [[buffer(5)]],
                      uint2 gid [[thread_position_in_grid]]) {
     if (gid.x >= N || gid.y >= M) { return; }
     float acc = 0.0;
     for (uint k = 0; k < K; k++) { acc += A[gid.y*K + k] * B[k*N + gid.x]; }
     C[gid.y*N + gid.x] = acc;
   }
   ```

   Dispatch geometry: `threadgroupSize = (16, 16, 1)`, `dispatchThreads = (N, M, 1)`.

**Why naive first.** The kernel body is intentionally the dumbest
possible row-major GEMM. That keeps stage 1 about *pipeline wiring*,
not *kernel tuning*. When stage 2 brings in the tile + simdgroup path,
every test in `test_hxmetal_matmul.hexa` stays green without change,
because the correctness contract (128³, max-abs-err < 1e-4) is kernel-
body-agnostic.

**Why HXM_MATMUL_LIVE=0 today.** ROI #157 is in flight and the
on-disk `hexa_v2` stage0 shim is frozen until its circular-rebuild
regression clears (HX8 seed-freeze). Shipping the kernel + dispatch
scaffold behind a single-flag routing lets #157 land independently
and flip `HXM_MATMUL_LIVE = 1` with zero changes to the MSL source
or the test. That is the point of stage 1.

**Dependency on ROI #157.** If #157 completes before stage 2, the
knob can flip in a 1-line edit to `hxmetal_stub.hexa`. Until then the
tests stay green on any host — macOS or Linux CI — because the driver
routes to CPU-ref and the routing path itself is the tested unit.

## 3. Stage 2 — simdgroup 16×16 tile + threadgroup shared memory

Budget estimate: ≈ 12h of the remaining ROI #160 budget.

Work items:

1. **Tile kernel.** Replace the naive MSL body with a tiled version:
   ```metal
   threadgroup float As[TILE][TILE];
   threadgroup float Bs[TILE][TILE];
   // K / TILE outer, tile-level load + barrier + mac + barrier, final C store
   ```
   `TILE` is still 16; `hxm_matmul_tile()` already returns 16 so no
   caller contract changes. Expected speedup on M4: 3–6× vs naive at
   128³ (bandwidth hiding) once device execution is live.

2. **simdgroup MAC.** Upgrade the inner 16×16 MAC to
   `simdgroup_matrix_multiply_accumulate` (Apple M1+). Additional
   2–4× over threadgroup-tile alone. Only applies when kernel executes
   on Apple Silicon; behind runtime `mtl_device_is_apple_silicon()`
   detection added in this stage.

3. **Host-side buffer I/O primitives.** The three calls this stage
   needs that do NOT exist yet in #147's 9-fn ABI:
   - `hxm_buffer_write_f32(buf_h: int, arr) -> int` —
     `memcpy([buf contents], f32-packed arr, arr.len * 4)`.
   - `hxm_buffer_read_f32(buf_h: int, n: int) -> arr` —
     inverse; reads n×4 bytes out of `[buf contents]`.
   - `hxm_matmul_dispatch(pipe, buf_a, buf_b, buf_c, m, n, k) -> int` —
     encodes a command buffer with 3 buffer binds + 3 constant binds
     + `dispatchThreads(N, M)` + commit + wait. Return 0 on success.

   These are additive; they bump `HXMETAL_ABI_VERSION` to 2.

4. **HXM_MATMUL_LIVE flip.** Once the above are green and the
   corresponding `hxm_*` stubs route to live `objc_msgSend` (ROI #157),
   `HXM_MATMUL_LIVE = 1`. At that point `hxm_matmul` sends to Metal
   and `test_hxmetal_matmul.hexa`'s `max-abs-err < 1e-4` assertion
   becomes the GPU-vs-CPU correctness gate.

5. **Perf bench.** Add `self/bench_hxmetal_matmul.hexa` that times
   1024³ GEMM on the live path and reports TFLOPS. Target: ≥ 2 TFLOPS
   f32 on M4 (physics ceiling ~4 TFLOPS for the 16-ALU GPU at 1.4 GHz).

## 4. Stage 3 — fused activation + bf16

Budget estimate: ≈ 8h of the remaining ROI #160 budget.

Work items:

1. **Fused RMSNorm → GEMM → SwiGLU.** Single kernel that consumes an
   input tile, applies RMSNorm to the row, does the GEMM MAC against
   a weight tile, applies SwiGLU, and writes out. Avoids a device-memory
   round-trip between every FFN step. Called via a new
   `hxm_ffn_fused_kernel_source()` constant + driver in the stub.
   ROI #168 is the parallel PR that lands the fused kernel skeleton;
   this stage connects it to live dispatch using the same
   `HXM_MATMUL_LIVE` routing.

2. **bf16 path.** Add a `hxm_matmul_bf16(a_bf16, b_bf16, m, n, k)`
   variant. On Apple Silicon M3+, `simdgroup_matrix_multiply_accumulate`
   supports `bf16 × bf16 → f32` natively, doubling throughput over f32.
   Encoding: arr elements are ints carrying the bf16 bit pattern in
   the low 16 bits (hexa has no bf16 primitive yet; we reuse int).

3. **MPSGraph fallback.** Optional. For very large N (> 2048), defer
   the MSL path and call MPSGraph. `objc_msgSend(mps_graph, matrixMul…)`.
   Benefits from Apple's GPU-tuned kernels but sacrifices the
   fused-activation property from work-item #1.

## 5. Links

- Surface + stubs: `self/runtime/hxmetal_stub.hexa` (9-fn ABI + matmul scaffold).
- Tests: `self/test_hxmetal_stub.hexa` (shape tests) + `self/test_hxmetal_matmul.hexa` (matmul PoC).
- FFI design (ROI #147): `docs/plans/hxmetal_ffi_design.md`.
- Live wiring (ROI #157): tracked in `shared/hexa-lang/roi.json` id=157.
- Fused kernel (ROI #168): `docs/plans/p168_hxmetal_fused.md` (parallel).
- Upstream roadmap: `$NEXUS/shared/roadmaps/m4_edge_deploy.json` E2-3.
- ROI: `shared/hexa-lang/roi.json` id=160.
