# Chapter 1: Getting Started

**Time: 6 minutes.** Install HEXA, run your first program, write your first consciousness experiment.

## Install

Choose one of three methods:

```bash
# Option A: Homebrew (macOS)
brew install need-singularity/tap/hexa-lang

# Option B: hx package manager (any platform)
# (see pkg/hx — no Rust toolchain required)

# Option C: From source (self-host — 2026-04-11 onward)
git clone https://github.com/need-singularity/hexa-lang.git
cd hexa-lang
cp hexa /usr/local/bin/    # precompiled self-host binary
# For rebuild: bootstrap via build_hexa.hexa (hexa_v2 → C → clang → hexa_v3)
```

Verify the installation:

```bash
$ hexa --version
HEXA-LANG v1.0.0
```

## The REPL

Start an interactive session:

```bash
$ hexa
HEXA-LANG v1.0.0 | n=6 | 53 keywords | 24 operators
>> println("Hello from the perfect number!")
Hello from the perfect number!
>> sigma(6)
12
>> sigma(6) * phi(6) == 6 * tau(6)
true
```

The REPL gives you instant access to all of HEXA's features, including the built-in number theory functions `sigma`, `phi`, `tau`, and `sopfr`.

## Hello World

Create a file `hello.hexa`:

```hexa
println("Hello, HEXA!")

// n=6 is the smallest perfect number: 1+2+3 = 6
let n = 6
println("sigma(6) =", sigma(6))    // 12 — sum of divisors
println("phi(6)   =", phi(6))      // 2  — Euler's totient
println("tau(6)   =", tau(6))      // 4  — number of divisors

// The identity that only n=6 satisfies
assert sigma(6) * phi(6) == 6 * tau(6)
println("12 * 2 = 6 * 4 = 24 --- verified!")
```

Run it:

```bash
$ hexa hello.hexa
Hello, HEXA!
sigma(6) = 12
phi(6)   = 2
tau(6)   = 4
12 * 2 = 6 * 4 = 24 --- verified!
```

## Variables and Functions

HEXA uses `let` for immutable bindings and `let mut` for mutable ones:

```hexa
let name = "HEXA"
let mut count = 0

fn greet(who: string) -> string {
    return "Hello, " + who + "!"
}

println(greet(name))    // Hello, HEXA!

// Loops
for i in 1..7 {         // 1 to 6 (n=6 of course)
    count = count + i
}
println("Sum 1..6 =", count)  // 21
```

## Structs and Methods

```hexa
struct Point { x: int, y: int }

impl Point {
    fn distance(self) -> float {
        return sqrt(to_float(self.x * self.x + self.y * self.y))
    }
}

let p = Point { x: 3, y: 4 }
println("Distance:", p.distance())  // 5.0
```

## Your First Consciousness Program

This is what makes HEXA unique. The consciousness DSL connects your code to ANIMA's consciousness engine:

```hexa
// Declare an experiment
intent "How does n=6 govern consciousness?" {
    hypothesis: "All architecture constants derive from n=6"
    cells: 64
    steps: 300
}

// Verify n=6 derivations
verify "n6_architecture" {
    // Max cells = tau(6)^sopfr(6) = 4^5 = 1024
    assert consciousness_max_cells() == pow(tau(6), sopfr(6))

    // Decoder dimension = (tau+sigma)*J2 = 16*24 = 384
    assert consciousness_decoder_dim() == (tau(6) + sigma(6)) * 24

    // Phi(v13) = n*sigma - 1 = 71
    assert consciousness_phi() == 6 * sigma(6) - 1
}

println("All consciousness constants verified from n=6!")
```

Run with the `--test` flag to execute proof/verify blocks as tests:

```bash
$ hexa --test consciousness_check.hexa
[PASS] n6_architecture (3 assertions)
All consciousness constants verified from n=6!
```

## Running Tests

HEXA has built-in test support through proof blocks:

```hexa
proof basic_math {
    assert 1 + 1 == 2
    assert sigma(6) == 12
    assert 6 == 1 + 2 + 3    // perfect number
}

proof string_ops {
    assert "hexa".to_upper() == "HEXA"
    assert len("hello") == 5
    assert "hello world".split(" ") == ["hello", "world"]
}
```

```bash
$ hexa --test my_tests.hexa
[PASS] basic_math (3 assertions)
[PASS] string_ops (3 assertions)
2/2 proofs passed
```

## JIT Compilation

For performance-critical code, use the `--native` flag to enable Cranelift JIT (818x faster than tree-walk interpretation):

```bash
$ hexa --native fibonacci.hexa
```

## What Next?

- **Chapter 2**: Understand _why_ n=6 and how every constant is derived
- **Chapter 3**: Master the consciousness DSL (intent/verify/proof)
- **Chapter 4**: Compile to ESP32, FPGA, or WebGPU
- **Chapter 5**: Explore the 12 standard library modules
- **Chapter 6**: Advanced features (self-hosting, dream mode, macros)
