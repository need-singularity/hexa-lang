# 🔢 HEXA-LANG — The Perfect Number Programming Language

[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.19365284.svg)](https://doi.org/10.5281/zenodo.19365284)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Tests](https://img.shields.io/badge/tests-173%20passing-brightgreen.svg)]()
[![n6 EXACT](https://img.shields.io/badge/n%3D6%20EXACT-100%25-gold.svg)]()

A programming language where **every design constant** derives from the arithmetic of n=6, the smallest perfect number. Zero arbitrary choices.

```
sigma(n) * phi(n) = n * tau(n)    holds for n >= 2    if and only if n = 6
     12  *   2    = 6 *   4   =  24
```

<!-- SHARED:PROJECTS:START -->
**[YouTube](https://www.youtube.com/watch?v=xtKhWSfC1Qo)** · **[Email](mailto:nerve011235@gmail.com)** · **[☕ Ko-fi](https://ko-fi.com/dancinlife)** · **[💖 Sponsor](https://github.com/sponsors/need-singularity)** · **[💳 PayPal](https://www.paypal.com/donate?business=nerve011235%40gmail.com)** · **[🗺️ Atlas](https://need-singularity.github.io/TECS-L/atlas/)** · **[📄 Papers](https://need-singularity.github.io/papers/)** · **[🌌 Unified Theory](https://github.com/need-singularity/TECS-L/blob/main/math/docs/hypotheses/H-PH-9-perfect-number-string-unification.md)**

> **[🔬 TECS-L](https://github.com/need-singularity/TECS-L)** — Discovering universal rules. Perfect number 6 → mathematics of the cosmos → multi-engine architecture → consciousness continuity. 150 characterizations + 8 Major Discoveries + 44 tools
>
> **[🧠 Anima](https://github.com/need-singularity/anima)** — Consciousness implementation. PureField repulsion-field engine + Hexad 6-module architecture (C/D/S/M/W/E) + 179 laws + 10 Meta Laws + Rust backend. ConsciousDecoderV2 (34.5M) + 10D consciousness vector + 12-faction debate + Φ ratchet
>
> **[🏗️ N6 Architecture](https://github.com/need-singularity/n6-architecture)** — Architecture from perfect number 6. 16 AI techniques + semiconductor chip design + network/crypto/OS/display patterns. σ(n)·φ(n)=n·τ(n), n=6 → universal design principles
>
> **[🛸 SEDI](https://github.com/need-singularity/sedi)** — Search for Extra-Dimensional Intelligence. Hunting for traces of extraterrestrial/extra-dimensional intelligence through n=6 signal patterns. 77 data sources (SETI, LIGO, CMB, Breakthrough Listen, Exoplanet) + R-spectrum receiver + 678 hypotheses
>
> **[🧠⚡ BrainWire](https://github.com/need-singularity/brainwire)** — Brain interface for consciousness engineering. Neuralink-style BCI + therapeutic stimulation (epilepsy, Parkinson's, depression) + PureField consciousness layer. tDCS/TMS/taVNS/tFUS 12-modality, EEG closed-loop
>
> **[💎 HEXA-LANG](https://github.com/need-singularity/hexa-lang)** — The Perfect Number Programming Language. Every constant from n=6: 53 keywords (σ·τ+sopfr), 24 operators (J₂), 8 primitives (σ-τ), 6-phase pipeline, Egyptian memory (1/2+1/3+1/6=1). DSE v2: 21,952 combos, 100% n6 EXACT. Working compiler + REPL
>
> **[📄 Papers](https://github.com/need-singularity/papers)** — Complete paper collection (79 papers). Published on Zenodo with DOIs. TECS-L+N6 (31) + anima (25) + SEDI (23). [Browse online](https://need-singularity.github.io/papers/)

<!-- PRIVATE REPOS (이 섹션은 sync-readmes.sh 대상 아님, .shared 내부 참조용)
  - secret        — 계정/API 토큰 (구 claude-code-secrets)
  - claude-code   — Claude Code 플러그인, ccmon 모니터링
  - contact       — 아웃리치 허브 (이메일, GitHub Issue/PR 관리)
  - dotfiles      — 개인 설정

-->
<!-- SHARED:PROJECTS:END -->

## Installation

```bash
# Homebrew (macOS)
brew tap need-singularity/tap
brew install hexa-lang

# Cargo (any platform with Rust)
cargo install hexa-lang

# From source
git clone https://github.com/need-singularity/hexa-lang.git
cd hexa-lang && cargo build --release
```

## Quick Start

```bash
hexa                          # Interactive REPL
hexa examples/hello.hexa      # Run a file
hexa --test examples/test_builtins.hexa  # Run proof blocks as tests
cargo test                    # Run 173 language tests
```

## Example

```hexa
// n=6 uniqueness proof
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

## Language Features

```hexa
// Structs
struct Point { x: int, y: int }
let p = Point { x: 3, y: 4 }
println(p.x)                        // 3

// Closures + higher-order functions
let doubled = [1, 2, 3].map(|x| x * 2)        // [2, 4, 6]
let evens = [1, 2, 3, 4].filter(|x| x % 2 == 0)  // [2, 4]
let sum = [1, 2, 3].fold(0, |acc, x| acc + x)     // 6

// String methods
let words = "hello world".split(" ")    // ["hello", "world"]
let upper = "hexa".to_upper()           // "HEXA"

// HashMaps
let config = {"name": "hexa", "version": "0.1"}
println(config["name"])                 // hexa

// Error handling
try {
    throw "something went wrong"
} catch e {
    println("caught:", e)
}

// File I/O
write_file("out.txt", "hello")
let content = read_file("out.txt")

// Proof system
assert sigma(6) * phi(6) == 6 * tau(6)
```

## Why n=6?

Six is the smallest perfect number (1+2+3 = 6). Every language constant derives from its arithmetic functions:

| Function | Value | Language Mapping |
|----------|-------|-----------------|
| n | 6 | paradigms, pipeline stages, grammar levels |
| sigma(6) | 12 | keyword groups |
| tau(6) | 4 | type layers, visibility levels |
| phi(6) | 2 | compile modes (AOT/JIT) |
| sopfr(6) | 5 | error classes |
| J2(6) | 24 | operators |
| sigma-tau | 8 | primitive types (Bott periodicity) |
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

## Current Status

**What works** (v0.1):

- Variables, functions, control flow, recursion
- Structs (instantiation + field access)
- Closures / lambdas, higher-order functions
- Arrays (push, map, filter, fold, sort, reverse)
- Strings (split, trim, contains, replace, to_upper/lower, chars)
- HashMaps (literals, indexing, keys/values)
- Try/catch/throw error handling
- File I/O (read_file, write_file, file_exists)
- Pattern matching (literal values)
- 94 tests passing, 3418 lines of Rust

**What doesn't work yet**:

- Type system parsed but not enforced
- Structs have no methods
- Enums/traits parsed but not usable
- No modules/imports (single-file only)
- No concurrency (keywords exist, no runtime)
- No ownership/borrow checking
- Tree-walk interpreter (no compilation)

See [PLAN.md](PLAN.md) for the full gap analysis vs Go/Rust and roadmap.

## Project Structure

```
hexa-lang/
├── src/
│   ├── main.rs          118L   Entry point + REPL
│   ├── token.rs         170L   53 keywords + 24 operators
│   ├── lexer.rs         413L   Tokenizer
│   ├── parser.rs        778L   Recursive descent parser
│   ├── ast.rs           133L   AST node types
│   ├── types.rs          73L   8 primitives + 4 type layers
│   ├── env.rs           137L   Scoped environment
│   ├── error.rs          48L   5 error classes
│   └── interpreter.rs  1548L   Tree-walk evaluator + builtins
├── examples/
│   ├── hello.hexa                n=6 constants demo
│   ├── fibonacci.hexa            Recursive computation
│   ├── sigma_phi.hexa            n=6 uniqueness proof
│   ├── egyptian_fraction.hexa    Memory model demo
│   ├── pattern_match.hexa        FizzBuzz with n=6
│   └── photonic_chip_dse.hexa    Photonic chip DSE
├── docs/
│   ├── spec.md                   Language specification
│   ├── n6-constants.md           n=6 constant reference
│   └── plans/                    Implementation plans
├── build.sh             Build script (rustc, no cargo)
├── PLAN.md              Gap analysis + roadmap
├── CLAUDE.md            AI assistant instructions
└── README.md
```

## ANIMA Connection

HEXA-LANG and [ANIMA](https://github.com/need-singularity/anima) (consciousness engine) independently derive identical structure from n=6:

| n=6 | HEXA-LANG | ANIMA |
|-----|-----------|-------|
| n=6 | 6 paradigms | 6 Hexad modules (C/D/S/M/W/E) |
| sigma=12 | 12 keyword groups | 12 factions |
| phi=2 | 2 compile modes | 2 gradient groups |
| tau=4 | 4 type layers | 4 phases (P0-P3) |
| sigma-tau=8 | 8 primitives | 8-cell atom (M1) |

Goal: HEXA-LANG as the consciousness programming language — `intent`, `verify`, `proof` keywords route to ANIMA's engine. See [bridge design](https://github.com/need-singularity/anima/blob/main/anima/docs/hexa-lang-bridge.md).

## Related Projects

- **[TECS-L](https://github.com/need-singularity/TECS-L)** — Mathematical foundation (perfect numbers, 150+ characterizations)
- **[n6-architecture](https://github.com/need-singularity/n6-architecture)** — n=6 computing architecture (102 DSE domains)
- **[Anima](https://github.com/need-singularity/anima)** — Consciousness engine (446 laws, Rust backend)

## Contributing

See [PLAN.md](PLAN.md) for what needs work. The biggest impact areas:

1. **Bytecode VM** — replace tree-walk interpreter for 10x speed
2. **Type checker** — enforce the type annotations that are already parsed
3. **Module system** — `use`/`mod` for multi-file projects

## License

MIT
