# Contributing to HEXA-LANG

Thank you for your interest in contributing to HEXA-LANG. This guide covers everything from setting up the development environment to submitting a pull request.

## Development Setup

### Prerequisites

- **Rust** (stable, 1.75+): `curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh`
- **Git**: any recent version

### Building from Source

```bash
git clone https://github.com/need-singularity/hexa-lang.git
cd hexa-lang
cargo build --release
```

### Running Tests

```bash
# Run all Rust tests
cargo test

# Run HEXA proof tests
./target/release/hexa --test examples/test_builtins.hexa

# Run a specific test module
cargo test -- lexer
cargo test -- parser
cargo test -- interpreter
```

### Project Structure

```
hexa-lang/
  src/
    main.rs           Entry point, REPL, CLI
    lexer.rs          Tokenizer (53 keywords, 24 operators)
    parser.rs         Recursive descent parser
    ast.rs            AST node types
    types.rs          8 primitive types + 4 type layers
    type_checker.rs   Static type checker
    interpreter.rs    Tree-walk evaluator + 50+ builtins
    compiler.rs       AST to bytecode compiler
    vm.rs             Stack-based bytecode VM
    jit.rs            Cranelift JIT backend
    proof_engine.rs   Proof/verify block execution
    memory.rs         Egyptian fraction allocator
    std_*.rs          Standard library modules (12 files)
    codegen_esp32.rs  ESP32 code generation
    codegen_verilog.rs  FPGA code generation
    codegen_wgsl.rs   WebGPU code generation
    ...               (51 source files total)
  examples/           Example HEXA programs
  docs/               Documentation, book, specs
  self/               Self-hosted lexer and parser
  playground/         WASM playground
  editors/            Editor support (VS Code)
```

## Coding Style

### Rust

- Follow standard Rust conventions (`cargo fmt`, `cargo clippy`)
- All public functions need doc comments
- Tests go in the same file as the code they test (`#[cfg(test)]`)
- No `unwrap()` in production code -- use `?` or explicit error handling

### HEXA Examples

- Every example should be runnable: `hexa examples/your_example.hexa`
- Include comments explaining the n=6 connection
- Use proof blocks to verify assertions

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
4. **Add tests** -- every new feature needs tests
5. **Run the full test suite**: `cargo test`
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

- [ ] `cargo test` passes
- [ ] `cargo clippy` has no warnings
- [ ] New features have tests
- [ ] Documentation updated if needed
- [ ] n=6 derivation explained (for design changes)

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
