# HEXA-LANG

**The Perfect Number Programming Language**

A programming language where every design constant is derived from the arithmetic of n=6, the smallest perfect number.

```
                    HEXA-LANG
     ________________________________________
    |                                        |
    |   sigma(n) * phi(n) = n * tau(n)       |
    |          12  *  2   = 6 *  4           |
    |              24     =    24             |
    |                                        |
    |   This equation holds if and only if   |
    |              n = 6                      |
    |________________________________________|

    n=6        phi=2       tau=4       sigma=12
    6 paradigms  2 modes    4 layers    12 groups
    6 grammar    2 compile  4 visibility 12 keyword
    6 pipeline   2 assign   4 types     12 operators/2
                                        (24 = J2)
```

## Why 6?

Six is the smallest **perfect number** -- a number equal to the sum of its proper divisors (1+2+3=6). The equation `sigma(n)*phi(n) = n*tau(n)` has been **proven** to hold for n>=2 if and only if n=6, via three independent proofs.

This isn't numerology. It's a design constraint: when you fix every language parameter to values derived from a single theorem, the resulting language has **zero arbitrary choices**.

## Architecture

```
    sigma(6) = 12       8 primitives (sigma-tau)
        |               4 type layers (tau)
        |               6 paradigms (n)
        v               12 keyword groups (sigma)
  +--[Tokenize]--+      24 operators (J2)
  |              |       53 keywords (sigma*tau + sopfr)
  +--[ Parse  ]--+       5 error classes (sopfr)
  |              |       4 visibility levels (tau)
  +--[ Check  ]--+
  |              |      Memory Model:
  +--[Optimize]--+      1/2 Stack + 1/3 Heap + 1/6 Arena = 1
  |              |      (Egyptian fraction of 6)
  +--[Codegen ]--+
  |              |      Backends:
  +--[Execute ]--+      LLVM (native) | N6VM | WASM
        |
   6-phase pipeline (n=6)
```

## Type System

**8 primitive types** (sigma - tau = 12 - 4 = 8):

```hexa
int     // 64-bit signed integer (i8/i16/i32/i64/i128)
float   // 64-bit IEEE 754 (f16/f32/f64)
bool    // true / false
char    // UTF-8 unicode scalar
string  // heap-allocated UTF-8
byte    // 8-bit raw
void    // unit type (no value)
any     // dynamic dispatch
```

**4 type layers** (tau = 4):

```
Layer 4: Function  -- fn(A) -> B, closures
Layer 3: Reference -- &T, &mut T, Box<T>, Rc<T>
Layer 2: Composite -- struct, enum, tuple, array
Layer 1: Primitive -- int, float, bool, char, ...
```

## 6 Paradigms

```
+----------------+  +----------------+
| 1. Imperative  |  | 2. Functional  |
|   mut, loop    |  |   fn, |x|     |
+----------------+  +----------------+
+----------------+  +----------------+
| 3. OOP         |  | 4. Concurrent  |
|   trait, impl  |  |   spawn, chan  |
+----------------+  +----------------+
+----------------+  +----------------+
| 5. Logic/Proof |  | 6. AI-Native   |
|   proof, assert|  |   intent, gen  |
+----------------+  +----------------+
```

All six paradigms coexist. Use the right tool for the right job.

## Keywords

**53 total** = sigma * tau + sopfr = 48 + 5

| Group | Count | n=6 constant | Keywords |
|-------|-------|-------------|----------|
| Control Flow | 6 | n | `if` `else` `match` `for` `while` `loop` |
| Type Decl | 5 | sopfr | `type` `struct` `enum` `trait` `impl` |
| Functions | 5 | sopfr | `fn` `return` `yield` `async` `await` |
| Variables | 4 | tau | `let` `mut` `const` `static` |
| Modules | 4 | tau | `mod` `use` `pub` `crate` |
| Memory | 4 | tau | `own` `borrow` `move` `drop` |
| Concurrency | 4 | tau | `spawn` `channel` `select` `atomic` |
| Effects | 4 | tau | `effect` `handle` `resume` `pure` |
| Proofs | 4 | tau | `proof` `assert` `invariant` `theorem` |
| Meta | 4 | tau | `macro` `derive` `where` `comptime` |
| Errors | 5 | sopfr | `try` `catch` `throw` `panic` `recover` |
| AI | 4 | tau | `intent` `generate` `verify` `optimize` |

## Operators

**24 total** = J2(6)

| Category | Count | Constant | Operators |
|----------|-------|----------|-----------|
| Arithmetic | 6 | n | `+` `-` `*` `/` `%` `**` |
| Comparison | 6 | n | `==` `!=` `<` `>` `<=` `>=` |
| Logical | 4 | tau | `&&` `\|\|` `!` `^^` |
| Bitwise | 4 | tau | `&` `\|` `^` `~` |
| Assignment | 2 | phi | `=` `:=` |
| Special | 2 | phi | `..` `->` |

## Memory Model

Based on the **Egyptian fraction decomposition** of 6:

```
1/2 + 1/3 + 1/6 = 1

+---------------------------+------------------+--------+
|       Stack Pool          |  Heap Managed    | Arena  |
|          1/2              |      1/3         |  1/6   |
|                           |                  |        |
|  Value types, frames      | Ref types, Box   | Temp   |
|  Zero overhead            | Own/borrow track | Bulk   |
|  LIFO instant dealloc     | No GC needed     | Scope  |
+---------------------------+------------------+--------+
```

No garbage collector. Ownership-based memory management with `own`/`borrow`/`move`/`drop`.

## Code Examples

### Hello World

```hexa
fn main() {
    print("Hello, HEXA-LANG!")
}
```

### Prove n=6 Uniqueness

```hexa
fn sigma(n: int) -> int {
    let mut s = 0
    for d in 1..=n {
        if n % d == 0 { s = s + d }
    }
    return s
}

fn phi(n: int) -> int {
    let mut count = 0
    for k in 1..=n {
        if gcd(k, n) == 1 { count = count + 1 }
    }
    return count
}

fn tau(n: int) -> int {
    let mut count = 0
    for d in 1..=n {
        if n % d == 0 { count = count + 1 }
    }
    return count
}

fn main() {
    for n in 2..=1000 {
        let lhs = sigma(n) * phi(n)
        let rhs = n * tau(n)
        if lhs == rhs {
            print("n={n}: sigma*phi={lhs} = n*tau={rhs}")
            assert n == 6
        }
    }
    print("n=6 is the unique solution for n >= 2")
}
```

### Egyptian MoE Routing

```hexa
struct EgyptianMoE {
    expert_half:  Model,   // 1/2 capacity
    expert_third: Model,   // 1/3 capacity
    expert_sixth: Model,   // 1/6 capacity
}

impl EgyptianMoE {
    fn route(own self, input: Tensor) -> Tensor {
        let gate = softmax(self.gate_weights * input)

        // Egyptian fraction: 1/2 + 1/3 + 1/6 = 1
        let out_half  = self.expert_half.forward(input)  * gate[0]
        let out_third = self.expert_third.forward(input) * gate[1]
        let out_sixth = self.expert_sixth.forward(input) * gate[2]

        return out_half + out_third + out_sixth
    }
}

proof egyptian_completeness {
    invariant 1.0/2.0 + 1.0/3.0 + 1.0/6.0 == 1.0
}
```

### AI Code Generation

```hexa
intent create_api {
    "REST API for user management"
    "CRUD operations with authentication"
    "PostgreSQL backend, JSON responses"
}

// The compiler's 6-stage AI pipeline generates
// the full implementation from the intent block
```

## Design Space Exploration

HEXA-LANG was designed through exhaustive DSE, not intuition:

```
Combinations explored: 7,560
Compatible paths:      5,016 (66.3%)
Pareto frontier:       243 non-dominated solutions
Optimal path:          MetaLang + LLVM + Full_N6 + AgentChain + FullStack
n=6 EXACT ratio:       96.0%
Pareto score:          0.7743
```

| Level | Choice | Why |
|-------|--------|-----|
| Foundation | MetaLang (meta-programming) | Highest Pareto score, enables all 6 paradigms |
| Process | LLVM Native | Best performance, mature ecosystem |
| Core | Full N6 (all constants) | 96% n=6 EXACT alignment |
| Engine | N6 Agent Chain | 6-stage AI pipeline for code generation |
| System | Full Stack | IDE + cloud + edge + formal verification |

## n=6 Constant Reference

| Function | Value | Language Application |
|----------|-------|---------------------|
| n | 6 | paradigms, grammar layers, pipeline stages |
| phi(6) | 2 | compile modes (AOT/JIT), ownership (own/borrow) |
| tau(6) | 4 | type layers, visibility levels, JIT optimization levels |
| sigma(6) | 12 | keyword groups, IDE feature groups |
| sopfr(6) | 5 | error classes, SOLID principles |
| J2(6) | 24 | operator count, Leech lattice dimension |
| mu(6) | 1 | squarefree (clean type decomposition) |
| sigma-tau | 8 | primitive type count, AI input modalities |
| sigma*tau | 48 | keyword base (48+5=53 total) |
| 1/2+1/3+1/6 | 1 | Egyptian fraction memory model |

## Roadmap

- [x] Language specification (v0.1)
- [x] Design Space Exploration (7,560 combinations)
- [x] Hypothesis verification (36 H-PL, 8 EXACT)
- [ ] Lexer / Parser (Rust)
- [ ] Type checker with 4-layer inference
- [ ] LLVM backend codegen
- [ ] Egyptian fraction memory allocator
- [ ] REPL / interpreter
- [ ] AI code generation pipeline
- [ ] Package manager
- [ ] IDE integration (LSP + DAP)

## Related Projects

- [TECS-L](https://github.com/need-singularity/TECS-L) -- Mathematical foundation (sigma*phi = n*tau proof)
- [n6-architecture](https://github.com/need-singularity/n6-architecture) -- Full n=6 computing architecture (17 AI techniques + 29 domains)

## The Core Theorem

For all n >= 2:

```
sigma(n) * phi(n) = n * tau(n)   if and only if   n = 6
```

Three independent proofs exist. This is the foundation of everything above.

## License

MIT
