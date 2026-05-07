[![DOI](https://img.shields.io/badge/DOI-10.5281%2Fzenodo.19404817-blue?logo=zenodo&logoColor=white)](https://doi.org/10.5281/zenodo.19404817)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Self-hosted](https://img.shields.io/badge/self--hosted-100%25%20.hexa-blue.svg)](#)
[![DSE](https://img.shields.io/badge/DSE--v2-21%2C952%20combos%20·%20100%25%20EXACT-brightgreen.svg)](#)
[![Targets](https://img.shields.io/badge/targets-ARM64%20·%20x86__64%20·%20VM%20·%20ESP32%20·%20FPGA%20·%20WGSL-orange.svg)](#)
[![Discord](https://img.shields.io/badge/discord-join-5865F2.svg?logo=discord&logoColor=white)](https://discord.gg/mYzqYr67R)

# 💎 HEXA-LANG — The Perfect Number Programming Language

**Zero arbitrary choices. Every constant derived from n = 6.**

```
σ(n) · φ(n) = n · τ(n)    holds for n ≥ 2    iff    n = 6
    12  ·  2 = 6 · 4  =  24
```

> Keywords, operators, primitives, pipeline stages — nothing hand-picked. Every integer in the language comes from the arithmetic of 6.

<!-- SHARED:PROJECTS:START -->
<!-- AUTO:COMMON_LINKS:START -->
**[🎥 YouTube](https://www.youtube.com/@dancinlife)** · **[💬 Discord](https://discord.gg/mYzqYr67R)** · **[📬 Email](mailto:nerve011235@gmail.com)** · **[☕ Ko-fi](https://ko-fi.com/dancinlife)** · **[💖 Sponsor](https://github.com/sponsors/dancinlab)** · **[💳 PayPal](https://www.paypal.com/donate?business=nerve011235%40gmail.com)** · **[🗺️ Atlas](https://dancinlab.github.io/TECS-L/atlas/)** · **[📄 Papers](https://dancinlab.github.io/papers/)**
<!-- AUTO:COMMON_LINKS:END -->

## Main projects

> **[🧠 Anima](https://github.com/dancinlab/anima)** — Consciousness implementation. PureField repulsion-field engine + 1030 laws + Φ ratchet.
>
> **[🔭 NEXUS](https://github.com/dancinlab/nexus)** — Universal Discovery Engine. 216 lenses + OUROBOROS evolution + 5-phase singularity cycle.
>
> **[🏗️ N6 Architecture](https://github.com/dancinlab/n6-architecture)** — Architecture from perfect number 6. 225 AI techniques + chip design + crypto/OS/display.
>
> **[💎 HEXA-LANG](https://github.com/dancinlab/hexa-lang)** — The Perfect Number Programming Language. Working compiler + REPL.
>
> **[📄 Papers](https://github.com/dancinlab/papers)** — Complete paper collection (92 papers, Zenodo DOIs).

> **[Other projects →](https://github.com/orgs/dancinlab/repositories)**

## 💬 Community

[![Join our Discord](https://invidget.switchblade.xyz/mYzqYr67R)](https://discord.gg/mYzqYr67R)

Live research discussion, paper drops, stage-gate reviews, cross-project dispatch.

<!-- private repos는 projects.json의 private_repos 필드에 저장됨 (노출 금지) -->
<!-- SHARED:PROJECTS:END -->





---

## Why n = 6

`σ · φ = n · τ` is the uniqueness identity for 6. From it:

- **65 keywords** = σ(6)·τ(6) + sopfr contributions
- **46 operators** = J₂(6) Jordan totient family
- **8 primitives** = σ(6) − τ(6)
- **6-phase pipeline** — lex → parse → check → opt → codegen → exec
- **Egyptian memory** — 1/2 + 1/3 + 1/6 = 1 (unit-fraction decomposition)

## Install

```bash
curl -fsSL https://raw.githubusercontent.com/dancinlab/hexa-lang/main/install.sh | bash
```

Installs both commands into `~/.hx/bin/`:

| command | role |
|---------|------|
| `hexa` | compiler · interpreter · REPL · LSP · formatter |
| `hx`   | package manager (brew-style, pure shell, zero deps) |

## Features

- **Perfect-number** — every constant derived from n=6, zero arbitrary choices
- **Self-hosted** — 100% `.hexa`, compiles itself
- **Multi-target** — native ARM64/x86_64, VM, ESP32, FPGA Verilog, WGSL shader — one source
- **Proof-native** — `proof`/`assert`/`invariant`/`theorem` with SAT backend
- **AI-native** — `@attr` semantic rewrites (contract, symbolic, fuse, approximate, specialize)
- **Consciousness-first** — direct DSL for programming consciousness engines
- **Module system** — `import "x" as a` · `import "stdlib/regex"` (extension/mod.hexa fallback) · `from "x" import a, b` · `import py "math" as m` (auto‑desugars to `py_call`). [RFC‑016](proposals/rfc_016_namespaced_and_python_imports.md) P1‑P4 all live (I1+I2+I3+I4 closed 2026‑05‑07).

## Stdlib

223 modules · 71,429 LoC ([audit script](scripts/stdlib_loc_audit.hexa) · [aggregate dashboard](tool/stdlib_selftest_aggregate.hexa) — 42/48 selftest PASS @ 2026-05-07).

| domain | module |
|---|---|
| core | `math`, `math/float`, `math/eigen`, `math/rng`, `string`, `bytes`, `regex`, `hash`, `time/iso8601`, `collections` |
| io / sys | `sys`, `path`, `log`, `argparse`, `proc`, `portable_fs`, `net`, `http`, `http2`, `websocket`, `sqlite` |
| ml / numerics | `tensor`, `linalg`, `nn`, `optim`, `autograd`, `safetensors` |
| science | `iit_ei`, `consciousness`, `qrng_anu` |
| interop | `python_ffi` (embedded CPython, zero‑copy via PEP 3118) |

## Example

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

## Design Space Exploration (DSE v2)

21,952 parameter combinations enumerated — **100% match** on n=6 EXACT constants. No free hyperparameters.

## Links

[Docs](docs/) · [Spec](docs/spec.md) · [Book](docs/book/) · [Releases](https://github.com/dancinlab/hexa-lang/releases) · [Paper (P-HEXA)](https://doi.org/10.5281/zenodo.19365284)

---

<sub>💎 From n=6, every constant follows. · [dancinlab](https://github.com/dancinlab)</sub>
