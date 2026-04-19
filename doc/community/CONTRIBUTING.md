# Contributing to HEXA-LANG

Thank you for your interest in contributing to HEXA-LANG. This guide covers
everything from setting up the development environment to submitting a pull
request.

> **2026-04-11 self-host milestone**: HEXA-LANG is now fully self-hosted.
> `src/` and `Cargo.toml` have been deleted. The precompiled `./hexa`
> binary is the runtime; rebuilds bootstrap via `build_hexa.hexa`.

## Development Setup

### Prerequisites

- **Git**: any recent version
- **clang** (only for rebuild; runtime uses the precompiled binary)
- **bash**: for package manager scripts

No Rust, no cargo, no python required.

### Running from Source

```bash
git clone https://github.com/need-singularity/hexa-lang.git
cd hexa-lang
./hexa run examples/hello_min.hexa        # go/cargo-style subcommand
./hexa version                            # 0.1.0-stage1 CLI 확인
```

### Running Tests

```bash
# Self-host regression suite (205/205 should PASS)
./hexa run self/test_interp.hexa
./hexa run self/test_interp_e2e.hexa
./hexa run self/test_interp_match.hexa
./hexa run self/test_interp_valuesystem.hexa

# 호환 모드 (python/node 스타일): ./hexa 와 .hexa 파일만 넘기면 run으로 위임
./hexa self/test_interp.hexa
```

### Project Structure

```
hexa-lang/
  self/               605 .hexa files — sole source of truth
    lexer.hexa        Tokenizer (53 keywords, 24 operators)
    parser.hexa       Recursive descent parser (83 AST kinds)
    interpreter.hexa  Tree-walk evaluator (270+ builtins)
    ast.hexa          AST node types
    type_checker.hexa Static type checker + Law types
    compiler.hexa     AST → bytecode compiler
    vm.hexa           Stack-based bytecode VM
    jit.hexa          Cranelift JIT bridge
    trace_jit.hexa    Hot loop detection + trace recording
    proof_engine.hexa SAT/SMT proof engine
    memory.hexa       Egyptian fraction allocator
    ir/               HEXA-IR (J₂=24 opcodes, τ=4 categories)
    codegen/          ARM64/x86_64/ELF/Mach-O native codegen
    std_*.hexa        Standard library modules (14 files)
    lib.hexa          Library entry (module registry + run_source API)
    main.hexa         CLI dispatcher
  examples/           Example HEXA programs
  docs/               Documentation, book, specs
  playground/         Browser playground (static pages)
  editors/            Editor support (VS Code, JetBrains)
  pkg/hx              Pure-bash package manager (zero deps)
  hexa.toml           Self-host package manifest (replaces Cargo.toml)
  hexa                stage1 CLI dispatcher (77KB — go/cargo style)
  build/
    hexa_stage0       stage0 인터프리터 (351KB, run 서브커맨드 위임처)
    artifacts/        빌드 산출물 덤프 (.c 트랜스파일 결과, 네이티브 바이너리)
  self/main.hexa      CLI 디스패처 SSOT (run/build/cc/status/version/help)
  self/native/hexa_v2 트랜스파일러 (.hexa → .c)
```

## Coding Style

### Hexa

- Follow the self-host conventions in `self/*.hexa`
- All public functions need doc comments (triple `//` or `/** */`)
- Tests use `self/test_*.hexa` pattern runnable via `./hexa`
- Prefer `try { ... } catch e { ... }` over raw `exit(1)`
- Module state uses `pub let mut` to support cross-module `import`

### HEXA Examples

- Every example should be runnable: `./hexa examples/your_example.hexa`
- Include comments explaining the n=6 connection
- Use proof blocks (`proof { ... }`) to verify assertions

### n=6 Principle

Every design constant must derive from n=6. If you are adding a new feature:

1. Identify which n=6 constant governs the feature
2. Document the derivation in your PR description
3. Add a proof block that verifies the derivation

Example: "Adding a 5th error class is justified because sopfr(6) = 5."

## Pull Request Process

1. **Fork** the repository on GitHub
2. **Create a branch**: `git checkout -b feature/your-feature`
3. **Write code** following the coding style above
4. **Add tests** — every new feature needs tests in `self/test_*.hexa`
5. **Run the full regression suite**: all 205/205 must PASS
6. **Commit** with a clear message: `feat: add X (maps to tau(6) = 4)`
7. **Push** and open a Pull Request

### Commit Message Format

```
type: short description

Longer description if needed.
Explain the n=6 derivation if relevant.
```

Types: `feat`, `fix`, `docs`, `test`, `refactor`, `perf`, `chore`

### PR Checklist

- [ ] All 4 self/test_interp*.hexa files PASS (205/205 total)
- [ ] New features have tests in self/test_*.hexa
- [ ] Documentation updated if needed
- [ ] n=6 derivation explained (for design changes)
- [ ] No new external dependencies (see `hexa.toml` — dependencies = empty)

## What to Contribute

### High Impact

- **Bug fixes** -- especially in the interpreter and type checker
- **Test coverage** -- more proof-block tests for edge cases
- **Error messages** -- better diagnostics and suggestions
- **Documentation** -- The HEXA Book always needs improvements

### Medium Impact

- **Standard library** -- new functions for existing modules
- **Optimization** -- VM and JIT performance improvements
- **Examples** -- real-world programs written in HEXA
- **Editor support** -- LSP improvements, syntax highlighting

### Design Changes

Changes to the language design (new keywords, operators, types) require:

1. A GitHub Discussion explaining the proposal
2. An n=6 mathematical derivation
3. Consensus from maintainers
4. Implementation with full test coverage

## Good First Issues

See [FIRST_ISSUES.md](FIRST_ISSUES.md) for 6 beginner-friendly tasks, one per paradigm.

## License

By contributing to HEXA-LANG, you agree that your contributions will be licensed under the MIT License.
