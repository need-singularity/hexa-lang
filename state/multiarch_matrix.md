# F-MULTIARCH-1 — Live execution matrix (4 arch x 2 shape)

This is the schedule the human runs once each row's hardware is online.
The agent that produced this file does **not** have AMD GPU, NVIDIA GPU,
or even a built `libhxmetal.dylib` available, so all rows except the
CPU baseline are marked `PENDING-HARDWARE`. Do **not** mark a row PASS
without an actual hardware run.

Backends are dispatched via `self/ml/arch_dispatch.hexa` (see
`arch_select(pref)` and `HEXA_ARCH` env override). Shapes come from
`self/ml/model_registry.hexa` (`shape_lookup(id)` / `HEXA_SHAPE` env).

## Acceptance criteria (per cell)

A cell is PASS when `hexa run` of the listed command exits 0 **and**
prints a non-zero forward-pass checksum. The CPU baseline emits the
checksum directly from `cpu_forward_tiny`; the hardware rows print
their checksum from the corresponding native shim's debug exit (see
`self/native/hxqwen14b.c` rc semantics — rc==0 + non-empty out buffer
counts as PASS once the kernel ships).

Each row records: HOST (machine identifier), DATE (UTC), STATUS
(PASS|FAIL|PENDING-HARDWARE|SKIP), CHECKSUM (forward-pass anchor or
"-"), NOTES.

## Matrix

Columns: arch x shape.

### Row CPU x tiny  (developer Mac, no hardware required)

```
HEXA_ARCH=cpu /Users/ghost/.hx/bin/hexa run test/t_multiarch_cpu_smoke.hexa
```

| HOST | DATE | STATUS | CHECKSUM | NOTES |
|------|------|--------|----------|-------|
| ghost-mac-arm64 | 2026-05-04 | PASS (agent) | -773.917 | pure-hexa forward; runs in-process, no FFI; deterministic across 3 reruns |

### Row CPU x qwen14b  (full-shape CPU reference, very slow)

```
HEXA_ARCH=cpu HEXA_SHAPE=qwen14b /Users/ghost/.hx/bin/hexa run tool/multiarch_forward.hexa
```

| HOST | DATE | STATUS | CHECKSUM | NOTES |
|------|------|--------|----------|-------|
|      |      | PENDING-HARDWARE | - | needs >32 GiB RAM box; pure-hexa scalar path will take hours; consider gating behind HEXA_BIG_CPU=1; tool/multiarch_forward.hexa not yet authored — see "what is missing" below |

### Row CUDA x tiny  (any NVIDIA box, sanity probe)

```
HEXA_ARCH=cuda HEXA_SHAPE=tiny LD_LIBRARY_PATH=$PWD/self/native/build \
  /Users/ghost/.hx/bin/hexa run test/t_multiarch_cpu_smoke.hexa
```

| HOST | DATE | STATUS | CHECKSUM | NOTES |
|------|------|--------|----------|-------|
|      |      | PENDING-HARDWARE | - | needs libcudart + libhxcuda.so on a Linux+NVIDIA host; the smoke test re-uses CPU path until the FFI binds; if `arch_select` falls back to CPU, the dispatcher prints `[arch_dispatch] cuda requested but libcudart not found` — record that line in NOTES instead of marking PASS |

### Row CUDA x qwen14b  (existing Live execution anchor)

```
HEXA_ARCH=cuda HEXA_SHAPE=qwen14b LD_LIBRARY_PATH=$PWD/self/native/build \
  /Users/ghost/.hx/bin/hexa run tool/deploy_h100.hexa --forward-only
```

| HOST | DATE | STATUS | CHECKSUM | NOTES |
|------|------|--------|----------|-------|
|      |      | PENDING-HARDWARE | - | H100 SXM5 80GB pod; baseline matches the existing single-arch path before this commit |

### Row MPS x tiny  (Apple Silicon, M-series)

```
HEXA_ARCH=mps HEXA_SHAPE=tiny DYLD_LIBRARY_PATH=$PWD/self/native/build \
  /Users/ghost/.hx/bin/hexa run test/t_multiarch_cpu_smoke.hexa
```

| HOST | DATE | STATUS | CHECKSUM | NOTES |
|------|------|--------|----------|-------|
|      |      | PENDING-HARDWARE | - | M1/M2/M3/M4; requires `hexa run tool/build_hxmetal.hexa` first; without libhxmetal.dylib the dispatcher falls back to CPU and the row is SKIP |

### Row MPS x mistral-7b  (Apple Silicon practical target)

```
HEXA_ARCH=mps HEXA_SHAPE=mistral-7b DYLD_LIBRARY_PATH=$PWD/self/native/build \
  /Users/ghost/.hx/bin/hexa run tool/multiarch_forward.hexa
```

| HOST | DATE | STATUS | CHECKSUM | NOTES |
|------|------|--------|----------|-------|
|      |      | PENDING-HARDWARE | - | M2 Ultra / M3 Max with >=64 GiB unified memory; mistral-7b SwiGLU + RoPE 1e6 + GQA 4:1 |

### Row ROCm x tiny  (any AMD GPU host, sanity probe)

```
HEXA_ARCH=rocm HEXA_SHAPE=tiny LD_LIBRARY_PATH=/opt/rocm/lib:$PWD/self/native/build \
  /Users/ghost/.hx/bin/hexa run test/t_multiarch_cpu_smoke.hexa
```

| HOST | DATE | STATUS | CHECKSUM | NOTES |
|------|------|--------|----------|-------|
|      |      | PENDING-HARDWARE | - | needs libhipblas.so + a libhxrocm.so shim (NOT YET BUILT — see "what is missing" below) |

### Row ROCm x llama-7b  (AMD reference shape)

```
HEXA_ARCH=rocm HEXA_SHAPE=llama-7b LD_LIBRARY_PATH=/opt/rocm/lib:$PWD/self/native/build \
  /Users/ghost/.hx/bin/hexa run tool/multiarch_forward.hexa
```

| HOST | DATE | STATUS | CHECKSUM | NOTES |
|------|------|--------|----------|-------|
|      |      | PENDING-HARDWARE | - | MI250 / MI300 ideal; llama-7b is the cleanest open-weight 7B reference (no GQA, RoPE 1e4) |

## What is missing for the PENDING-HARDWARE rows

The agent that authored F-MULTIARCH-1 did **not** have hardware to
build or run any of these:

1. `libhxrocm.so` — no source file in tree. Mirror of `libhxcuda.so`
   targeting `libhipblas.so` / rocBLAS. Add a `self/native/hxrocm.c`
   sibling to `self/native/hxqwen14b_cuda.cu` once an AMD machine is
   available; bind it as a fourth `extern fn` block in
   `arch_dispatch.hexa` (or, more cleanly, in a new
   `self/ml/hxrocm.hexa` peer of `self/ml/hxmetal.hexa`).
2. `tool/multiarch_forward.hexa` — driver script that consumes
   `HEXA_SHAPE` + `arch_select` and runs a single forward pass on
   the chosen backend. Stubbed by the matrix above; can be authored
   without hardware but yields no signal until the dylibs land. The
   CPU x tiny row tests the dispatch path end-to-end without this
   driver, so the F-MULTIARCH-1 acceptance does NOT block on it.
3. MPS forward path — `self/ml/hxmetal.hexa` +
   `self/native/hxmetal_macos.m` exist but currently only expose
   matmul / FFN, not the full Qwen-style attention block. The MPS x
   mistral-7b row needs an end-to-end glue layer comparable to
   `self/native/hxqwen14b.c` for the CUDA case.

## How a human runs a row

1. Check `arch_probe_all()` reports the expected backend as available:
   ```
   /Users/ghost/.hx/bin/hexa run -e 'use "self/ml/arch_dispatch"; \
     println(arch_describe(arch_select("auto")))'
   ```
2. Run the row's command on the matching hardware.
3. Edit this file and replace the row's `STATUS` / `CHECKSUM` / `NOTES`.
4. Commit with a message like `chore(multiarch): record CUDA x qwen14b PASS`.

## Known invariants

- The `tiny` shape's CPU forward checksum is deterministic across hosts
  and is the contract used to validate the dispatcher itself. The
  agent-validated value on `ghost-mac-arm64` is **-773.917**. If the
  CPU x tiny row's checksum drifts on a different host, suspect
  RNG / float-determinism before any hardware row PASS.
- `HEXA_ARCH` always overrides the auto-cascade. A row labelled
  `arch=cuda` that silently falls back to CPU because `libcudart` is
  missing is **not** a valid PASS — surface the warning line emitted by
  `arch_dispatch.hexa` as part of the row's `NOTES`.
- This document is **the** source of truth for F-MULTIARCH-1
  acceptance. The roadmap entry must reference this filename, not the
  legacy single-arch deploy plan in `tool/training/deploy/`.

## Summary tally

|             | tiny             | qwen14b          | llama-7b         | mistral-7b       |
|-------------|------------------|------------------|------------------|------------------|
| **cpu**     | PASS (agent)     | PENDING-HARDWARE | -                | -                |
| **cuda**    | PENDING-HARDWARE | PENDING-HARDWARE | -                | -                |
| **mps**     | PENDING-HARDWARE | -                | -                | PENDING-HARDWARE |
| **rocm**    | PENDING-HARDWARE | -                | PENDING-HARDWARE | -                |

Per F-MULTIARCH-1: 4 arch x 2 shape = 8 cells planned. 1/8 PASS (agent).
The remaining 7/8 rows are PENDING-HARDWARE and gated on a human run.
