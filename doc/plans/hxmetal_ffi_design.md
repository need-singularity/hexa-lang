# hxmetal — Apple Metal compute-shader FFI (ROI #147)

Status: scaffold / PoC. Execution path is stubbed; surface locked for future `objc_msgSend` wiring.
Date: 2026-04-19.
ROI item: `doc/roi.json` → id 147 (`ffi_backend`, P0, M4 edge ceiling highest).
Upstream consumer: `$NEXUS/shared/roadmaps/m4_edge_deploy.json` → E2-2 / E2-3 / E2-4 (`metal_ffi.hexa`, `metal_matmul.hexa`, `metal_fused.hexa`).

## 1. Why

hexa-lang today has **no GPU path**. The M4 16GB edge-deploy bottleneck analysis
(smash×2 + free_dfs×2, physics ceiling 33 tok/s at 100 GB/s shared BW) shows
the single highest-ROI hexa-lang improvement is **GPU compute overlap for the
FFN stage** — FFN is compute-bound (arithmetic intensity > crossover 36 FLOP/byte)
while attention at seq=1 is BW-bound. Overlapping these (GPU FFN 7.2 ms vs CPU
attn+prefetch 4.2 ms) collapses the per-layer cost to `max(7.2, 4.2) = 7.2 ms`
with the BW cost hidden. That is the only lever that moves the 33 tok/s ceiling
without touching the model.

hxmetal is the scaffold for that lever. It does **not** ship a full Metal
backend in ROI #147 — that is E2-3 / E2-4 work. What #147 lands is:

- a **stable FFI surface** (function signatures, handle semantics) so later
  commits can fill in the Objective-C runtime calls without reshaping the API
- a **thin pure-hexa stub** that returns sentinel handles so dependent code
  (matmul shader driver, fused kernel driver) can be authored against a fixed
  shape while the real bridge is built
- a **design for the C-codegen lowering** that will emit `#import <Metal/Metal.h>`
  and dispatch through `objc_msgSend` once the `rt_target="metal"` knob exists

## 2. Pipeline

```
.hexa (user: metal_matmul.hexa)
   │  (use self::runtime::hxmetal_stub  // or hxmetal_obj once real)
   │
   ▼
codegen_c2.hexa           — emits standard C per today; extern calls resolved
   │                         via @link("Metal") @link("Foundation")
   │
   ▼
hexa → flatten → codegen_c2 → .c   (cg_rt_target() switch: "c" | "rt" | "metal")
   │
   ▼
clang -framework Metal -framework Foundation  ← new link flags
   │
   ▼
Mach-O native binary with live MTLDevice handles
```

Three dispatch targets in `cg_rt_target()`:

| target  | math/string symbols        | extern libs                    | status   |
|---------|----------------------------|--------------------------------|----------|
| `c`     | `hexa_*` (runtime.c)       | libc only                      | live     |
| `rt`    | `rt_*_v` (self/rt/*.hexa)  | libc only                      | wired, unused (ROI #143) |
| `metal` | `rt_*_v` + `hxm_*` routing | `-framework Metal Foundation`  | this ROI (scaffold) |

**Linux dev caveat:** `Metal.framework` is macOS-only. On Linux the build
path compiles the stub (all bodies return sentinel `int`s) and links as if
it were libc. The test suite verifies signature shape and handle propagation
only. The `HXMETAL_FFI_LIVE=1` gate in the test (same pattern as `HXCUDA_FFI_LIVE`
in `self/test_hxcuda.hexa`) keeps live dispatch off-by-default so CI passes on
both platforms.

## 3. FFI binding surface

Two possible bridging strategies. #147 commits to **Strategy A** (pure-hexa
stub + later `objc_msgSend` FFI) because it keeps the self-host-first rule
(HX4) — **no Objective-C shim file ships under #147**.

### Strategy A — `objc_msgSend` direct FFI (preferred, HX4-clean)

`objc_msgSend` is the ABI entry point for **all** Objective-C method calls. A
`[MTLCreateSystemDefaultDevice()]` is actually a C function symbol, and every
subsequent `[dev newCommandQueue]` is `objc_msgSend(dev, sel_registerName("newCommandQueue"))`.
The `rt/hxmetal_objc.hexa` module (future) declares:

```hexa
@link("objc")
extern fn objc_msgSend(obj: *Void, sel: *Void) -> *Void

@link("objc")
extern fn sel_registerName(name: *Void) -> *Void

@link("Metal")
extern fn MTLCreateSystemDefaultDevice() -> *Void
```

Then `hxm_device_default()` becomes:

```hexa
fn hxm_device_default() -> int {
    let dev_p = MTLCreateSystemDefaultDevice()  // *Void (id<MTLDevice>)
    return ptr_to_int(dev_p)                     // opaque handle
}

fn hxm_queue(dev_h: int) -> int {
    let dev_p = ptr_from_int(dev_h)
    let sel   = sel_registerName(cstr("newCommandQueue"))
    let q_p   = objc_msgSend(dev_p, sel)
    return ptr_to_int(q_p)
}
```

This keeps **zero Objective-C source files** in the repo. Signatures stay in
.hexa. The only native code is `runtime.c` (existing) and the Metal framework
binaries shipped with macOS.

**Caveat.** `objc_msgSend` is variadic in C, but its ABI is regular on arm64
(all args in x0–x7, d0–d7). ARM64 Darwin **requires** declaring it with the
exact argument signature used at the call site; a generic variadic shim will
miscompile on release builds due to ABI mismatch. So `hxmetal_objc.hexa` will
need **one `extern fn objc_msgSend_<sig>`** per signature actually used
(`..._pv` = ptr→void, `..._pvp` = ptr,ptr→ptr, etc.). We estimate ≤6 shapes
for the matmul + dispatch path, so this is tractable.

### Strategy B — thin Objective-C shim (rejected for #147)

A `self/runtime/metal_shim.m` file of ~50 lines could wrap each call in a
plain C function (`hxm_raw_device_default() -> void*`, `hxm_raw_queue(void*) -> void*`, …).
Simpler ABI, no per-signature shim, no variadic pitfalls.

**Rejected** because:
1. HX4 (SELF-HOST FIRST): no new .m file. We only ship .hexa.
2. Forces the build to know about `clang -x objective-c`, which `native_build.hexa` does not do today.
3. If ROI #147's ceiling surfaces a blocker on Strategy A (variadic ABI), we can revisit and add a shim in a follow-up ROI — not under #147's 8h budget.

### The 9-function surface (locked by #147)

| hexa signature                                         | Metal equivalent (Strategy A fill-in)         | returns |
|--------------------------------------------------------|-----------------------------------------------|---------|
| `hxm_device_default() -> int`                          | `MTLCreateSystemDefaultDevice()`              | opaque device handle |
| `hxm_queue(dev_h: int) -> int`                         | `[dev newCommandQueue]`                       | opaque queue handle  |
| `hxm_buffer(dev_h: int, nbytes: int) -> int`           | `[dev newBufferWithLength:nbytes options:MTLResourceStorageModeShared]` | opaque buffer handle |
| `hxm_buffer_bytes(buf_h: int) -> int`                  | returns nbytes (round-trip check)             | int     |
| `hxm_library_from_source(dev_h, src: string) -> int`   | `[dev newLibraryWithSource:src options:nil error:&e]` | opaque library handle |
| `hxm_pipeline(lib_h, fn_name: string) -> int`          | `[lib newComputePipelineStateWithFunction:...]` | opaque pipeline handle |
| `hxm_dispatch(pipe_h, buf_in_h, buf_out_h, nthreads: int) -> int` | full command buffer encode+commit+waitUntilCompleted | 0 on success |
| `hxm_release(handle: int) -> int`                      | `[obj release]` (ARC-off or manual)           | 0 always |
| `hxm_version() -> int`                                 | `HXMETAL_ABI_VERSION` constant                | 1 (current) |

Handle semantics:
- A handle is an `int` that is bit-cast from a raw `*Void`. `ptr_to_int` /
  `ptr_from_int` round-trip preserves identity.
- A handle value of `0` means **null/error**. All stubs return 0 on failure.
- Handles are not refcounted by hexa — the caller owns the lifetime and
  must pair each `hxm_device_default` / `hxm_queue` / `hxm_buffer` /
  `hxm_library_from_source` / `hxm_pipeline` with a matching `hxm_release`.
  (In the stub every release returns 0 immediately.)

## 4. End-to-end proof: vector-add compute shader

Smallest possible GPU kernel — vector add of two f32 buffers of length N. This
is the proof-of-end-to-end-wiring that matmul (E2-3) will extend.

Flow in hexa (future file `serving/edge/metal_vector_add.hexa`, **not** in #147):

```hexa
use self::runtime::hxmetal_stub   // swap to hxmetal_objc once FFI lands

let VADD_MSL = "
#include <metal_stdlib>
using namespace metal;
kernel void vadd(device const float* a [[buffer(0)]],
                 device const float* b [[buffer(1)]],
                 device       float* c [[buffer(2)]],
                 uint tid [[thread_position_in_grid]]) {
    c[tid] = a[tid] + b[tid];
}
"

fn main() -> int {
    let dev  = hxm_device_default()
    let q    = hxm_queue(dev)
    let lib  = hxm_library_from_source(dev, VADD_MSL)
    let pipe = hxm_pipeline(lib, "vadd")

    let n    = 1024
    let nb   = n * 4
    let buf_a = hxm_buffer(dev, nb)
    let buf_b = hxm_buffer(dev, nb)
    let buf_c = hxm_buffer(dev, nb)
    // (real path will fill buf_a/buf_b via hxm_buffer_contents + write_f32)

    let rc = hxm_dispatch(pipe, buf_a, buf_c, n)  // stub: single-buffer signature
    return rc
}
```

In the stub today, `rc == 0` deterministically. In the live build `rc == 0`
iff the command buffer committed and completed without error. The matmul
shader (E2-3) replaces `vadd` MSL with a tiled bf16 GEMM but touches **zero**
code in `hxmetal_stub.hexa` or `hxmetal_objc.hexa` — only the MSL source
string changes.

## 5. `rt_target = "metal"` dispatch

Piggybacks on ROI #143's `cg_rt_target()` knob (see `docs/plans/rt_target_flip_wiring.md`).
When `cg_rt_target() == "metal"`:

1. math/string lowering uses `rt_*_v` (pure-hexa, same as `"rt"`)
2. `codegen_c2.hexa` additionally emits, at the top of the generated `.c`:
   ```c
   #import <Metal/Metal.h>
   #import <Foundation/Foundation.h>
   ```
   (Or — if Strategy A is chosen — nothing new; `objc_msgSend` / `sel_registerName` /
   `MTLCreateSystemDefaultDevice` are already `extern` declarations in the stub.)
3. `native_build.hexa` appends `-framework Metal -framework Foundation`
   to the clang invocation **iff** `uname` is `Darwin`. On Linux the build
   silently drops these flags and the stub sentinels remain the only bodies.

This is the single point where `"metal"` becomes a first-class target. #147
**does not** wire step 2 or step 3 (the stub has no framework dependency);
the hooks are documented here for the follow-up ROI.

## 6. Test strategy

Two axes of testing:

**Axis A — shape tests (always run, Linux + macOS):** `self/test_hxmetal_stub.hexa`
verifies that the 9-function surface accepts the documented argument shapes
and returns the documented types. Handle round-trip, nbytes propagation,
release idempotence, version stability. No GPU required.

**Axis B — live tests (macOS only, gated):** `HXMETAL_FFI_LIVE=1` env var
turns on real Metal dispatch in a future `self/test_hxmetal_live.hexa` (not
part of #147). Mirrors `HXCUDA_FFI_LIVE` / `HXCCL_FFI_LIVE` precedent. The
first live test will be vadd correctness against a CPU reference.

Why both? Because the stub is the **ABI contract**: if someone changes a
signature by mistake (drop an argument, change return type), axis A fires
before the real Metal build can break. axis B is the correctness guard
against actual shader + dispatch regressions.

## 7. Scope slice / non-goals for #147

**In scope (8h):**
- design doc (this file)
- stub module (`self/runtime/hxmetal_stub.hexa`) — 9 stub fns, full commentary
- test (`self/test_hxmetal_stub.hexa`) — ≥15 assertions covering axis A

**Explicitly out of scope (follow-up ROIs):**
- real `objc_msgSend` extern declarations + wiring
- MSL source inline / kernel library compilation
- `rt_target = "metal"` wiring in `codegen_c2.hexa` / `native_build.hexa`
- vadd live test
- matmul tiled bf16 kernel (E2-3)
- fused RMSNorm + SwiGLU + softmax kernel (E2-4)
- `hxm_buffer_contents` / `write_f32` host-side buffer writers
- Neural Engine (`MPSGraph` / `ANE`) — separate backend

## 8. Open questions for follow-up

1. **Command buffer reuse.** Should `hxm_dispatch` encode a fresh command
   buffer every call (simple, higher latency) or reuse a pooled buffer
   (lower latency, more bookkeeping)? For FFN at seq=1 the compute-per-dispatch
   dominates, so the simple model is probably fine. Revisit after vadd lands.
2. **MSL source caching.** `hxm_library_from_source` recompiles MSL every call.
   Real builds should cache by `sha256(src)`. Stub can ignore.
3. **Error channel.** Today failures return `0`. No `errno`-style channel.
   If Metal compilation errors need surfacing, add `hxm_last_error() -> string`
   in a follow-up.
4. **ANE path.** If Neural Engine becomes viable for draft model inference,
   a parallel `hxane_stub.hexa` can mirror this design. Out of #147 scope.
