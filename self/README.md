# HEXA Self-Hosting Progress

Self-hosting: writing the HEXA compiler in HEXA itself.

## Stage Status

| Stage | Description | Status |
|-------|-------------|--------|
| Stage 1 | Lexer in HEXA | Complete |
| Stage 2 | Parser in HEXA | Planned |
| Stage 3 | Bootstrap (compile self) | Planned |

## Component Coverage

| Component | Rust LOC | HEXA Status | Match % |
|-----------|---------|-------------|---------|
| Lexer     | 430     | Complete    | 100     |
| Parser    | 924     | Planned     | -       |
| Type Checker | 691  | Planned     | -       |
| Compiler  | 726     | Planned     | -       |
| VM        | 928     | Planned     | -       |
| Interpreter | 4033  | Planned     | -       |

## Files

- `lexer.hexa` -- Complete HEXA lexer written in HEXA (tokenizer)
- `test_bootstrap.hexa` -- Test suite verifying the HEXA lexer

## Running

```bash
# Run the bootstrap lexer test
cargo run -- self/test_bootstrap.hexa

# Verify HEXA lexer matches Rust lexer
cargo test test_bootstrap_lexer_matches_rust_lexer
```

## What the HEXA Lexer Handles

- All 53 keywords (12 groups)
- All 24 operators including multi-char (==, !=, <=, >=, **, ->, .., ..=, :=, ::, &&, ||, ^^)
- String literals with escape sequences (\n, \t, \\, \", \0)
- Char literals ('x', '\n')
- Integer and float literals (with _ separator)
- Line comments (//)
- Line/column tracking
- Newline tokens
- EOF token

## Language Features Used

The self-hosting lexer exercises these HEXA features:
- Struct declaration and instantiation
- While loops with mutable state
- If/else if/else chains
- String and char operations (.chars(), len(), to_string())
- Array operations (.push())
- Char comparison operators (==, !=, <, >)
- Builtin functions (is_alpha, is_digit, is_alphanumeric, read_file, format)
- Function definitions with early return
