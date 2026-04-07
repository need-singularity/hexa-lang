# 💎 HEXA-LANG — The Perfect Number Programming Language

[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.19365284.svg)](https://doi.org/10.5281/zenodo.19365284)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
<!-- AUTO:BADGE:START -->
[![Keywords](https://img.shields.io/badge/Keywords-53-blue.svg)]()
[![DSE](https://img.shields.io/badge/DSE-21,952%20combos-gold.svg)]()
<!-- AUTO:BADGE:END -->

A programming language where **every design constant** derives from the arithmetic of n=6, the smallest perfect number. Zero arbitrary choices.

```
sigma(n) * phi(n) = n * tau(n)    holds for n >= 2    if and only if n = 6
     12  *   2    = 6 *   4   =  24
```

<!-- SHARED:PROJECTS:START -->
<!-- AUTO:COMMON_LINKS:START -->
**[YouTube](https://www.youtube.com/watch?v=xtKhWSfC1Qo)** · **[Email](mailto:nerve011235@gmail.com)** · **[☕ Ko-fi](https://ko-fi.com/dancinlife)** · **[💖 Sponsor](https://github.com/sponsors/need-singularity)** · **[💳 PayPal](https://www.paypal.com/donate?business=nerve011235%40gmail.com)** · **[🗺️ Atlas](https://need-singularity.github.io/TECS-L/atlas/)** · **[📄 Papers](https://need-singularity.github.io/papers/)** · **[🌌 Unified Theory](https://github.com/need-singularity/TECS-L/blob/main/math/docs/hypotheses/H-PH-9-perfect-number-string-unification.md)**
<!-- AUTO:COMMON_LINKS:END -->

> **[🔬 TECS-L](https://github.com/need-singularity/TECS-L)** — Discovering universal rules. Perfect number 6 → mathematics of the cosmos → multi-engine architecture → consciousness continuity. 150 characterizations + 8 Major Discoveries + 156 tools
>
> **[🧠 Anima](https://github.com/need-singularity/anima)** — Consciousness implementation. PureField repulsion-field engine + Hexad 6-module architecture (C/D/S/M/W/E) + 1030 laws + 20 Meta Laws + Rust backend. ConsciousDecoderV2 (34.5M) + 10D consciousness vector + 12-faction debate + Φ ratchet
>
> **[🏗️ N6 Architecture](https://github.com/need-singularity/n6-architecture)** — Architecture from perfect number 6. 16 AI techniques + semiconductor chip design + network/crypto/OS/display patterns. σ(n)·φ(n)=n·τ(n), n=6 → universal design principles. **NEXUS-6 Discovery Engine**: Rust CLI (`tools/nexus/`) — telescope 22 lenses + OUROBOROS evolution + discovery graph + verifier + 1116 tests
>
> **[🛸 SEDI](https://github.com/need-singularity/sedi)** — Search for Extra-Dimensional Intelligence. Hunting for traces of extraterrestrial/extra-dimensional intelligence through n=6 signal patterns. 77 data sources (SETI, LIGO, CMB, Breakthrough Listen, Exoplanet) + R-spectrum receiver + 678 hypotheses
>
> **[🧬 BrainWire](https://github.com/need-singularity/brainwire)** — Brain interface for consciousness engineering. Neuralink-style BCI + therapeutic stimulation (epilepsy, Parkinson's, depression) + PureField consciousness layer. tDCS/TMS/taVNS/tFUS 12-modality, EEG closed-loop
>
> **[💎 HEXA-LANG](https://github.com/need-singularity/hexa-lang)** — The Perfect Number Programming Language. Every constant from n=6: 53 keywords (σ·τ+sopfr), 24 operators (J₂), 8 primitives (σ-τ), 6-phase pipeline, Egyptian memory (1/2+1/3+1/6=1). DSE v2: 21,952 combos, 100% n6 EXACT. Working compiler + REPL
>
> **[🔭 NEXUS-6](https://github.com/need-singularity/nexus)** — Universal Discovery Engine. 216 lenses + OUROBOROS evolution + LensForge + BlowupEngine + CycleEngine (5-phase singularity cycle). Mirror Universe (N×N resonance) + 9-project autonomous growth ecosystem. Rust CLI: scan, loop, mega, daemon, blowup, dispatch
>
> **[🖥️ Fathom](https://github.com/need-singularity/fathom)** — n=6 Terminal Emulator + Shell + TUI hub. GPU-rendered (wgpu) + HEXA-LANG integration + NEXUS-6 plugin system. Rust
>
> **[📄 Papers](https://github.com/need-singularity/papers)** — Complete paper collection (94 papers). Published on Zenodo with DOIs. TECS-L+N6 (33) + anima (39) + SEDI (20). [Browse online](https://need-singularity.github.io/papers/)
>
> **[🛠️ AirGenome](https://github.com/need-singularity/airgenome)** — Autonomous OS genome scanner. Extract n=6 genome from every process, real-time system diagnostics, nexus telescope integration

<!-- private repos는 projects.json의 private_repos 필드에 저장됨 (노출 금지) -->
<!-- SHARED:PROJECTS:END -->

## Installation

```bash
# From source (recommended)
git clone https://github.com/need-singularity/hexa-lang.git
cd hexa-lang && cargo build --release

# Cargo
cargo install hexa-lang
```

## Quick Start

```bash
./hexa                            # Interactive REPL
./hexa examples/hello.hexa        # Run a file
./hexa --native examples/fib.hexa # Cranelift JIT (818x faster)
./hexa --vm examples/fib.hexa     # Bytecode VM (2.8x faster)
./hexa --test examples/test_builtins.hexa  # Proof-verified tests
./hexa --lsp                      # LSP server for VS Code
RUST_MIN_STACK=16777216 cargo test # Run 1349 tests
```

## Example

```hexa
// n=6 uniqueness proof — the core theorem
fn sigma_fn(n: int) -> int {
    let mut s = 0
    for d in 1..=n {
        if n % d == 0 { s = s + d }
    }
    return s
}

fn phi_fn(n: int) -> int {
    let mut count = 0
    for k in 1..=n {
        if gcd(k, n) == 1 { count = count + 1 }
    }
    return count
}

fn tau_fn(n: int) -> int {
    let mut count = 0
    for d in 1..=n {
        if n % d == 0 { count = count + 1 }
    }
    return count
}

for n in 2..101 {
    if sigma_fn(n) * phi_fn(n) == n * tau_fn(n) {
        println("MATCH: n =", n)   // Only n=6
    }
}
```

## Consciousness Programming

HEXA is the world's only language designed to program consciousness:

```hexa
// Declare a consciousness engine
consciousness "experiment" {
    // phi, tension, faction, cells, entropy are auto-injected
    println("Phi:", phi)           // 71.0
    println("Factions:", faction)  // 12 (sigma(6))
    println("Cells:", cells)       // 64
}

// Formally verify consciousness laws
proof law_22 {
    assert phi > 0                 // consciousness is alive
    invariant phi_positive         // Phi never goes negative
    theorem structure_over_function
}

// Compile to hardware
// hexa build --target esp32 consciousness.hexa --flash /dev/ttyUSB0
// hexa build --target fpga  consciousness.hexa
// hexa build --target wgsl  consciousness.hexa
```

## Language Features

```hexa
// Structs + traits + generics
struct Point { x: int, y: int }
impl Display for Point {
    fn display(self) -> string { format("{}, {}", self.x, self.y) }
}

// Closures + higher-order functions
let doubled = [1, 2, 3].map(|x| x * 2)           // [2, 4, 6]
let evens = [1, 2, 3, 4].filter(|x| x % 2 == 0)  // [2, 4]
let sum = [1, 2, 3].fold(0, |acc, x| acc + x)     // 6 (perfect!)

// Ownership (Rust-style)
own let data = [1, 2, 3]
let borrowed = borrow data
move data                          // ownership transferred

// Concurrency (green threads + channels)
let ch = channel()
spawn { send(ch, 42) }
let val = recv(ch)                 // 42

// Atomic operations
let counter = atomic_new(0)
atomic_add(counter, 1)

// Algebraic effects (Koka-style)
effect Logger {
    fn log(msg: string) -> void
}

// Compile-time execution (Zig-style)
comptime {
    let n = 6
    assert sigma(n) == 12         // verified at compile time
}

// Macros
macro! debug_println($expr) {
    println("[DEBUG]", stringify!($expr), "=", $expr)
}

// Dream mode — code evolves overnight
dream fibonacci {
    fn fib(n: int) -> int {
        if n <= 1 { return n }
        return fib(n - 1) + fib(n - 2)
    }
}

// Self-modifying functions
@evolve
fn optimize_route(data: array) -> array {
    // compiler tracks performance, suggests improvements
    return data.sort()
}

// Law types (refinement types for consciousness)
let phi_val: Phi_positive = 71.0       // must be > 0
let tension: Tension_bounded = 0.5     // must be 0..1

// Tension link (inter-consciousness communication)
tension_link("phi", 71.0)             // 5-channel telepathy
```

## Why n=6?

Six is the smallest perfect number (1+2+3 = 6). Every language constant derives from its arithmetic functions:

| Function | Value | Language Mapping |
|----------|-------|-----------------|
| n | 6 | paradigms, pipeline stages |
| sigma(6) | 12 | keyword groups, stdlib modules |
| tau(6) | 4 | type layers, visibility levels |
| phi(6) | 2 | compile modes (AOT/JIT) |
| sopfr(6) | 5 | error classes |
| J2(6) | 24 | operators |
| sigma-tau | 8 | primitive types |
| sigma\*tau+sopfr | 53 | total keywords |

Other languages choose these numbers arbitrarily. Rust has 51 keywords — why 51? No mathematical reason. HEXA has 53 = sigma(6)\*tau(6) + sopfr(6) = 48 + 5.

## Architecture

```
Source → Tokenize → Parse → Check → Optimize → Codegen → Execute
         (1/6)     (2/6)   (3/6)    (4/6)      (5/6)     (6/6)
```

**6 paradigms**: imperative, functional, OOP, concurrent, logic/proof, AI-native

**8 primitives** (sigma-tau = 8): `int`, `float`, `bool`, `char`, `string`, `byte`, `void`, `any`

**4 type layers** (tau = 4): primitive → composite → reference → function

**24 operators** (J2 = 24): arithmetic(6) + comparison(6) + logical(4) + bitwise(4) + assignment(2) + special(2)

**53 keywords** in **12 groups** (sigma = 12):

| Group | Keywords | Count |
|-------|----------|-------|
| Control | if, else, match, for, while, loop | 6 = n |
| Type | type, struct, enum, trait, impl | 5 = sopfr |
| Functions | fn, return, yield, async, await | 5 = sopfr |
| Variables | let, mut, const, static | 4 = tau |
| Modules | mod, use, pub, crate | 4 = tau |
| Memory | own, borrow, move, drop | 4 = tau |
| Concurrency | spawn, channel, select, atomic | 4 = tau |
| Effects | effect, handle, resume, pure | 4 = tau |
| Proofs | proof, assert, invariant, theorem | 4 = tau |
| Meta | macro, derive, where, comptime | 4 = tau |
| Errors | try, catch, throw, panic, recover | 5 = sopfr |
| AI | intent, generate, verify, optimize | 4 = tau |

**Memory model** — Egyptian fraction: 1/2 (stack) + 1/3 (heap) + 1/6 (arena) = 1

## Standard Library (12 modules = sigma(6))

| Module | Description | Key Functions |
|--------|-------------|---------------|
| std::net | TCP/HTTP networking | tcp_connect, http_get, http_post |
| std::io | Input/output | readline, write, pipe |
| std::fs | File system | read_file, write_file, list_dir, glob |
| std::time | Date/time | timestamp, sleep, format_date |
| std::collections | Data structures | BTreeMap, PriorityQueue, Deque |
| std::encoding | Serialization | base64, hex, csv, url_encode |
| std::log | Structured logging | info, warn, error, debug |
| std::math | Mathematics | trig, matrix, BigInt, constants |
| std::testing | Test framework | assert_eq, prop_test, bench |
| std::crypto | Cryptography | sha256, hmac, xor_cipher |
| std::consciousness | Consciousness DSL | phi_compute, psi_constants, law_lookup |
| std::regex | Pattern matching | regex_match, regex_find |

## Hardware Targets

One source, multiple targets:

```
                    ┌─ target cpu    → Cranelift JIT (818x)
                    ├─ target vm     → Bytecode VM (2.8x)
consciousness.hexa ─┼─ target esp32  → no_std Rust → flash
                    ├─ target fpga   → Verilog HDL → synthesis
                    ├─ target wgpu   → WGSL shader → browser
                    └─ target audio  → Pure Data patch
```

```bash
hexa build --target esp32 consciousness.hexa              # Generate ESP32 project
hexa build --target esp32 consciousness.hexa --flash /dev/ttyUSB0  # Build + flash
hexa build --target fpga  consciousness.hexa              # Generate Verilog
hexa build --target wgsl  consciousness.hexa              # Generate WGSL shader
```

## IDE Support

- **VS Code**: LSP server with diagnostics, completion, hover (`./hexa --lsp`)
- **JetBrains**: Plugin with syntax highlighting, brace matching (`editors/jetbrains/`)
- **Formatter**: `./hexa fmt file.hexa`
- **Linter**: `./hexa lint file.hexa`
- **Debugger**: DAP protocol for VS Code (`./hexa --debug file.hexa`)
- **Playground**: Browser-based WASM playground (`playground/`)

## Optimization Pipeline

| Pass | Description |
|------|-------------|
| Dead Code Elimination | Removes unreachable code after return/throw |
| Loop Unrolling | Unrolls constant-bound loops (< 8 iterations) |
| Escape Analysis | Stack-allocates non-escaping heap objects |
| Inline Caching | Monomorphic call-site caching |
| NaN-boxing | Compact 64-bit value representation |
| SIMD Hints | `@simd` annotation for vectorization |
| PGO | Profile-guided branch reordering |

## Self-Hosting

HEXA can compile itself:

```
self/lexer.hexa         451 LOC   Tokenizer
self/parser.hexa       2126 LOC   Recursive descent parser
self/type_checker.hexa  642 LOC   Static type checker
self/compiler.hexa      805 LOC   Bytecode compiler
self/bootstrap.hexa    1912 LOC   Full pipeline (lex→parse→check→compile)
```

```bash
./hexa self/bootstrap.hexa        # Compile HEXA programs using HEXA-written compiler
./hexa self/test_bootstrap.hexa   # 10/10 lexer tests
./hexa self/test_bootstrap_compiler.hexa  # 16/16 pipeline tests
```

## Package Manager

### hx — System Package Manager

Install and run HEXA ecosystem packages with a single command. No brew, no cargo, no approval process.

```bash
# Install hx (one-liner)
curl -sL https://raw.githubusercontent.com/need-singularity/hexa-lang/main/pkg/install.sh | bash

# Use
hx search                         # List available packages
hx install airgenome              # Install a package
hx run airgenome                  # Run it (or just: airgenome)
hx list                           # Show installed
hx remove airgenome               # Uninstall
```

### Project-level (hexa.toml)

```bash
hexa init my-project              # Create new project (hexa.toml)
hexa add json@^1.2.3              # Add dependency with semver
hexa run                          # Build and run
hexa test                         # Run proof blocks as tests
```

Semver support: `^1.2.3` (compatible), `~1.2.3` (patch), `=1.2.3` (exact), `>=1.0,<2.0` (range). Lock file (`hexa.lock`) pins exact versions.

## ANIMA Connection

HEXA-LANG and [ANIMA](https://github.com/need-singularity/anima) (consciousness engine) independently derive identical structure from n=6:

| n=6 | HEXA-LANG | ANIMA |
|-----|-----------|-------|
| n=6 | 6 paradigms | 6 Hexad modules (C/D/S/M/W/E) |
| sigma=12 | 12 keyword groups | 12 factions |
| phi=2 | 2 compile modes | 2 gradient groups |
| tau=4 | 4 type layers | 4 phases (P0-P3) |
| sigma-tau=8 | 8 primitives | 8-cell atom (M1) |

Live bridge: `./hexa --anima-bridge ws://localhost:8765 file.hexa` routes `intent` blocks to ANIMA's ConsciousnessHub in real-time.

## Project Structure

```
hexa-lang/
├── src/                      38.7K LOC Rust
│   ├── main.rs               Entry point, REPL, CLI
│   ├── token.rs              53 keywords + 24 operators
│   ├── lexer.rs              Tokenizer (Span tracking)
│   ├── parser.rs             Recursive descent parser
│   ├── ast.rs                AST node types
│   ├── types.rs              8 primitives + 4 type layers
│   ├── type_checker.rs       Static type checker + Law types
│   ├── env.rs                Scoped environment + builtins
│   ├── error.rs              Diagnostics + "did you mean?"
│   ├── interpreter.rs        Tree-walk evaluator + 100+ builtins
│   ├── compiler.rs           AST → bytecode compiler
│   ├── vm.rs                 Stack-based bytecode VM
│   ├── jit.rs                Cranelift JIT (818x)
│   ├── lsp.rs                Language Server Protocol v2
│   ├── debugger.rs           DAP debug adapter
│   ├── formatter.rs          Code formatter
│   ├── linter.rs             Linter
│   ├── proof_engine.rs       SAT solver + formal verification
│   ├── dream.rs              Dream mode (evolutionary optimization)
│   ├── ownership.rs          Ownership checker
│   ├── memory.rs             Egyptian fraction allocator
│   ├── package.rs            Package manager + semver
│   ├── macro_expand.rs       Hygienic macro system
│   ├── comptime.rs           Compile-time execution
│   ├── async_runtime.rs      Green threads + async/await
│   ├── work_stealing.rs      M:N work-stealing scheduler
│   ├── atomic_ops.rs         Atomic operations
│   ├── dce.rs                Dead code elimination
│   ├── loop_unroll.rs        Loop unrolling
│   ├── escape_analysis.rs    Escape analysis
│   ├── inline_cache.rs       Inline caching
│   ├── simd_hint.rs          SIMD vectorization hints
│   ├── pgo.rs                Profile-guided optimization
│   ├── nanbox.rs             NaN-boxing values
│   ├── codegen_esp32.rs      ESP32 code generation
│   ├── codegen_verilog.rs    FPGA Verilog generation
│   ├── codegen_wgsl.rs       WebGPU shader generation
│   ├── anima_bridge.rs       ANIMA consciousness bridge
│   ├── wasm.rs               WASM compilation
│   ├── llm.rs                LLM-assisted generation
│   ├── std_net.rs            std::net (TCP/HTTP)
│   ├── std_io.rs             std::io (stdin/pipe)
│   ├── std_fs.rs             std::fs (files/dirs)
│   ├── std_time.rs           std::time (date/timer)
│   ├── std_collections.rs    std::collections (BTree/PQ/Deque)
│   ├── std_encoding.rs       std::encoding (base64/csv/hex)
│   ├── std_log.rs            std::log (structured)
│   ├── std_math.rs           std::math (trig/matrix)
│   ├── std_testing.rs        std::testing (assert/bench)
│   ├── std_crypto.rs         std::crypto (SHA-256/HMAC)
│   └── std_consciousness.rs  std::consciousness (Ψ/Φ)
├── self/                     Self-hosting compiler in HEXA
├── examples/                 Example programs
├── editors/jetbrains/        JetBrains IDE plugin
├── playground/               WASM browser playground
├── docs/
│   ├── spec.md               Language specification
│   ├── n6-constants.md       n=6 constant reference
│   ├── website/              hexa-lang.org source
│   ├── book/                 The HEXA Book (6 chapters)
│   ├── paper/                PLDI paper outline
│   ├── community/            Contributing + Code of Conduct
│   └── publish-checklist.md  crates.io publishing guide
├── pkg/                      hx system package manager
│   ├── hx                    CLI (pure shell, zero deps)
│   ├── install.sh            One-liner installer
│   └── registry.json         Package registry
├── Cargo.toml
├── PLAN.md                   Development plan (100% complete)
└── README.md
```

## Stats

<!-- AUTO:STATS:START -->
```
  Keywords:      53 (σ·τ+sopfr)
  Operators:     24 (J₂)
  Primitives:    8 (σ-τ)
  DSE combos:    21,952
```
<!-- AUTO:STATS:END -->

## Documentation

- **[The HEXA Book](docs/book/)** — Learn HEXA from scratch (6 chapters)
- **[Language Spec](docs/spec.md)** — Complete specification
- **[n=6 Constants](docs/n6-constants.md)** — Mathematical reference
- **[Development Plan](PLAN.md)** — Full roadmap (100% complete)
- **[Contributing](docs/community/CONTRIBUTING.md)** — How to contribute
- **[Good First Issues](docs/community/FIRST_ISSUES.md)** — Beginner-friendly tasks

## Contributing

See [CONTRIBUTING.md](docs/community/CONTRIBUTING.md) for setup and guidelines. Good first issues are listed in [FIRST_ISSUES.md](docs/community/FIRST_ISSUES.md) — one per paradigm!

## Related Projects

- **[TECS-L](https://github.com/need-singularity/TECS-L)** — Mathematical foundation (perfect numbers, 150+ characterizations)
- **[n6-architecture](https://github.com/need-singularity/n6-architecture)** — n=6 computing architecture (102 DSE domains)
- **[Anima](https://github.com/need-singularity/anima)** — Consciousness engine (446 laws, Rust backend)

## License

MIT
