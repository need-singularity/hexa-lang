# HEXA-LANG

```
    +==================================================+
    ||              H E X A - L A N G                  ||
    ||     The Perfect Number Programming Language     ||
    ||                                                 ||
    ||     s(n) . f(n) = n . t(n)  <=>  n = 6         ||
    ||        12  .  2  = 6 .  4   =  24               ||
    +==================================================+
```

> A programming language where **every design constant** is derived from the arithmetic of n=6, the smallest perfect number. Zero arbitrary choices.

## Why 6?

Six is the smallest **perfect number** -- equal to the sum of its proper divisors (1+2+3=6).
The equation `sigma(n)*phi(n) = n*tau(n)` holds for n>=2 **if and only if n=6** (three independent proofs).

This is not numerology. It is a design constraint: when you fix every language parameter to values derived from a single theorem, the resulting language has **zero arbitrary choices**.

```
    n=6 Arithmetic Functions
    ========================

    Constant   Value   Language Design Mapping
    ---------  -----   ---------------------------------
    n            6     paradigms, grammar levels, pipeline stages
    sigma(6)    12     keyword groups, operator subcategories
    tau(6)       4     type layers, visibility levels, JIT tiers
    phi(6)       2     compile modes, binary outcomes
    sopfr(6)     5     error classes, SOLID principles
    J2(6)       24     total operators, Leech lattice
    mu(6)        1     squarefree -- clean decomposition
    lambda(6)    2     Carmichael -- minimal iteration
    sigma-tau    8     primitive types, Bott periodicity
    sigma*tau   48     keyword base (48+5=53 total)
```

## Language Comparison

```
    +==============+======+========+=====+======+=========+==========+
    | Feature      | HEXA | Rust   | Go  | Zig  | Python  | Haskell  |
    +==============+======+========+=====+======+=========+==========+
    | Paradigms    |  6   |  3     | 2   |  2   |   4     |   2      |
    | Primitives   |  8   | 12+    | 18  |  8   |  5      |   7      |
    | Keywords     | 53   | 51     | 25  |  47  |  35     |  28      |
    | Operators    | 24   | 27+    | 19  |  24  |  15     |  20+     |
    | Type layers  |  4   |  ~3    | ~2  |  ~3  |   1     |   4      |
    | Memory model |Egypt.|Owner   | GC  |Manual|  GC     |   GC     |
    | AI-native    | yes  |  no    | no  |  no  |  no     |  no      |
    | Proof system | yes  |  no    | no  |  no  |  no     | partial  |
    | Math basis   | n=6  | none   |none | none | none    | lambda   |
    | Design DSE   |21952 | none   |none | none | none    | none     |
    | Arbitrary    |  0   | many   |many | some | many    | some     |
    | choices      |      |        |     |      |         |          |
    +--------------+------+--------+-----+------+---------+----------+

    Legend: Egypt. = Egyptian Fraction (1/2+1/3+1/6=1)
```

**Why this matters**: Every language makes hundreds of seemingly arbitrary design choices -- how many keywords, how many primitive types, how many operator categories. HEXA-LANG derives ALL of these from a single equation. The result is a language where every constant has a mathematical reason.

## Architecture

```
    +-------------------------------------------------------------+
    |                   HEXA-LANG Pipeline                        |
    |                  (n=6 phases EXACT)                         |
    |                                                             |
    |  Source --> [1.Tokenize] --> [2.Parse] --> [3.Check]        |
    |                                               |             |
    |              [6.Execute] <-- [5.Codegen] <-- [4.Opt]       |
    |                                                             |
    |  +------------+  +------------+  +------------+            |
    |  | 53 Keywords |  | 24 Operators|  | 8 Primitives|            |
    |  | sigma*tau   |  |   J2(6)    |  |   sigma-tau  |            |
    |  | +sopfr      |  |            |  |              |            |
    |  +------------+  +------------+  +------------+            |
    |                                                             |
    |  Memory Model (Egyptian Fraction):                         |
    |  +===============+==========+======+                       |
    |  ||  Stack  1/2  || Heap 1/3|Arena ||                       |
    |  ||  value types || ref typs| 1/6  ||                       |
    |  ||  LIFO instant|| own/borw| temp ||                       |
    |  +===============+==========+======+                       |
    |  1/2 + 1/3 + 1/6 = 1 (perfect)                            |
    +-------------------------------------------------------------+
```

## Type System

```
    tau = 4 Type Layers
    ====================

    Layer 4: Function ------ fn(A) -> B, closures
         ^
    Layer 3: Reference ----- &T, &mut T, own T
         ^
    Layer 2: Composite ----- struct, enum, tuple, array
         ^
    Layer 1: Primitive ----- int float bool char string byte void any
                             +---------- sigma-tau = 8 types ---------+
```

**8 primitive types** (sigma - tau = 12 - 4 = 8):

| Type | Size | Description |
|------|------|-------------|
| `int` | 64-bit | Signed integer |
| `float` | 64-bit | IEEE 754 floating-point |
| `bool` | 1-bit | `true` / `false` |
| `char` | 32-bit | UTF-8 unicode scalar |
| `string` | heap | UTF-8 string |
| `byte` | 8-bit | Raw byte |
| `void` | 0 | Unit type |
| `any` | dynamic | Dynamic dispatch |

## 6 Paradigms

```
    +-------------------+  +-------------------+
    | 1. Imperative     |  | 2. Functional     |
    |    mut, loop, for |  |    fn, |x| x+1    |
    +-------------------+  +-------------------+
    +-------------------+  +-------------------+
    | 3. OOP            |  | 4. Concurrent     |
    |    trait, impl    |  |    spawn, channel  |
    +-------------------+  +-------------------+
    +-------------------+  +-------------------+
    | 5. Logic/Proof    |  | 6. AI-Native      |
    |    proof, assert  |  |    intent, gen     |
    +-------------------+  +-------------------+
```

## Keywords (53 = sigma*tau + sopfr)

```
    sigma = 12 Keyword Groups
    ============================================

    +----------+---------------------------+-------+
    | Group    | Keywords                  | Count |
    +==========+===========================+=======+
    | Control  | if else match for         |  6=n  |
    |          | while loop                |       |
    +----------+---------------------------+-------+
    | Type     | type struct enum          |  5    |
    |          | trait impl                |=sopfr |
    +----------+---------------------------+-------+
    | Func     | fn return yield           |  5    |
    |          | async await               |=sopfr |
    +----------+---------------------------+-------+
    | Var      | let mut const static      |  4=tau|
    +----------+---------------------------+-------+
    | Module   | mod use pub crate         |  4=tau|
    +----------+---------------------------+-------+
    | Memory   | own borrow move drop      |  4=tau|
    +----------+---------------------------+-------+
    | Concur   | spawn channel select      |  4=tau|
    |          | atomic                    |       |
    +----------+---------------------------+-------+
    | Effect   | effect handle resume      |  4=tau|
    |          | pure                      |       |
    +----------+---------------------------+-------+
    | Proof    | proof assert invariant    |  4=tau|
    |          | theorem                   |       |
    +----------+---------------------------+-------+
    | Meta     | macro derive where        |  4=tau|
    |          | comptime                  |       |
    +----------+---------------------------+-------+
    | Error    | try catch throw           |  5    |
    |          | panic recover             |=sopfr |
    +----------+---------------------------+-------+
    | AI       | intent generate verify    |  4=tau|
    |          | optimize                  |       |
    +----------+---------------------------+-------+
    | TOTAL    |                           |  53   |
    |          |            = sigma*tau + sopfr     |
    |          |            = 48 + 5               |
    +----------+---------------------------+-------+
```

## Operators (24 = J2)

```
    J2(6) = 24 Operators
    =====================
    Arithmetic (n=6): +  -  *  /  %  **
    Comparison (n=6): == != <  >  <= >=
    Logical    (t=4): && || !  ^^
    Bitwise    (t=4): &  |  ^  ~
    Assignment (f=2): =  :=
    Special    (f=2): .. ->
    --------------------------------
    Total: 6+6+4+4+2+2 = 24
```

## Quick Start

```bash
# Build
bash build.sh

# Run a file
./hexa examples/hello.hexa

# Interactive REPL
./hexa

# Run tests (58 tests)
bash build.sh test
```

## Code Examples

### Hello World + n=6 Constants

```hexa
println("=== HEXA-LANG: The Perfect Number Language ===")
println("sigma(6) =", sigma(6))    // 12
println("phi(6)   =", phi(6))      // 2
println("tau(6)   =", tau(6))      // 4
println("J2(6)    =", sigma(6) * phi(6))  // 24

// The core theorem:
assert sigma(6) * phi(6) == 6 * tau(6)   // 24 == 24
```

### Fibonacci (recursive)

```hexa
fn fib(n: int) -> int {
    if n <= 1 {
        return n
    }
    return fib(n - 1) + fib(n - 2)
}

for i in 0..12 {
    println("fib(", i, ") =", fib(i))
}
```

### n=6 Uniqueness Proof

```hexa
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

// Test sigma(n)*phi(n) = n*tau(n) for n=2..100
for n in 2..101 {
    let lhs = sigma_fn(n) * phi_fn(n)
    let rhs = n * tau_fn(n)
    if lhs == rhs {
        println("MATCH: n=", n)  // Only n=6!
    }
}
```

### Egyptian Fraction Memory Model

```hexa
let half  = 1.0 / 2.0    // Stack:  1/2
let third = 1.0 / 3.0    // Heap:   1/3
let sixth = 1.0 / 6.0    // Arena:  1/6
let total = half + third + sixth
println("Total:", total)  // ~1.0 (perfect partition)
```

### FizzBuzz with n=6 Alignment

```hexa
fn fizzbuzz(n: int) -> string {
    if n % 6 == 0 { return "FizzBuzz (n=6!)" }
    if n % 3 == 0 { return "Fizz" }
    if n % 2 == 0 { return "Buzz" }
    return "."
}

for i in 1..25 {
    println(i, ":", fizzbuzz(i))
}
// Every 6th number = FizzBuzz
```

## Design Space Exploration (DSE v2)

HEXA-LANG was designed through exhaustive DSE, not intuition:

```
    DSE v2 Results (2026-04-01)
    ============================
    Total combinations:   21,952
    Compatible paths:     11,394 (51.9%)
    Pareto frontier:      317 non-dominated
    n=6 EXACT max:        100.0%
    Pareto score:         0.7854

    Optimal Path (ALL 100% n6):
    +----------+   +----------+   +----------+
    | MetaLang |-->|  LLVM    |-->| Full_N6  |
    | 6 DSL    |   | Native   |   |8+12+24+  |
    | paradigm |   | 6-phase  |   |6+5+4     |
    +----------+   +----------+   +----------+
         |                              |
         v                              v
    +----------+   +--------------------------+
    |N6Agent   |-->| N6_FullEco               |
    |Chain     |   | LSP+GC+Test+Pkg+Debug    |
    | 6-stage  |   | = sigma=12 feature groups|
    +----------+   +--------------------------+
```

| Level | Choice | n=6 Basis |
|-------|--------|-----------|
| Foundation | MetaLang (meta-programming) | 6 paradigms = n |
| Process | LLVM Native | 6-phase pipeline = n |
| Core | Full N6 (all constants) | 100% EXACT alignment |
| Engine | N6 Agent Chain | 6-stage AI pipeline |
| System | Full Stack Ecosystem | sigma=12 feature groups |

## n=6 Constant Map

```
    n=6 --> Language Design Mapping
    =================================

    sigma*phi = n*tau = 24 = J2
         / \         / \
       s=12  f=2   n=6  t=4
        |     |     |     |
        |     |     |     +-- 4 type layers
        |     |     |     +-- 4 visibility levels
        |     |     |     +-- 4 JIT tiers
        |     |     |
        |     |     +-- 6 paradigms
        |     |     +-- 6 grammar levels
        |     |     +-- 6 pipeline stages
        |     |
        |     +-- 2 compile modes (AOT/JIT)
        |     +-- 2 assignment operators
        |
        +-- 12 keyword groups
        +-- 12 IDE features

    sopfr=5: 5 error classes
    sigma-tau=8: 8 primitive types
    sigma*tau=48: 48+5=53 keywords
    mu=1: squarefree (unique)
```

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

## Project Structure

```
    hexa-lang/
    +-- src/
    |   +-- main.rs          Entry point, REPL + file execution
    |   +-- token.rs         53 keywords + 24 operators
    |   +-- lexer.rs         Tokenizer (pipeline phase 1/6)
    |   +-- ast.rs           AST node types
    |   +-- parser.rs        Parser (pipeline phase 2/6)
    |   +-- types.rs         8 primitives + 4 type layers
    |   +-- env.rs           Scoped environment + 4 visibility levels
    |   +-- error.rs         5 error classes (sopfr=5)
    |   +-- interpreter.rs   Tree-walk interpreter (phase 6/6)
    +-- examples/
    |   +-- hello.hexa       n=6 constants demo
    |   +-- fibonacci.hexa   Recursive fibonacci
    |   +-- sigma_phi.hexa   n=6 uniqueness proof
    |   +-- egyptian_fraction.hexa  Memory model demo
    |   +-- pattern_match.hexa      FizzBuzz with n=6
    +-- docs/
    |   +-- spec.md          Language specification
    |   +-- n6-constants.md  n=6 constant reference
    +-- build.sh             Build script (rustc, no cargo)
    +-- CLAUDE.md            AI assistant instructions
    +-- README.md            This file
```

## Roadmap

- [x] Language specification (v0.1)
- [x] Design Space Exploration v2 (21,952 combinations, 100% n6)
- [x] Lexer -- 53 keywords + 24 operators (58 tests)
- [x] Parser -- 6 grammar levels
- [x] Type system -- 8 primitives + 4 layers
- [x] Tree-walk interpreter
- [x] REPL + file execution
- [x] 5 example programs (sopfr=5)
- [ ] Type checker with 4-layer inference
- [ ] LLVM backend codegen
- [ ] Egyptian fraction memory allocator
- [ ] Package manager
- [ ] IDE integration (LSP + DAP)
- [ ] AI code generation pipeline

## Related Projects

- [TECS-L](https://github.com/need-singularity/TECS-L) -- Mathematical foundation
- [n6-architecture](https://github.com/need-singularity/n6-architecture) -- Full n=6 computing architecture (102 DSE domains)

## The Core Theorem

```
    For all n >= 2:

        sigma(n) * phi(n) = n * tau(n)  <==>  n = 6

    Three independent proofs exist.
    This is the foundation of everything.
```

## License

MIT
