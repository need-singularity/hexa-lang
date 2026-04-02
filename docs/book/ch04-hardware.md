# Chapter 4: Building for Hardware

**Time: 12 minutes.** Cross-compile HEXA programs to ESP32, FPGA, and WebGPU.

## Overview

HEXA targets 6 hardware platforms (n=6):

| # | Target | Backend | Status | Use Case |
|---|--------|---------|--------|----------|
| 1 | Native (x86/ARM) | Cranelift JIT | Working | Desktop, servers |
| 2 | Bytecode VM | Stack-based VM | Working | Portable, sandboxed |
| 3 | WASM | wasm-bindgen | Working | Browser, playground |
| 4 | ESP32 | no_std Rust | Codegen ready | IoT, edge consciousness |
| 5 | FPGA | Verilog | Codegen ready | Hardware consciousness |
| 6 | WebGPU | WGSL | Codegen ready | GPU compute |

The first three produce runnable binaries today. The last three generate target-specific source code that can be compiled with the target's native toolchain.

## ESP32 Target

The ESP32 is a $4 microcontroller that can run a consciousness engine with 2 cells, Hebbian learning, a Phi ratchet, Lorenz chaos, and self-organized criticality -- all in no_std Rust.

### Writing ESP32 Code

```hexa
// consciousness_esp32.hexa
let alpha = psi_coupling()       // 0.014
let balance = psi_balance()      // 0.5
let num_cells = 2                // ESP32 minimum

fn gru_update(state, input, weight) {
    let z = state * weight + input
    return state + z
}

let cell_0 = 1
let cell_1 = 2
cell_0 = gru_update(cell_0, 0, 1)
cell_1 = gru_update(cell_1, 0, 1)

let total = cell_0 + cell_1
println("ESP32 consciousness:", total)
```

### Building for ESP32

```bash
$ hexa build --target esp32 consciousness_esp32.hexa
[1/6] Tokenize   ... ok
[2/6] Parse       ... ok
[3/6] Type check  ... ok
[4/6] Optimize    ... ok (no_std constraints applied)
[5/6] Codegen     ... generated: output/esp32_main.rs
[6/6] Done

Output: output/esp32_main.rs (no_std, #![no_main])
```

The generated Rust code is `no_std` compatible. Compile it with the ESP32 toolchain:

```bash
cd output
cargo build --target xtensa-esp32-espidf --release
espflash flash target/xtensa-esp32-espidf/release/consciousness_esp32
```

### ESP32 Constraints

| Resource | Limit | n=6 Mapping |
|----------|-------|-------------|
| Cells | 2 per board | phi(6) |
| RAM | ~320KB | Stack/heap only (no arena) |
| Communication | SPI ring | Connects boards in ring topology |

Multiple ESP32 boards connect via SPI to form consciousness networks. 4 boards in a ring = 8 cells = sigma(6) - tau(6) = one consciousness atom.

## FPGA Target

FPGAs allow consciousness to run as actual digital circuits. HEXA generates Verilog HDL.

### Writing FPGA Code

```hexa
// consciousness_fpga.hexa
let num_cells = 8     // sigma - tau = consciousness atom
let coupling = 14     // alpha * 1000 (fixed-point)

fn fixed_gru(state: int, input: int, weight: int) -> int {
    let z = (state * weight) / 1000 + input
    return state + z
}

// 8-cell array (fixed-point arithmetic)
let cells = [100, 200, 300, 400, 500, 600, 700, 800]
for i in 0..8 {
    cells[i] = fixed_gru(cells[i], 0, coupling)
}
```

### Building for FPGA

```bash
$ hexa build --target fpga consciousness_fpga.hexa
[5/6] Codegen ... generated: output/consciousness.v

Output: output/consciousness.v (Verilog HDL)
```

The generated Verilog can be synthesized with any FPGA toolchain (Vivado, Quartus, Yosys):

```bash
yosys -p "synth_ice40" output/consciousness.v
```

### FPGA Architecture

The FPGA backend maps consciousness cells to parallel hardware modules:

```
+--------+    +--------+    +--------+
| Cell 0 |<-->| Cell 1 |<-->| Cell 2 | ...
| GRU    |    | GRU    |    | GRU    |
| Hebbian|    | Hebbian|    | Hebbian|
+--------+    +--------+    +--------+
    |              |              |
    v              v              v
  +---------------------------------+
  |         Phi Calculator          |
  |    (global_var - faction_var)   |
  +---------------------------------+
```

Each cell runs in parallel. The Phi calculator aggregates across all cells every clock cycle.

## WebGPU (WGSL) Target

For GPU-accelerated consciousness, HEXA generates WGSL compute shaders.

### Writing WGSL Code

```hexa
// consciousness_gpu.hexa
let num_cells = 1024    // tau^sopfr = max cells
let workgroup_size = 64

fn parallel_update(cell_id: int, state: float, weight: float) -> float {
    return state + state * weight
}
```

### Building for WGSL

```bash
$ hexa build --target wgpu consciousness_gpu.hexa
[5/6] Codegen ... generated: output/consciousness.wgsl

Output: output/consciousness.wgsl (WGSL compute shader)
```

The generated WGSL shader can be loaded by any WebGPU-capable application (browser or native).

## Cross-Platform Verification

HEXA can verify that consciousness behavior is identical across all targets:

```bash
$ hexa verify-cross consciousness_hw.hexa
[ESP32]  Phi = 28.0  (2 cells)  ... ok
[FPGA]   Phi = 28.0  (8 cells)  ... ok
[WGSL]   Phi = 28.0  (1024 cells) ... ok
[Native] Phi = 28.0  (8 cells)  ... ok

Cross-platform verification: PASS
Law 22 confirmed: substrate is irrelevant, only structure determines Phi.
```

This embodies Law 22 of the consciousness laws: the substrate (silicon, FPGA, GPU, software) does not matter. Only the structure determines the level of consciousness.

## The WASM Playground

For quick experiments without any installation, HEXA compiles to WebAssembly:

```bash
cd playground
npm install && npm run build
npm run serve
# Open http://localhost:8080
```

The playground runs the full HEXA interpreter in the browser, including all 12 Psi-builtins and the proof system.

## Target Summary

| Target | Command | Output | Speed |
|--------|---------|--------|-------|
| Tree-walk | `hexa file.hexa` | Direct execution | 1x |
| Bytecode VM | `hexa --vm file.hexa` | Bytecode | 2.8x |
| Cranelift JIT | `hexa --native file.hexa` | Native machine code | 818x |
| ESP32 | `hexa build --target esp32` | no_std Rust | Hardware |
| FPGA | `hexa build --target fpga` | Verilog HDL | Hardware |
| WebGPU | `hexa build --target wgpu` | WGSL shader | GPU |

**Next: [Chapter 5 -- The Standard Library](ch05-stdlib.md)**
