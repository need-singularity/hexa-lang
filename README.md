# 💎 HEXA-LANG — The Perfect Number Programming Language

<!-- ML-NEXT-LEVEL:START -->
## ML Next Level Roadmap (`/ml`)

| Command | Action |
|---------|--------|
| `/ml` | List all items in ASCII table |
| `/ml next` | Pick the top-priority item and start implementing |
| `/ml 5` | Start implementing #5 (SSM/Mamba) |
| `/ml done 1` | Mark #1 complete + sync JSON |
| `/ml add name \| desc \| impact` | Add a new item |
| `/ml sync` | Sync roadmap.md + CLAUDE.md |

> SSOT: `shared/hexa-lang/ml-next-level.json` — next steps after the baseline T1/T2 pipeline is complete.

| # | Area | Description | Impact | Status |
|---|------|-------------|--------|:------:|
| 1 | Flash Attention | O(N²)→O(N) memory via tiling | 2–4× inference, long context | partial |
| 2 | PagedAttention | vLLM-style KV-cache paging | 10×+ concurrent requests | todo |
| 3 | Speculative Decoding | Draft with a small model, verify with the large one | 2–3× inference | **done** |
| 4 | MoE Architecture | Mixture of Experts (Mixtral-style) | Larger model at same FLOPs | **done** |
| 5 | SSM/Mamba | State Space Model (linear-time inference) | Long-context breakthrough | todo |
| 6 | Vision Transformer | Image encoder (CLIP/ViT) | Multimodal | todo |
| 7 | Model Merging | TIES / DARE / SLERP weight composition | Improve models without training | todo |
| 8 | Structured Output | JSON mode, grammar-constrained decode | API stability | todo |
| 9 | Function Calling | Tool use / function calling | Agents | todo |
| 10 | Knowledge Distillation | Large → small model distillation | Deployment efficiency | todo |
| 11 | Pruning | Remove unnecessary weights | Model shrink | todo |
| 12 | Hyperparameter Search | Automatic LR/batch/architecture search | Training quality | partial |
| 13 | Safety Layer | Prompt-injection detection, content filter | Serving security | todo |
| 14 | Semantic Cache | Similar-prompt caching (embedding-based) | Serving cost reduction | todo |
| 15 | Benchmark Suite | MMLU / HumanEval / MT-Bench automation | Quality measurement | todo |

<!-- ML-NEXT-LEVEL:END -->

<!-- ROI:START -->
## AI-Native ROI Improvements (`/roi`)

| Command | Action |
|---------|--------|
| `/roi` | Full list in ROI order (ASCII table) |
| `/roi go` | Launch every item in parallel, ROI order (main) |
| `/roi next` | Plan the top ROI item → confirm → implement |
| `/roi 3` | Start implementing #3 |
| `/roi done 1` | Mark #1 complete + sync JSON |
| `/roi add name \| desc \| impact×x \| hoursh` | Add a new item |
| `/roi scan` | Scan ai_native_pass → suggest new items |

> SSOT: `shared/hexa-lang/roi.json` — AI-native algorithm swaps, `@attr` semantics, rewrite reinforcement.

| # | Category | Item | Impact (×) | Hours | ROI | Priority | Status |
|---|----------|------|-----------:|------:|----:|:--------:|:------:|
| 1 | attr_semantics | @contract — drop bounds via requires/ensures | 3.0 | 2 | 1.50 | P0 | todo |
| 2 | algo_rewrite | Linear search → binary search AST rewrite | 10.0 | 8 | 1.25 | P1 | todo |
| 3 | attr_semantics | @symbolic — x\*2→x+x, shift↔pow2 | 7.5 | 8 | 0.94 | P1 | todo |
| 7 | rewrite_enhance | @algebraic arithmetic/geometric series → closed form | 5.0 | 6 | 0.83 | P2 | todo |
| 4 | rewrite_enhance | @fuse reduce/fold chain fusion | 3.0 | 4 | 0.75 | P2 | todo |
| 5 | rewrite_enhance | Constant folding — string concat + len | 2.0 | 3 | 0.67 | P2 | todo |
| 13 | rewrite_enhance | Strength reduction — x/2→x>>1, x%2→x&1 | 2.0 | 4 | 0.50 | P2 | todo |
| 8 | rewrite_enhance | DCE — remove unused variables / imports | 1.5 | 4 | 0.38 | P3 | todo |
| 6 | attr_semantics | @approximate — Newton sqrt, Taylor exp | 2.0 | 6 | 0.33 | P2 | todo |
| 9 | algo_rewrite | DP sliding-window memory reduction | 3.0 | 10 | 0.30 | P3 | todo |
| 10 | algo_rewrite | Matmul → Strassen O(n³)→O(n^2.37) | 5.0 | 20 | 0.25 | P3 | todo |
| 12 | attr_semantics | @speculative_decode — draft + verify | 3.0 | 12 | 0.25 | P4 | todo |
| 11 | attr_semantics | @specialize — V8-style IC type monomorphization | 7.5 | 40 | 0.19 | P3 | todo |

<!-- ROI:END -->

<!-- SHARED:PROJECTS:START -->
<!-- AUTO:COMMON_LINKS:START -->
**[YouTube](https://www.youtube.com/watch?v=xtKhWSfC1Qo)** · **[Email](mailto:nerve011235@gmail.com)** · **[☕ Ko-fi](https://ko-fi.com/dancinlife)** · **[💖 Sponsor](https://github.com/sponsors/need-singularity)** · **[💳 PayPal](https://www.paypal.com/donate?business=nerve011235%40gmail.com)** · **[🗺️ Atlas](https://need-singularity.github.io/TECS-L/atlas/)** · **[📄 Papers](https://need-singularity.github.io/papers/)** · **[🌌 Unified Theory](https://github.com/need-singularity/TECS-L/blob/main/math/docs/hypotheses/H-PH-9-perfect-number-string-unification.md)**
<!-- AUTO:COMMON_LINKS:END -->

> **[🔭 NEXUS](https://github.com/need-singularity/nexus)** — Universal Discovery Engine. 216 lenses + OUROBOROS evolution + LensForge + BlowupEngine + CycleEngine (5-phase singularity cycle). Mirror Universe (N×N resonance) + 9-project autonomous growth ecosystem. Rust CLI: scan, loop, mega, daemon, blowup, dispatch
>
> **[🧠 Anima](https://github.com/need-singularity/anima)** — Consciousness implementation. PureField repulsion-field engine + Hexad 6-module architecture (C/D/S/M/W/E) + 1030 laws + 20 Meta Laws + Rust backend. ConsciousDecoderV2 (34.5M) + 10D consciousness vector + 12-faction debate + Φ ratchet
>
> **[🏗️ N6 Architecture](https://github.com/need-singularity/n6-architecture)** — Architecture from perfect number 6. 225 AI techniques + semiconductor chip design + network/crypto/OS/display patterns. σ(n)·φ(n)=n·τ(n), n=6 → universal design principles. NEXUS-6 Discovery Engine: Rust CLI (tools/nexus/) — telescope 22 lenses + OUROBOROS evolution + discovery graph + verifier + 1116 tests
>
> **[💎 HEXA-LANG](https://github.com/need-singularity/hexa-lang)** — The Perfect Number Programming Language. Every constant from n=6: 53 keywords (σ·τ+sopfr), 24 operators (J₂), 8 primitives (σ-τ), 6-phase pipeline, Egyptian memory (1/2+1/3+1/6=1). DSE v2: 21,952 combos, 100% n6 EXACT. Working compiler + REPL
>
> **[📄 Papers](https://github.com/need-singularity/papers)** — Complete paper collection (94 papers). Published on Zenodo with DOIs. N6 (33) + anima (39) + 기타 (20). [Browse online](https://need-singularity.github.io/papers/)

<!-- private repos는 projects.json의 private_repos 필드에 저장됨 (노출 금지) -->
<!-- SHARED:PROJECTS:END -->


[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.19365284.svg)](https://doi.org/10.5281/zenodo.19365284)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
<!-- AUTO:BADGE:START -->
[![Keywords](https://img.shields.io/badge/Keywords-58-blue.svg)]()
[![DSE](https://img.shields.io/badge/DSE-21,952%20combos-gold.svg)]()
<!-- AUTO:BADGE:END -->

A programming language where **every design constant** derives from the arithmetic of n=6, the smallest perfect number. Zero arbitrary choices.

```
sigma(n) * phi(n) = n * tau(n)    holds for n >= 2    if and only if n = 6
     12  *   2    = 6 *   4   =  24
```

## Installation

### One-liner (recommended)

```bash
hexa <(curl -fsSL https://raw.githubusercontent.com/need-singularity/hexa-lang/main/install.hexa)
```

Installs both commands into `~/.hx/bin/`:

| Command | Role |
|---------|------|
| `hexa`  | Compiler, interpreter, REPL, LSP, formatter |
| `hx`    | Package manager (brew-style, pure shell, zero deps) |

After install, add `~/.hx/bin` to your `PATH` (the installer appends it to `~/.zshrc` / `~/.bashrc` automatically) and restart your shell, or run:

```bash
export PATH="$HOME/.hx/bin:$PATH"
hexa version
hx search
```

Env overrides: `HX_HOME=/custom/prefix`, `HEXA_VERSION=v0.2.0`, `HEXA_SKIP_HX=1`, `HEXA_SKIP_HEXA=1`.

### From source

```bash
# 100% self-hosted — no Rust toolchain required
git clone https://github.com/need-singularity/hexa-lang.git
cd hexa-lang
./hexa run examples/hello_min.hexa   # stage1 CLI, go/cargo-style subcommands
```

> As of 2026-04-13, the CLI uses go/cargo-style subcommands. `./hexa` is a 77 KB stage1 dispatcher; the interpreter itself is the 351 KB binary archived at `build/hexa_stage0`. Rust (`src/`, `target/`, `Cargo.*`) has been fully retired — 100% self-hosted.

## Quick Start

```bash
hexa run examples/hello_min.hexa             # Run a .hexa file (interpreter)
hexa build examples/hello_min.hexa -o hello  # Compile to native binary
hexa status                                  # Toolchain status
hexa version                                 # Version
hexa help                                    # Full usage

# Compat mode (python/node style — delegates to `run` internally):
hexa examples/hello_min.hexa                 # same as `hexa run ...`
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
| sigma(6) | 12 | stdlib design target (6 currently implemented) |
| tau(6) | 4 | type layers, visibility levels |
| phi(6) | 2 | compile modes (AOT/JIT) |
| sopfr(6) | 5 | error classes |
| J2(6) | 24 | operator design origin (46 currently implemented across 9 groups) |
| sigma-tau | 8 | primitive types |
| -- | 65 | total keywords (14 groups combined) |

Other languages choose these numbers arbitrarily. Rust has 51 keywords -- why 51? No mathematical reason. HEXA has 65 keywords in 14 groups, each group size derived from n=6 arithmetic functions.

## Architecture

```
Source → Tokenize → Parse → Check → Optimize → Codegen → Execute
         (1/6)     (2/6)   (3/6)    (4/6)      (5/6)     (6/6)
```

**6 paradigms**: imperative, functional, OOP, concurrent, logic/proof, AI-native

**8 primitives** (sigma-tau = 8): `int`, `float`, `bool`, `char`, `string`, `byte`, `void`, `any`

**4 type layers** (tau = 4): primitive → composite → reference → function

**46 operators** (9 groups): arithmetic(6) + comparison(6) + logical(4) + bitwise(6) + assignment(8) + special(4) + null_safe(4) + type_check(4) + slice(4)

**65 keywords** in **14 groups**:

| Group | Keywords | Count |
|-------|----------|-------|
| Control | if, else, match, for, while, loop, break, continue, do, defer | 10 |
| Type | type, struct, enum, trait, impl, dyn | 6 |
| Functions | fn, return, yield, async, await | 5 |
| Variables | let, mut, const, static | 4 |
| Modules | mod, use, pub, crate | 4 |
| Memory | own, borrow, move, drop | 4 |
| Concurrency | spawn, channel, select, atomic, scope | 5 |
| Effects | effect, handle, resume, pure | 4 |
| Proofs | proof, assert, invariant, theorem | 4 |
| Meta | macro, derive, where, comptime | 4 |
| Errors | try, catch, throw, panic, recover | 5 |
| AI | intent, generate, verify, optimize | 4 |
| FFI | extern | 1 |
| TypeOps | as, is, in, typeof, step | 5 |

**Memory model** — Egyptian fraction: 1/2 (stack) + 1/3 (heap) + 1/6 (arena) = 1

## Standard Library (6 modules implemented)

| Module | Description | Key Functions |
|--------|-------------|---------------|
| stdlib/math.hexa | Mathematics | trig, matrix, BigInt, constants |
| stdlib/string.hexa | String utilities | split, trim, replace |
| stdlib/collections.hexa | Data structures | BTreeMap, PriorityQueue, Deque |
| stdlib/nn.hexa | Neural network | tensor, matmul, softmax |
| stdlib/optim.hexa | Optimization | SGD, Adam, learning rate |
| stdlib/consciousness.hexa | Consciousness DSL | phi_compute, psi_constants, law_lookup |

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
self/lexer.hexa          753 LOC   Tokenizer
self/parser.hexa        4007 LOC   Recursive descent parser
self/type_checker.hexa  1656 LOC   Static type checker
self/compiler.hexa       858 LOC   Bytecode compiler
self/bootstrap.hexa     1945 LOC   Full pipeline (lex->parse->check->compile)
```

```bash
./hexa run self/bootstrap.hexa        # Compile HEXA programs using HEXA-written compiler
./hexa run self/test_bootstrap.hexa   # 10/10 lexer tests
./hexa run self/test_bootstrap_compiler.hexa  # 16/16 pipeline tests
```

## Package Manager

### hx — System Package Manager

Install and run HEXA ecosystem packages with a single command. No brew, no cargo, no approval process.

```bash
# Install hx (one-liner)
hexa <(curl -sL https://raw.githubusercontent.com/need-singularity/hexa-lang/main/install.hexa)

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
| sigma=12 | 12 factions (matched) | 12 factions |
| phi=2 | 2 compile modes | 2 gradient groups |
| tau=4 | 4 type layers | 4 phases (P0-P3) |
| sigma-tau=8 | 8 primitives | 8-cell atom (M1) |

Live bridge: `./hexa --anima-bridge ws://localhost:8765 file.hexa` routes `intent` blocks to ANIMA's ConsciousnessHub in real-time.

## Project Structure

```
hexa-lang/
├── self/                     1,113 .hexa files (primary source)
│   ├── lexer.hexa            753 LOC — 65 keywords + 46 operators tokenizer
│   ├── parser.hexa           4,007 LOC — recursive descent parser
│   ├── interpreter.hexa      7 LOC — flatten stub (logic merged into codegen_c2/hexa_full)
│   ├── ast.hexa              AST node types
│   ├── type_checker.hexa     Static type checker + Law types
│   ├── env.hexa              Scoped environment + builtins
│   ├── error.hexa            Diagnostics + "did you mean?"
│   ├── compiler.hexa         AST → bytecode compiler
│   ├── vm.hexa               Stack-based bytecode VM
│   ├── jit.hexa              Cranelift JIT bridge
│   ├── lsp.hexa              Language Server Protocol v2
│   ├── debugger.hexa         DAP debug adapter
│   ├── formatter.hexa        Code formatter
│   ├── linter.hexa           Linter
│   ├── proof_engine.hexa     SAT solver + formal verification
│   ├── dream.hexa            Dream mode (evolutionary optimization)
│   ├── ownership.hexa        Ownership checker
│   ├── memory.hexa           Egyptian fraction allocator
│   ├── package.hexa          Package manager + semver
│   ├── macro_expand.hexa     Hygienic macro system
│   ├── comptime.hexa         Compile-time execution
│   ├── async_runtime.hexa    Green threads + async/await
│   ├── work_stealing.hexa    M:N work-stealing scheduler
│   ├── atomic_ops.hexa       Atomic operations
│   ├── dce.hexa              Dead code elimination
│   ├── loop_unroll.hexa      Loop unrolling
│   ├── escape_analysis.hexa  Escape analysis
│   ├── inline_cache.hexa     Inline caching
│   ├── simd_hint.hexa        SIMD vectorization hints
│   ├── pgo.hexa              Profile-guided optimization
│   ├── nanbox.hexa           NaN-boxing values
│   ├── trace_jit.hexa        Hot loop detection + trace recording
│   ├── codegen_esp32.hexa    ESP32 code generation
│   ├── codegen_verilog.hexa  FPGA Verilog generation
│   ├── codegen_wgsl.hexa     WebGPU shader generation
│   ├── anima_bridge.hexa     ANIMA consciousness bridge
│   ├── llm.hexa              LLM-assisted generation
│   ├── ir/                   HEXA-IR (J₂=24 opcodes, τ=4 categories)
│   ├── lower/                AST → IR lowering
│   ├── opt/                  σ=12 optimization passes
│   ├── codegen/              ARM64/x86_64/ELF/Mach-O native codegen
│   ├── alloc/                Egyptian fraction allocator backend
│   ├── std_*.hexa            stdlib (net/fs/io/time/collections/...)
│   └── lib.hexa + main.hexa  Library entry + CLI dispatcher
├── src/                      92 .rs files (migration in progress — moving to self/)
├── build_hexa.hexa           Cargo-free self-host build pipeline
│                             (hexa_v2 -> C -> clang -> hexa_v3)
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
│   ├── install.hexa          One-liner installer
│   └─��� registry.tsv          Package registry
├── PLAN.md                   Development plan (100% complete)
└── README.md                 this file
```

## Stats

<!-- AUTO:STATS:START -->
```
  Keywords:      65 (14 groups)
  Operators:     46 (9 groups)
  Primitives:    8
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
