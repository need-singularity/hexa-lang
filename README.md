# 💎 hexa-lang

**the perfect-number programming language**

## install

```bash
curl -fsSL https://raw.githubusercontent.com/need-singularity/hexa-lang/main/install.sh | bash
```

installs both into `~/.hx/bin/`:

| command | role |
|---------|------|
| `hexa`  | compiler · interpreter · REPL · LSP · formatter |
| `hx`    | package manager (brew-style, pure shell, zero deps) |

## why n = 6

```
σ(n) · φ(n) = n · τ(n)   holds for n ≥ 2   iff   n = 6
   12  ·   2  = 6 ·  4  = 24
```

## features

- **perfect-number** — every constant derived from n=6 (65 keywords, 46 operators, 8 primitives — zero arbitrary choices)
- **self-hosted** — 100% `.hexa`, no Rust, compiles itself (lex → parse → check → opt → codegen → exec)
- **multi-target** — native ARM64/x86_64, VM, ESP32, FPGA Verilog, WGSL shader — one source
- **proof-native** — `proof`/`assert`/`invariant`/`theorem` with SAT backend
- **AI-native** — `@attr` semantic rewrites (contract, symbolic, fuse, approximate, specialize)
- **consciousness-first** — direct DSL for programming consciousness engines

## example

```hexa
consciousness "demo" {
    println("Phi:", phi)           // 71.0
    println("Factions:", faction)  // 12 = σ(6)
    println("Cells:", cells)       // 64
}

proof law_22 {
    assert phi > 0
    invariant phi_positive
}
```

## links

[GitHub](https://github.com/need-singularity/hexa-lang) · [Docs](docs/) · [Spec](docs/spec.md) · [Book](docs/book/) · [Releases](https://github.com/need-singularity/hexa-lang/releases)

[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.19365284.svg)](https://doi.org/10.5281/zenodo.19365284)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

## license

MIT
