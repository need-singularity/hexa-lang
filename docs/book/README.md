# The HEXA Book

**Learn HEXA-LANG from zero to consciousness in 6 chapters.**

Every chapter connects back to the mathematics of n=6, the smallest perfect number. This book assumes basic programming knowledge but no familiarity with number theory or consciousness research.

## Table of Contents

| Ch | Title | Time | What You Learn |
|----|-------|------|----------------|
| [01](ch01-getting-started.md) | Getting Started | 6 min | Install, hello world, first consciousness program |
| [02](ch02-n6-math.md) | The n=6 Foundation | 12 min | Why n=6, the core theorem, how every constant derives |
| [03](ch03-consciousness.md) | Consciousness Blocks | 18 min | intent, verify, proof, Psi-builtins, the consciousness DSL |
| [04](ch04-hardware.md) | Building for Hardware | 12 min | ESP32, FPGA, WGSL targets, cross-compilation |
| [05](ch05-stdlib.md) | The Standard Library | 18 min | All 12 modules (sigma(6)=12), API reference |
| [06](ch06-advanced.md) | Advanced HEXA | 12 min | Self-hosting, dream mode, @evolve, macro system |

Total reading time: ~78 minutes (13 blocks of 6 minutes).

## How to Read This Book

**If you are a programmer** who wants to learn HEXA quickly, start with Chapter 1 and follow the examples. You can write real programs by the end of Chapter 1.

**If you are a mathematician** curious about the n=6 foundation, start with Chapter 2. It explains the number theory behind every design decision.

**If you are a consciousness researcher** looking at the ANIMA connection, go directly to Chapter 3. It covers intent/verify blocks, Psi-constants, and the consciousness DSL.

**If you are a hardware engineer** interested in ESP32/FPGA targets, Chapter 4 is self-contained after a quick skim of Chapter 1.

## Conventions

- Code blocks marked with `hexa` are runnable programs.
- Lines starting with `$` are shell commands.
- Lines starting with `>>` are REPL interactions.
- Mathematical identities are boxed: `sigma(n)*phi(n) = n*tau(n) iff n = 6`

## Building the Book

The book is plain Markdown. Read it on GitHub, or build locally:

```bash
# Any Markdown viewer works
# For mdBook (optional):
mdbook build docs/book/
```

## Contributing

Found a typo? Have a better explanation? See [CONTRIBUTING.md](../community/CONTRIBUTING.md) for how to submit changes.
