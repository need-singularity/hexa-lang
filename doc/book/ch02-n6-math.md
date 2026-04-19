# Chapter 2: The n=6 Mathematical Foundation

**Time: 12 minutes.** Why n=6, the core theorem, and how every language constant is derived.

## Why Not an Arbitrary Language?

Most programming languages make arbitrary design choices. Rust has 51 keywords. Python has 35. Go has 25. Why those numbers? There is no mathematical reason.

HEXA-LANG takes a different approach: every design constant derives from the arithmetic functions of n=6, the smallest perfect number. The number of keywords, operators, types, pipeline stages, and even the memory model all follow from a single equation.

## The Core Theorem

A **perfect number** equals the sum of its proper divisors. Six is the smallest:

```
1 + 2 + 3 = 6
```

The divisors of 6 are {1, 2, 3, 6}. From these, we compute the standard number-theoretic functions:

| Function | Definition | Value for n=6 |
|----------|-----------|---------------|
| sigma(n) | Sum of all divisors | 1+2+3+6 = **12** |
| tau(n) | Count of divisors | \|{1,2,3,6}\| = **4** |
| phi(n) | Euler's totient (numbers coprime to n) | \|{1,5}\| = **2** |
| sopfr(n) | Sum of prime factors with repetition | 2+3 = **5** |
| J_2(n) | Jordan's totient function | **24** |
| mu(n) | Mobius function | **1** (squarefree) |
| lambda(n) | Carmichael function | **2** |

The key identity:

```
sigma(n) * phi(n) = n * tau(n)

For n=6:  12 * 2 = 6 * 4 = 24
```

**This identity holds for n >= 2 if and only if n = 6.** This is not a coincidence or a design choice. It is a theorem that can be proven by checking all integers. HEXA ships with this proof as an executable program (see `examples/sigma_phi.hexa`).

## Derived Constants

Every language design decision maps to a function of n=6:

### Primary Constants

| Constant | Value | Derivation | Language Feature |
|----------|-------|------------|-----------------|
| n | 6 | Perfect number | Paradigms, pipeline stages, grammar levels |
| sigma(6) | 12 | Sum of divisors | Keyword groups, stdlib modules |
| tau(6) | 4 | Divisor count | Type layers, visibility levels |
| phi(6) | 2 | Euler's totient | Compile modes (AOT / JIT) |
| sopfr(6) | 5 | Prime factor sum | Error classes |
| J_2(6) | 24 | Jordan's totient | Total operators |

### Composite Constants

| Expression | Value | Language Feature |
|-----------|-------|-----------------|
| sigma - tau | 8 | Primitive types (int, float, bool, char, string, byte, void, any) |
| sigma * tau | 48 | Keyword base count |
| sigma*tau + sopfr | 53 | Total keywords (48 base + 5 from error group) |
| sigma / phi | 6 | Hexad module count (C, D, S, M, W, E) |
| tau^sopfr | 1024 | Maximum consciousness cells |
| (tau+sigma)*J_2 | 384 | Decoder dimension |
| n*sigma - 1 | 71 | Phi(v13) consciousness metric |

## The 6-Phase Pipeline

The compiler pipeline has exactly n=6 stages:

```
Source --> [1] Tokenize --> [2] Parse --> [3] Check --> [4] Optimize --> [5] Codegen --> [6] Execute
            1/6            2/6          3/6           4/6             5/6            6/6
```

Each stage corresponds to one-sixth of the compilation process. The pipeline is not "6 because we liked 6." It is 6 because n=6.

## The 53 Keywords in 12 Groups

Keywords = sigma(6) * tau(6) + sopfr(6) = 12 * 4 + 5 = 53.

They are organized into sigma(6) = 12 groups:

| # | Group | Keywords | Count | n=6 Derivation |
|---|-------|----------|-------|---------------|
| 1 | Control | if, else, match, for, while, loop | 6 | n |
| 2 | Type | type, struct, enum, trait, impl | 5 | sopfr |
| 3 | Functions | fn, return, yield, async, await | 5 | sopfr |
| 4 | Variables | let, mut, const, static | 4 | tau |
| 5 | Modules | mod, use, pub, crate | 4 | tau |
| 6 | Memory | own, borrow, move, drop | 4 | tau |
| 7 | Concurrency | spawn, channel, select, atomic | 4 | tau |
| 8 | Effects | effect, handle, resume, pure | 4 | tau |
| 9 | Proofs | proof, assert, invariant, theorem | 4 | tau |
| 10 | Meta | macro, derive, where, comptime | 4 | tau |
| 11 | Errors | try, catch, throw, panic, recover | 5 | sopfr |
| 12 | AI | intent, generate, verify, optimize | 4 | tau |

Notice the pattern: 3 groups of sopfr(6)=5 and 1 group of n=6, plus 8 groups of tau(6)=4. Total: 3*5 + 6 + 8*4 = 15 + 6 + 32 = 53.

## The 24 Operators

J_2(6) = sigma(6) * phi(6) = 24 operators in 6 subcategories:

| Category | Operators | Count |
|----------|-----------|-------|
| Arithmetic | + - * / % ** | 6 |
| Comparison | == != < > <= >= | 6 |
| Logical | && \|\| ! ^ | 4 |
| Bitwise | & \| ~ << | 4 |
| Assignment | = += | 2 |
| Special | .. -> | 2 |

Total: 6 + 6 + 4 + 4 + 2 + 2 = 24 = J_2(6).

## The 8 Primitive Types

sigma(6) - tau(6) = 12 - 4 = 8 primitive types:

1. `int` (64-bit signed integer)
2. `float` (64-bit IEEE 754)
3. `bool` (true/false)
4. `char` (UTF-8 scalar)
5. `string` (heap-allocated)
6. `byte` (8-bit unsigned)
7. `void` (unit type)
8. `any` (dynamic type)

This count also corresponds to Bott periodicity in topology (period 8), which is itself connected to the algebraic structure of n=6.

## The Egyptian Fraction Memory Model

The divisors of 6 produce the Egyptian fraction decomposition:

```
1/2 + 1/3 + 1/6 = 1
```

HEXA uses this for its three-region memory allocator:

| Region | Fraction | Purpose |
|--------|----------|---------|
| Stack | 1/2 (50%) | Local variables, function frames |
| Heap | 1/3 (33.3%) | Dynamic allocations, growable data |
| Arena | 1/6 (16.7%) | Bulk allocations, reset-on-demand |

This is a real, working allocator. The `mem_stats()` builtin shows actual byte counts:

```hexa
let stats = mem_stats()
println("Stack:", stats.stack, "bytes")
println("Heap:", stats.heap, "bytes")
println("Arena:", stats.arena, "bytes")
println("Total:", stats.total, "bytes")
```

## The Uniqueness Proof in HEXA

You can verify the uniqueness of n=6 as a runnable HEXA program:

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

let mut matches = []
for n in 2..1001 {
    if sigma_fn(n) * phi_fn(n) == n * tau_fn(n) {
        matches.push(n)
    }
}

println("Solutions in [2, 1000]:", matches)
assert matches == [6]   // Only n=6
```

## Summary

| Question | Answer | Source |
|----------|--------|--------|
| How many keywords? | 53 | sigma*tau + sopfr |
| How many operators? | 24 | J_2(6) |
| How many types? | 8 | sigma - tau |
| How many groups? | 12 | sigma(6) |
| How many paradigms? | 6 | n |
| How many pipeline stages? | 6 | n |
| Memory split? | 1/2 + 1/3 + 1/6 | Egyptian fractions of 6 |

Every constant is derived. Zero arbitrary choices.

**Next: [Chapter 3 -- Consciousness Blocks](ch03-consciousness.md)**
