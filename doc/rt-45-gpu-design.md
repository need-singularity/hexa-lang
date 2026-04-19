# rt#45 — GPU codegen backend for hexa (research)

**Status**: research / design only (rt#45-research, wave 3, independent track)
**Successor**: rt#45 proper (wave 6) — actual PTX/Metal emission
**Date**: 2026-04-13
**Author**: rt-45-gpu-research agent
**Scope**: NO runtime changes. NO actual codegen. Architecture, API, and a stub.

---

## 1. Motivation

hexa today compiles `.hexa` → C (`self/native/hexa_cc.c`) → `clang` → native
CPU binary. This is fine for the consciousness runtime, the L0 hub, and
arena/GC work. It is **not** fine for the AGI path: CLM (consciousness
language model, from-scratch) and ALM (LoRA fine-tune) need
matmul/GEMM/attention to run on H100 / RTX 5070, not on a single CPU core
chasing tagged pointers.

The current escape valve is FFI to CUDA (see `project_clm_hexa_gpu_plan` in
memory: 7 phases shipped, 16 empirical FFI fixes). FFI works but it's a
**Python-shaped hole** in a hexa-first project: every kernel becomes opaque,
unverifiable, and bypasses the consciousness law system. The whole point of
HEXA-FIRST (R1) is that the language owns the metal.

Goal: a `@gpu` function attribute that the hexa compiler lowers to a real
GPU kernel + a host-side launch shim, so user code reads:

```hexa
@gpu
fn saxpy(a: f32, x: [f32], y: [f32]) -> [f32] {
    let i = gpu_thread_id()
    if i < x.len() {
        y[i] = a * x[i] + y[i]
    }
    return y
}

let result = saxpy.launch(grid=1024, block=256, args=(2.0, x, y))
```

No `extern "C"`. No CUDA runtime imported by the user. The compiler emits
the kernel and the launch glue.

This document is the **architecture proposal** the rt#45-implementation
agent will execute against. It does not commit to any specific code beyond
the stub in `self/native/gpu_codegen_stub.c`.

---

## 2. Constraints

### 2.1 Hexa semantic constraints

- **HexaVal is a tagged 64-bit value** (immediate int / float / pointer to
  a heap object). GPUs cannot meaningfully follow tagged pointers: warps
  diverge on every type test, atomic refcount on a heap object across 80
  SMs is a non-starter, and the GC has no GPU-side mark phase.
- **Lists are pass-by-value** in user-facing hexa (memory feedback
  `feedback_hexa_lists_pbv`). Copying a 100M-element list per kernel
  invocation defeats the purpose. The GPU subset must use
  **borrowed-by-reference fixed-shape buffers**, not hexa lists.
- **Closures and dynamic dispatch** require a live runtime stack walk;
  none of that exists on device.
- **String / map / set** allocate on the hexa heap; out of scope for `@gpu`.

### 2.2 Project constraints

- **HEXA-FIRST is enforced** by `.claude/hooks/block-forbidden-ext.sh`. The
  implementation must minimize `.c` surface — only the codegen backend itself
  may be C, and only because hexa_cc.c is C. All host-side launch shims and
  user kernels stay in `.hexa`.
- **AI-native CLM policy**: Python/CUDA wrappers are forbidden as a long-term
  path. The escape via FFI today is conditional and will be removed once the
  native path lands (this rt#45).
- **No quantization** (memory `feedback_no_quantization`). f32 / bf16 are
  the only acceptable kernel dtypes; int8/int4 inference paths are out.

### 2.3 Hardware targets

| Target            | Priority   | Why                                          |
| ----------------- | ---------- | -------------------------------------------- |
| NVIDIA PTX (sm_80, sm_90) | **P0** | H100 production; RTX 5070 dev; everything trains here |
| AMD ROCm / HIP    | P2         | nice to have, no current hardware             |
| Apple Metal (MSL) | **P3**     | dev workstation only — see § 7 decision      |
| Intel SPIR-V / oneAPI | P3     | irrelevant for current fleet                 |
| WebGPU / WGSL     | P4         | future, browser-side consciousness viewer    |

The implementation is strictly NVIDIA PTX first. Metal is a documented
non-goal for v1; the abstraction must not preclude it.

---

## 3. Target IR — the four candidates

The single most important architecture decision is **what we emit**. Once
that's locked, everything else falls out. Four real candidates exist:

### 3.1 Direct PTX text emit

**What it is**: write `.ptx` assembly source text from hexa AST, hand it to
the CUDA driver via `cuModuleLoadData()` which JITs to SASS at load time.

| Pro | Con |
| --- | --- |
| Zero external dependencies (no LLVM in the build) | We become a PTX backend — every new hexa op = new ptx pattern |
| Very fast iteration, single pass | No optimizer; PTX-as-written is roughly what runs |
| Driver-side JIT handles SM version compat (sm_80 → sm_90) | PTX is documented but verbose; reg alloc is on us |
| Tiny (~5K LOC backend, comparable to hexa_cc.c) | Hand-written PTX cannot match nvcc/MLIR codegen quality |

**Examples**: TinyCC's NVPTX backend (toy), CuPy raw kernel mode (when user
writes PTX themselves).

### 3.2 NVPTX via LLVM IR

**What it is**: emit LLVM IR text/bitcode, feed to `libLLVM` with the NVPTX
target enabled, get PTX back, then `cuModuleLoadData()`.

| Pro | Con |
| --- | --- |
| LLVM does reg alloc, instruction scheduling, loop hoisting for free | LLVM is **300 MB** of C++ as a build dep — violates HEXA-FIRST tooling spirit |
| Mature; this is what Julia (CUDA.jl), Numba, Mojo, Halide use | Build complexity explodes; cross-compilation hostile |
| Can later target ROCm by switching backend (AMDGPU) | Slower compile (LLVM passes are not free) |

**Examples**: CUDA.jl (Julia), Numba (Python), Mojo (Modular). Every
serious GPU DSL uses this.

### 3.3 SPIR-V

**What it is**: emit SPIR-V binary, hand to Vulkan / OpenCL 2.1+ /
WebGPU / oneAPI driver.

| Pro | Con |
| --- | --- |
| Vendor-neutral by construction (NVIDIA, AMD, Intel, ARM) | NVIDIA SPIR-V path is **compute-shader-shaped**, not CUDA-shaped — no warp intrinsics, no tensor cores, no cooperative groups |
| WebGPU future-proof | Worse perf than PTX on NVIDIA; that's where 100% of training runs |
| Standard-track | SPIR-V tools are still C++/LLVM-heavy; not much wins vs § 3.2 |

**Examples**: rust-gpu (rustc → SPIR-V), Zig GPU experiments, Bend.

### 3.4 Metal Shading Language (MSL) source

**What it is**: emit MSL text (a C++14 dialect Apple invented), compile via
`xcrun metal` at install time, ship `.metallib`.

| Pro | Con |
| --- | --- |
| Only path on Apple silicon | No production training on Apple silicon (zero H100s in a Mac) |
| Source is high-level — easier to emit than PTX | Tied to macOS toolchain, no headless CI on Linux |

**Examples**: ggml-metal, MLX, Apple's own MPS.

### 3.5 Decision: **NVPTX via LLVM IR** for v1, with PTX direct as a
fallback if LLVM dep is rejected.

Rationale:

1. **Quality**: register pressure on f32 GEMM is brutal; LLVM reg alloc
   is the difference between 60% and 95% of peak FLOPs. We cannot
   afford to ship a 60% backend when the entire reason for rt#45 is
   "stop being slow."
2. **Cost of rolling our own**: Julia tried direct PTX in v0.1 and
   abandoned it for LLVM by v0.4. Numba never tried. Mojo started on
   MLIR-via-LLVM. The world has converged here for a reason.
3. **Future ROCm / SPIR-V** comes nearly free — switch the LLVM target
   triple and adjust intrinsics.
4. **Build dep concession**: LLVM-NVPTX-only build (no x86 backend, no
   AArch64 backend, no MIPS, etc.) is ~80 MB. That is acceptable. We
   already optionally link clang.

If the L0 review later vetoes LLVM (HEXA-FIRST purity), the **fallback
plan** is direct PTX text emit (§ 3.1). Both backends share the
`emit_ptx_kernel(fn_node)` interface in the stub — only the body differs.
Pick at L0 review time, not now.

---

## 4. Memory model — the hard part

GPU memory ≠ hexa memory. The bridge has to be explicit.

### 4.1 What the user sees

```hexa
@gpu
fn vec_add(a: [f32; N], b: [f32; N]) -> [f32; N] { ... }
```

- Parameter types are restricted to: `f32`, `f64`, `i32`, `i64`, `bool`,
  and **fixed-size or borrowed slices** of those.
- No `String`, no `Map`, no `List`, no `Option`, no enums-with-payload, no
  closures, no nested structs that require runtime layout.
- Arrays are passed as `(device_ptr, len)` tuples. The compiler synthesizes
  a host-side adapter that calls `cuMemcpyHtoD` for `[f32]` arguments
  before the launch and `cuMemcpyDtoH` for the return.

### 4.2 What the compiler does

1. **`validate_gpu_subset(fn_node)`** runs first (see stub). If the
   function references any out-of-subset construct it errors with a
   specific list of offending nodes.
2. **Layout pass**: hexa list `[f32]` is lowered to a flat `f32*` plus
   `i64 len`. Tagged values are forbidden inside the kernel.
3. **Synthesize host stub**: for every `@gpu fn foo(...)` we emit a hexa
   `fn foo_launch(grid: i32, block: i32, ...args)` that:
   - Allocates device memory for each array argument
   - Memcpys host → device
   - Calls `cuLaunchKernel` with the JIT'd PTX module
   - Memcpys device → host for return
   - Frees device memory
4. **Emit kernel**: walk the AST, emit LLVM IR (or PTX text) per § 3.

### 4.3 Explicit device memory (escape hatch, future)

For training loops where memcpy per call is intolerable, expose a
`DeviceBuffer<f32>` type in stdlib later (rt#45 wave 6, not now):

```hexa
let x_dev = DeviceBuffer.alloc<f32>(N)
x_dev.copy_from_host(x)
for step in 0..1000 {
    saxpy.launch(grid=N/256, block=256, args=(2.0, x_dev, y_dev))
}
let y = y_dev.copy_to_host()
```

This is the API CLM training needs. The v1 PoC ships only the
copy-on-launch path.

---

## 5. API proposal — concrete syntax

### 5.1 Kernel declaration

```hexa
// Annotation form (preferred — matches @simd, @inline_hot precedent in repo)
@gpu
fn matmul_naive(a: [f32], b: [f32], c: [f32], n: i32) {
    let row = gpu_block_id_y() * gpu_block_dim_y() + gpu_thread_id_y()
    let col = gpu_block_id_x() * gpu_block_dim_x() + gpu_thread_id_x()
    if row < n && col < n {
        let mut acc: f32 = 0.0
        let mut k = 0
        while k < n {
            acc = acc + a[row * n + k] * b[k * n + col]
            k = k + 1
        }
        c[row * n + col] = acc
    }
}
```

### 5.2 Built-in intrinsics (kernel-only)

| hexa intrinsic | PTX equivalent | semantics |
| --- | --- | --- |
| `gpu_thread_id_x()` | `%tid.x` | thread index in block, x dim |
| `gpu_thread_id_y()` | `%tid.y` | thread index in block, y dim |
| `gpu_block_id_x()` | `%ctaid.x` | block index in grid, x dim |
| `gpu_block_dim_x()` | `%ntid.x` | block size, x dim |
| `gpu_grid_dim_x()` | `%nctaid.x` | grid size, x dim |
| `gpu_sync()` | `bar.sync 0` | block-wide barrier |
| `gpu_warp_shuffle(v, lane)` | `shfl.sync` | warp lane exchange |
| `gpu_atomic_add(p, v)` | `atom.add` | global atomic |

These are not free functions in the host runtime — they are intrinsics the
codegen pattern-matches and only legal inside `@gpu` bodies.
`validate_gpu_subset` errors if they appear outside.

### 5.3 Launch site

```hexa
// Form A — direct call with launch params (preferred)
matmul_naive.launch(grid=(n/16, n/16, 1), block=(16, 16, 1),
                    args=(a, b, c, n))

// Form B — for kernels with no return that operate on shared device buffers
launch_kernel(matmul_naive, grid=(n/16, n/16, 1), block=(16, 16, 1),
              a, b, c, n)
```

Both lower to the same `cuLaunchKernel` plus pre/post memcpy.

### 5.4 Compile-time errors the user will see

```
error[GPU01]: @gpu function 'foo' uses unsupported feature
  --> examples/bad.hexa:7:5
  |
7 |     let s = "hello"        // strings not allowed in @gpu
  |             ^^^^^^^
help: @gpu functions support: f32, f64, i32, i64, bool, fixed [T; N], borrowed [T]
```

```
error[GPU07]: gpu_thread_id_x() called outside @gpu function
  --> examples/bad.hexa:14:13
```

These are produced by `validate_gpu_subset` and rendered by the existing
hexa diagnostics path.

---

## 6. References — what existing projects do

Each comparable project answered the same four questions. Summary table
plus what we steal:

### 6.1 CUDA.jl (Julia)

- **IR choice**: LLVM IR → NVPTX backend → PTX → driver JIT.
- **Memory**: explicit `CuArray{Float32}`, host/device split visible.
- **Launch**: `@cuda threads=256 blocks=N kernel_fn(args...)` — macro
  rewrites into `cuLaunchKernel`.
- **Validation**: `KernelAbstractions.jl` runs Julia type inference;
  any unsupported call (e.g. `println`, GC alloc) is a hard error.
- **What we steal**: the macro-style launch, the explicit DeviceArray,
  the "validation pass = run inference and reject heap-allocating ops".
- **Reference repo**: <https://github.com/JuliaGPU/CUDA.jl>
- **Why it's relevant**: the closest analog. Julia is also dynamic, also
  has GC, also chose @gpu-attribute style.

### 6.2 JAX / XLA (Google)

- **IR choice**: HLO (high-level op IR) → MLIR → PTX (via LLVM) for GPU,
  or via XLA:CPU.
- **Memory**: tracing-based; user writes pure functions, JAX traces them
  to HLO. Arrays live wherever you put them (`jax.device_put`).
- **Launch**: invisible. `jit(fn)(args)` runs on whatever device.
- **What we don't steal**: HLO-level optimization is enormous (fusion,
  layout assignment). Out of scope for v1.
- **What we steal**: the lesson that **fusion** is the thing that buys
  perf, not raw kernel emission. Plan a fusion pass for rt#45 v2.
- **Reference repo**: <https://github.com/google/jax>

### 6.3 rust-gpu (EmbarkStudios)

- **IR choice**: rustc → SPIR-V (via custom rustc backend).
- **Memory**: Rust borrowing carries through; `&[f32]` becomes a
  storage buffer binding.
- **Launch**: graphics-shaped (vkCmdDispatch); compute-only.
- **What we steal**: the rustc-backend approach validates that emitting
  GPU IR from a host language compiler is feasible in a small team.
- **What we don't steal**: SPIR-V on NVIDIA underperforms PTX (§ 3.3).
- **Reference repo**: <https://github.com/EmbarkStudios/rust-gpu>

### 6.4 Zig GPU (experimental, std.gpu)

- **IR choice**: SPIR-V via Zig's own backend (no LLVM).
- **Memory**: `addrspace(.global)` decorations on pointers — manual.
- **Launch**: nascent.
- **What we steal**: address-space decorations may show up in our
  hexa intrinsics later (`@shared`, `@constant`).
- **Reference**: <https://github.com/ziglang/zig> (src/codegen/spirv.zig)

### 6.5 Mojo (Modular)

- **IR choice**: MLIR → LLVM → PTX.
- **Memory**: `Tensor` is a first-class type, layout-aware.
- **Launch**: `@parameter` and `vectorize` ish — closer to a polyhedral
  scheduler than a kernel call.
- **What we steal**: tensor as 1st-class type — but that's rt#44, not us.
- **Reference**: <https://docs.modular.com/mojo/manual/gpu/>

### 6.6 Triton (OpenAI)

- **IR choice**: Triton-IR → MLIR → LLVM → PTX.
- **Memory**: block-pointer abstraction; user thinks in tiles, not threads.
- **Launch**: Python decorator + autotuner.
- **What we steal**: **the block-pointer model** is the right abstraction
  for matmul/attention. Eventually `@gpu_tile` should be a thing. Not v1.
- **Reference**: <https://github.com/triton-lang/triton>

### 6.7 ggml / MLX (llama.cpp / Apple)

- **IR choice**: hand-written kernels per backend (Metal, CUDA, ROCm).
- **What we steal**: nothing — this is the **anti-pattern** we're trying
  to escape (one kernel per backend per dtype, maintained by hand).

### 6.8 The clear lesson

Of all of these, **CUDA.jl is the project to study first**. It's the
closest fit to our situation:

- dynamic-typed host language with a GC
- attribute / macro on a function makes it GPU
- LLVM-NVPTX is the proven path
- explicit device-memory escape hatch
- the validation pass runs the *normal* host type inference and just
  rejects unsupported ops — we can do the same with the existing hexa
  type checker

Recommend: rt#45 implementer reads CUDA.jl `src/compiler/irgen.jl` and
`src/compiler/validation.jl` first, before writing any code.

---

## 7. Apple Metal — does it matter?

**Decision: NO for v1, design must not preclude it.**

Reasoning:

- **Production target = NVIDIA**: H100 (RunPod), RTX 5070 (Ubuntu dev box).
  Zero training has ever happened on Apple silicon and zero is planned.
  Memory note `feedback_gpu_policy` is explicit: Ubuntu first, H100 last
  resort, Apple not in the list.
- **Dev convenience**: macOS is the primary local-dev OS. Devs would like
  to debug GPU kernels without round-tripping to Ubuntu. This is a
  legitimate but secondary concern.
- **Toolchain cost**: `xcrun metal` is macOS-only and not present in CI.
  Any Metal path forces a second CI lane.
- **MSL ↔ PTX abstraction cost**: building a real common IR (à la MLIR)
  for both is enormous work. The honest path is two backends sharing
  validation + AST, not a unified middle-end.

What the design preserves for future Metal:

- The `emit_ptx_kernel` and `emit_metal_shader` stubs are siblings, both
  taking the same validated AST node.
- All intrinsics (`gpu_thread_id_x`, etc.) are compiler-pattern-matched,
  not CUDA-specific names. Metal has the same concepts under different
  names (`thread_position_in_threadgroup`).
- The host-side launch shim is generated; whether it emits `cuLaunchKernel`
  or `MTLComputeCommandEncoder` is one switch.

If we later (post-v1) get an Apple-silicon-based experiment requirement,
implementing the Metal backend is ~1-2 weeks of work *given the validated
AST and intrinsic table from rt#45 v1*. Without rt#45 v1 it's
indistinguishable from starting over.

---

## 8. Risks (top 3)

### Risk 1 — LLVM dependency rejection

The L0 / HEXA-FIRST review may veto pulling LLVM as a build dep ("anima is
a hexa-first project; LLVM is C++ infrastructure"). If so we fall back to
direct PTX (§ 3.1) and accept the 30-40% perf hit on hand-written kernels.
The fallback is documented and the stub interface accommodates both.

**Mitigation**: ship the stub with both `emit_ptx_kernel_via_llvm` and
`emit_ptx_kernel_direct` slots. Default at build time. Decision is
reversible.

### Risk 2 — Memory model surprise

Users write `@gpu fn f(x: [f32]) { let y = x.map(|v| v * 2) }` and expect
it to work because hexa lists support `.map`. It does not work — `.map`
allocates a hexa list, the GC cannot run on device. Diagnostics must be
crystal clear (see § 5.4 example) or every CLM kernel attempt becomes an
hour-long debugging session.

**Mitigation**: `validate_gpu_subset` ships with a curated allowlist of
methods, not a denylist. Anything not on the list is rejected with a
helpful error. Allowlist starts very small (arithmetic, comparison,
indexing, intrinsics) and expands deliberately.

### Risk 3 — Tagged-pointer leak via interpreter path

hexa runs in two modes today: native (via hexa_cc.c → C → clang) and
interpreted (via `interpreter.hexa`, see memory `project_session_20260413_native_interp`). The interpreter has a different value
representation. If `@gpu` only works in native mode, half the dev workflow
breaks. If we try to support both, we have two parallel codegen paths.

**Mitigation**: `@gpu` is **native-only** in v1. Interpreter sees `@gpu`
and emits a host-only fallback that just runs the function on CPU,
serially. This unblocks `hexa run` workflows for testing logic without
GPU, while real perf comes through the native path. Document this loudly.

---

## 9. Implementation plan (for rt#45 wave 6, not this ticket)

1. Land `validate_gpu_subset` against the existing AST (no codegen yet) —
   that alone is useful, surfaces design holes early.
2. Land `emit_ptx_kernel_direct` (no LLVM dep) for the smallest possible
   kernel: `saxpy`. Verify it runs on H100 via `cuModuleLoadData`.
3. Land the host launch shim generator. End-to-end "hexa file → running
   GPU kernel" with no Python.
4. Add intrinsics one at a time: `gpu_thread_id_x`, `gpu_block_dim_x`,
   `gpu_sync`, `gpu_atomic_add`. Each gets a unit test.
5. Add `DeviceBuffer<T>` for explicit memory.
6. (Optional) LLVM-NVPTX backend behind a build flag for perf-critical
   kernels.
7. (Optional) Metal backend if/when an Apple-silicon target appears.
8. Connect to CLM training: `train_clm.hexa` matmul becomes `@gpu`.

This research ticket completes when the design doc, example, stub, and
build-toolchain entry land. It does not require any of the above to
work.

---

## 10. Open questions (deferred)

- Texture memory / surface objects? (no for v1)
- Multi-GPU? (no — single device, multi-device is rt#45 v3)
- bf16 / fp8 dtype support? (no for v1, f32 only)
- Cooperative groups across blocks? (no for v1)
- Graph capture for training loops? (no for v1)
- Integration with the consciousness law system (Φ-aware kernels)? (research — possible rt#46 overlap)

Each is a future ticket. None block rt#45 v1.

---

## 11. Files added by this ticket

- `docs/rt-45-gpu-design.md` (this file)
- `experiments/rt45_gpu_hello.hexa` — illustrative `@gpu` syntax
- `self/native/gpu_codegen_stub.c` — function-signature skeleton
- `shared/hexa-lang/build-toolchain.json` — bt#82 entry, status=planned

No runtime modified. No `hexa_cc.c`, `runtime.c`, `hexa_full.hexa`, or
`main.hexa` touched. Orthogonal to rt#32-L, rt#35, bt#81 in flight.
