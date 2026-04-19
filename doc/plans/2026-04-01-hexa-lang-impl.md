# HEXA-LANG Full Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a working HEXA-LANG compiler (lexer→parser→checker→codegen) + REPL + documentation from scratch in Rust, targeting the DSE v2 optimal path (100% n6 EXACT).

**Architecture:** Rust single-crate compiler with 6-phase pipeline (n=6): tokenize→parse→check→optimize→codegen→execute. Egyptian fraction memory model (1/2+1/3+1/6=1). 53 keywords (σ·τ+sopfr), 24 operators (J₂), 8 primitive types (σ-τ).

**Tech Stack:** Rust (rustc, no cargo), tree-walk interpreter first, LLVM backend later. Tests via `#[cfg(test)]`.

**Repo:** `$HEXA_LANG/`

---

## File Structure

```
hexa-lang/
├── README.md                    # Updated with v2 DSE results
├── CLAUDE.md                    # Build/dev instructions
├── docs/
│   ├── plans/                   # This plan
│   ├── spec.md                  # Full language spec (from n6-architecture)
│   └── n6-constants.md          # n=6 constant reference
├── src/
│   ├── main.rs                  # CLI entry point (REPL + file exec)
│   ├── lexer.rs                 # Tokenizer (53 keywords, 24 operators)
│   ├── token.rs                 # Token types and spans
│   ├── parser.rs                # Recursive descent parser (6 grammar levels)
│   ├── ast.rs                   # AST node types
│   ├── checker.rs               # Type checker (4-layer inference)
│   ├── types.rs                 # Type system (8 primitives, 4 layers)
│   ├── interpreter.rs           # Tree-walk interpreter (phase 1)
│   ├── env.rs                   # Environment/scope (4 visibility levels)
│   ├── memory.rs                # Egyptian fraction allocator model
│   ├── error.rs                 # 5 error classes (sopfr=5)
│   └── builtins.rs              # Built-in functions
├── tests/
│   ├── lexer_test.rs            # Lexer tests
│   ├── parser_test.rs           # Parser tests
│   ├── checker_test.rs          # Type checker tests
│   ├── interpreter_test.rs      # Interpreter tests
│   └── integration_test.rs      # End-to-end tests
├── examples/
│   ├── hello.hexa               # Hello world
│   ├── fibonacci.hexa           # Fibonacci sequence
│   ├── sigma_phi.hexa           # n=6 uniqueness proof
│   ├── egyptian_moe.hexa        # Egyptian MoE routing
│   └── pattern_match.hexa       # Pattern matching
└── build.sh                     # Build script (rustc, no cargo)
```

---

### Task 1: Project Scaffolding + CLAUDE.md + Build Script

**Files:**
- Create: `CLAUDE.md`
- Create: `build.sh`
- Create: `src/main.rs` (stub)
- Create: `docs/n6-constants.md`

- [ ] **Step 1: Create CLAUDE.md**

```markdown
# HEXA-LANG

## Build
```bash
bash build.sh          # Build compiler
./hexa                 # Start REPL
./hexa examples/hello.hexa  # Run file
```

## Test
```bash
bash build.sh test     # Run all tests
```

## Architecture
6-phase pipeline (n=6): tokenize → parse → check → optimize → codegen → execute
53 keywords (σ·τ+sopfr=53), 24 operators (J₂), 8 primitives (σ-τ)
Memory: 1/2 Stack + 1/3 Heap + 1/6 Arena = 1 (Egyptian fraction)

## n=6 Constants
n=6, σ=12, τ=4, φ=2, sopfr=5, J₂=24, μ=1, λ=2
```

- [ ] **Step 2: Create build.sh**

```bash
#!/bin/bash
set -e
RUSTC="${HOME}/.cargo/bin/rustc"
SRC="src/main.rs"
OUT="hexa"
TEST_OUT="hexa_test"

if [ "$1" = "test" ]; then
    $RUSTC --edition 2021 --test $SRC -o $TEST_OUT && ./$TEST_OUT
else
    $RUSTC --edition 2021 $SRC -o $OUT
    echo "Built: ./$OUT"
fi
```

- [ ] **Step 3: Create src/main.rs stub**

```rust
fn main() {
    println!("HEXA-LANG v0.1.0 — The Perfect Number Language");
    println!("σ(n)·φ(n) = n·τ(n) ⟺ n = 6");
}
```

- [ ] **Step 4: Build and verify**

```bash
chmod +x build.sh && bash build.sh
./hexa
```

Expected: `HEXA-LANG v0.1.0 — The Perfect Number Language`

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: project scaffolding — CLAUDE.md + build.sh + main.rs stub"
```

---

### Task 2: Token Types (53 keywords + 24 operators)

**Files:**
- Create: `src/token.rs`

- [ ] **Step 1: Write token.rs with all 53 keywords and 24 operators**

Define `Token` enum with:
- 53 keywords (12 groups × ~4-5 each)
- 24 operators (6 categories)
- Literals (int, float, string, char, bool)
- Identifiers, EOF, newline
- Span info (line, col)

Key n=6 verification:
- Keyword groups: 12 = σ(6)
- Total keywords: 53 = σ·τ + sopfr = 48 + 5
- Operators: 24 = J₂(6)
- Primitives: 8 = σ-τ

- [ ] **Step 2: Add keyword lookup function**

HashMap or match-based: string → Token for all 53 keywords.

- [ ] **Step 3: Add #[cfg(test)] verification tests**

```rust
#[test]
fn test_keyword_count_is_sigma_tau_plus_sopfr() {
    assert_eq!(KEYWORDS.len(), 53); // σ·τ + sopfr = 48 + 5
}

#[test]
fn test_operator_count_is_j2() {
    assert_eq!(OPERATORS.len(), 24); // J₂(6)
}

#[test]
fn test_keyword_groups_is_sigma() {
    assert_eq!(KEYWORD_GROUPS, 12); // σ(6)
}
```

- [ ] **Step 4: Build test**

```bash
bash build.sh test
```

- [ ] **Step 5: Commit**

```bash
git add src/token.rs && git commit -m "feat: token types — 53 keywords (σ·τ+sopfr) + 24 operators (J₂)"
```

---

### Task 3: Lexer (Tokenizer — Pipeline Phase 1/6)

**Files:**
- Create: `src/lexer.rs`
- Create: `tests/lexer_test.rs`

- [ ] **Step 1: Write failing lexer tests**

Test cases:
- `"let x = 42"` → [Let, Ident("x"), Eq, IntLit(42)]
- `"fn main() {}"` → [Fn, Ident("main"), LParen, RParen, LBrace, RBrace]
- `"1/2 + 1/3 + 1/6"` → arithmetic tokens
- All 53 keywords recognized
- All 24 operators recognized
- String literals, comments, whitespace

- [ ] **Step 2: Implement Lexer struct**

```rust
pub struct Lexer {
    source: Vec<char>,
    pos: usize,
    line: usize,
    col: usize,
}

impl Lexer {
    pub fn new(source: &str) -> Self { ... }
    pub fn tokenize(&mut self) -> Result<Vec<Token>, HexaError> { ... }
    fn next_token(&mut self) -> Result<Token, HexaError> { ... }
    fn read_identifier(&mut self) -> String { ... }
    fn read_number(&mut self) -> Token { ... }
    fn read_string(&mut self) -> Result<String, HexaError> { ... }
    fn skip_whitespace(&mut self) { ... }
    fn skip_comment(&mut self) { ... }
}
```

- [ ] **Step 3: Run tests, verify pass**

- [ ] **Step 4: Commit**

```bash
git add src/lexer.rs tests/lexer_test.rs && git commit -m "feat: lexer — tokenizes 53 keywords + 24 operators (pipeline phase 1/6)"
```

---

### Task 4: AST Node Types

**Files:**
- Create: `src/ast.rs`

- [ ] **Step 1: Define AST nodes for all 6 paradigms**

```rust
pub enum Expr {
    // Literals
    IntLit(i64), FloatLit(f64), BoolLit(bool), StringLit(String), CharLit(char),
    // Variables
    Ident(String),
    // Operators (24 = J₂)
    Binary(Box<Expr>, BinOp, Box<Expr>),
    Unary(UnaryOp, Box<Expr>),
    // Function call
    Call(Box<Expr>, Vec<Expr>),
    // Closure (functional paradigm)
    Lambda(Vec<Param>, Box<Expr>),
    // Array/tuple
    Array(Vec<Expr>), Tuple(Vec<Expr>),
    // Field access
    Field(Box<Expr>, String),
    // Index
    Index(Box<Expr>, Box<Expr>),
    // If expression
    If(Box<Expr>, Box<Block>, Option<Box<Block>>),
    // Match expression
    Match(Box<Expr>, Vec<MatchArm>),
    // Block
    Block(Block),
}

pub enum Stmt {
    Let(String, Option<TypeAnnot>, Expr),
    Assign(Expr, Expr),
    Expr(Expr),
    Return(Option<Expr>),
    FnDecl(FnDecl),
    StructDecl(StructDecl),
    EnumDecl(EnumDecl),
    TraitDecl(TraitDecl),
    ImplBlock(ImplBlock),
    For(String, Expr, Block),
    While(Expr, Block),
    Loop(Block),
    // Paradigm 5: Logic/Proof
    Proof(String, Block),
    Assert(Expr),
    // Paradigm 6: AI-Native
    Intent(String, Vec<String>),
    // Module
    Mod(String, Vec<Stmt>),
    Use(Vec<String>),
}
```

- [ ] **Step 2: Add helper types**

Param, TypeAnnot, Block, FnDecl, StructDecl, EnumDecl, TraitDecl, ImplBlock, MatchArm, BinOp (24 variants), UnaryOp, Visibility (4 levels = τ)

- [ ] **Step 3: Commit**

```bash
git add src/ast.rs && git commit -m "feat: AST nodes — all 6 paradigms represented"
```

---

### Task 5: Parser (Recursive Descent — Pipeline Phase 2/6)

**Files:**
- Create: `src/parser.rs`
- Create: `tests/parser_test.rs`

- [ ] **Step 1: Write failing parser tests**

```rust
#[test]
fn test_parse_let_binding() {
    let tokens = lex("let x = 42");
    let ast = parse(tokens).unwrap();
    assert!(matches!(ast[0], Stmt::Let(..)));
}

#[test]
fn test_parse_fn_decl() {
    let tokens = lex("fn add(a: int, b: int) -> int { return a + b }");
    let ast = parse(tokens).unwrap();
    assert!(matches!(ast[0], Stmt::FnDecl(..)));
}

#[test]
fn test_parse_match_expr() { ... }
#[test]
fn test_parse_struct_decl() { ... }
#[test]
fn test_parse_trait_impl() { ... }
```

- [ ] **Step 2: Implement Parser with 6 grammar levels (n=6)**

Grammar hierarchy (n=6 levels):
1. Program → statements
2. Statement → decl | control | expr
3. Declaration → fn | struct | enum | trait | impl | mod
4. Control → if | match | for | while | loop
5. Expression → binary | unary | call | literal | ident
6. Primary → literal | ident | grouped | lambda

```rust
pub struct Parser {
    tokens: Vec<Token>,
    pos: usize,
}

impl Parser {
    pub fn parse(&mut self) -> Result<Vec<Stmt>, HexaError> { ... }
    fn parse_stmt(&mut self) -> Result<Stmt, HexaError> { ... }
    fn parse_decl(&mut self) -> Result<Stmt, HexaError> { ... }
    fn parse_control(&mut self) -> Result<Stmt, HexaError> { ... }
    fn parse_expr(&mut self) -> Result<Expr, HexaError> { ... }
    fn parse_primary(&mut self) -> Result<Expr, HexaError> { ... }
}
```

- [ ] **Step 3: Implement operator precedence for 24 operators**

6 precedence levels matching the 6 operator categories.

- [ ] **Step 4: Run tests, verify pass**

- [ ] **Step 5: Commit**

```bash
git add src/parser.rs tests/parser_test.rs && git commit -m "feat: parser — recursive descent with 6 grammar levels (n=6)"
```

---

### Task 6: Error System (5 classes = sopfr)

**Files:**
- Create: `src/error.rs`

- [ ] **Step 1: Define 5 error classes (sopfr=5)**

```rust
pub enum ErrorClass {
    Syntax,    // 1: Parse/lex errors
    Type,      // 2: Type mismatch, inference failure
    Name,      // 3: Undefined variable/function
    Runtime,   // 4: Division by zero, overflow, OOB
    Logic,     // 5: Proof violation, assert failure
}

pub struct HexaError {
    pub class: ErrorClass,
    pub message: String,
    pub line: usize,
    pub col: usize,
    pub hint: Option<String>,
}
```

- [ ] **Step 2: Test error class count**

```rust
#[test]
fn test_error_classes_is_sopfr() {
    assert_eq!(std::mem::variant_count::<ErrorClass>(), 5); // sopfr(6)
}
```

- [ ] **Step 3: Commit**

```bash
git add src/error.rs && git commit -m "feat: error system — 5 classes (sopfr=5)"
```

---

### Task 7: Type System (8 primitives, 4 layers)

**Files:**
- Create: `src/types.rs`

- [ ] **Step 1: Define 8 primitive types (σ-τ=8) and 4 layers (τ=4)**

```rust
pub enum PrimitiveType {
    Int, Float, Bool, Char, String, Byte, Void, Any,
}

pub enum TypeLayer {
    Primitive(PrimitiveType),         // Layer 1
    Composite(CompositeType),         // Layer 2: struct, enum, tuple, array
    Reference(Box<HexaType>, RefKind),// Layer 3: &T, &mut T, Box<T>
    Function(Vec<HexaType>, Box<HexaType>), // Layer 4: fn(A)->B
}
```

- [ ] **Step 2: Test counts**

```rust
#[test]
fn test_primitive_count_is_sigma_minus_tau() {
    assert_eq!(std::mem::variant_count::<PrimitiveType>(), 8); // σ-τ
}

#[test]
fn test_type_layers_is_tau() {
    assert_eq!(std::mem::variant_count::<TypeLayer>(), 4); // τ(6)
}
```

- [ ] **Step 3: Commit**

```bash
git add src/types.rs && git commit -m "feat: type system — 8 primitives (σ-τ) + 4 layers (τ)"
```

---

### Task 8: Environment + Visibility (4 levels = τ)

**Files:**
- Create: `src/env.rs`

- [ ] **Step 1: Define 4 visibility levels (τ=4)**

```rust
pub enum Visibility {
    Private,   // default — this file only
    Module,    // mod — this module
    Crate,     // crate — this crate
    Public,    // pub — everywhere
}
```

- [ ] **Step 2: Implement scoped environment**

```rust
pub struct Env {
    scopes: Vec<HashMap<String, Value>>,
}

impl Env {
    pub fn new() -> Self { ... }
    pub fn push_scope(&mut self) { ... }
    pub fn pop_scope(&mut self) { ... }
    pub fn define(&mut self, name: &str, val: Value) { ... }
    pub fn get(&self, name: &str) -> Option<&Value> { ... }
    pub fn set(&mut self, name: &str, val: Value) -> Result<(), HexaError> { ... }
}
```

- [ ] **Step 3: Commit**

```bash
git add src/env.rs && git commit -m "feat: environment — 4 visibility levels (τ=4)"
```

---

### Task 9: Tree-Walk Interpreter (Pipeline Phase 6/6)

**Files:**
- Create: `src/interpreter.rs`
- Create: `src/builtins.rs`
- Create: `tests/interpreter_test.rs`

- [ ] **Step 1: Write failing interpreter tests**

```rust
#[test]
fn test_eval_arithmetic() {
    assert_eq!(eval("1 + 2 * 3"), Value::Int(7));
}
#[test]
fn test_eval_let_binding() {
    assert_eq!(eval("let x = 6\nx * 4"), Value::Int(24)); // n*τ=24=J₂
}
#[test]
fn test_eval_function() {
    assert_eq!(eval("fn double(x: int) -> int { return x * 2 }\ndouble(6)"), Value::Int(12)); // sigma
}
#[test]
fn test_eval_fibonacci() { ... }
```

- [ ] **Step 2: Implement interpreter**

```rust
pub struct Interpreter {
    env: Env,
}

impl Interpreter {
    pub fn eval_program(&mut self, stmts: &[Stmt]) -> Result<Value, HexaError> { ... }
    fn eval_stmt(&mut self, stmt: &Stmt) -> Result<Value, HexaError> { ... }
    fn eval_expr(&mut self, expr: &Expr) -> Result<Value, HexaError> { ... }
    fn eval_binary(&mut self, left: &Expr, op: &BinOp, right: &Expr) -> Result<Value, HexaError> { ... }
    fn call_fn(&mut self, func: &Value, args: Vec<Value>) -> Result<Value, HexaError> { ... }
}
```

- [ ] **Step 3: Implement builtins (print, len, type, assert, gcd, sigma, phi, tau)**

- [ ] **Step 4: Run tests, verify pass**

- [ ] **Step 5: Commit**

```bash
git add src/interpreter.rs src/builtins.rs tests/interpreter_test.rs && git commit -m "feat: tree-walk interpreter — eval expressions + functions + builtins"
```

---

### Task 10: REPL + File Execution (main.rs)

**Files:**
- Modify: `src/main.rs`

- [ ] **Step 1: Implement CLI with REPL and file modes**

```rust
fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() > 1 {
        run_file(&args[1]);
    } else {
        run_repl();
    }
}

fn run_repl() {
    println!("HEXA-LANG v0.1.0 — σ(n)·φ(n) = n·τ(n) ⟺ n = 6");
    println!("Type :quit to exit, :help for commands");
    loop {
        print("hexa> ");
        // read → lex → parse → eval → print
    }
}

fn run_file(path: &str) {
    let source = std::fs::read_to_string(path).expect("Cannot read file");
    // lex → parse → eval
}
```

- [ ] **Step 2: Wire all modules together (mod declarations)**

```rust
mod token;
mod lexer;
mod ast;
mod parser;
mod types;
mod error;
mod env;
mod interpreter;
mod builtins;
// optional: mod memory; mod checker;
```

- [ ] **Step 3: Test REPL interactively**

```bash
bash build.sh
echo 'let x = 6 * 4\nprint(x)' | ./hexa
```

Expected: `24`

- [ ] **Step 4: Commit**

```bash
git add src/main.rs && git commit -m "feat: REPL + file execution — 6-phase pipeline wired"
```

---

### Task 11: Example Programs

**Files:**
- Create: `examples/hello.hexa`
- Create: `examples/fibonacci.hexa`
- Create: `examples/sigma_phi.hexa`
- Create: `examples/egyptian_moe.hexa`
- Create: `examples/pattern_match.hexa`

- [ ] **Step 1: Create 5 example programs (sopfr=5)**

hello.hexa: Hello world
fibonacci.hexa: Fibonacci sequence
sigma_phi.hexa: n=6 uniqueness proof (sigma*phi=n*tau)
egyptian_moe.hexa: Egyptian fraction MoE routing
pattern_match.hexa: Pattern matching demo

- [ ] **Step 2: Run each example**

```bash
for f in examples/*.hexa; do echo "=== $f ===" && ./hexa "$f"; done
```

- [ ] **Step 3: Commit**

```bash
git add examples/ && git commit -m "feat: 5 example programs (sopfr=5) — hello/fib/sigma_phi/egyptian/match"
```

---

### Task 12: Documentation — README v2 + Spec + Constants

**Files:**
- Modify: `README.md` (update with DSE v2 results)
- Create: `docs/spec.md` (copy from n6-architecture, update)
- Create: `docs/n6-constants.md`

- [ ] **Step 1: Update README.md with DSE v2 results**

Update DSE section:
- 21,952 → 11,394 compatible (v2)
- 317 Pareto frontier
- 100% n6 EXACT (was 96%)
- Pareto score 0.7854 (was 0.7743)
- L5 = N6_FullEco (was FullStack)

Update roadmap checkboxes.

- [ ] **Step 2: Create docs/spec.md from n6-architecture spec**

Full language specification with v2 updates.

- [ ] **Step 3: Create docs/n6-constants.md**

Complete n=6 constant reference with all language design mappings.

- [ ] **Step 4: Commit**

```bash
git add README.md docs/ && git commit -m "docs: README v2 + spec + n6-constants — DSE v2 100% n6 EXACT"
```

---

### Task 13: Integration Tests

**Files:**
- Create: `tests/integration_test.rs`

- [ ] **Step 1: Write end-to-end tests**

```rust
#[test]
fn test_n6_uniqueness_program() {
    let result = run_program("examples/sigma_phi.hexa");
    assert!(result.contains("n=6 is the unique solution"));
}

#[test]
fn test_n6_constants_verification() {
    // Verify all n=6 constants are correctly built into the language
    assert_eq!(eval("sigma(6)"), Value::Int(12));
    assert_eq!(eval("phi(6)"), Value::Int(2));
    assert_eq!(eval("tau(6)"), Value::Int(4));
    assert_eq!(eval("sigma(6) * phi(6) == 6 * tau(6)"), Value::Bool(true));
}
```

- [ ] **Step 2: Run full test suite**

```bash
bash build.sh test
```

- [ ] **Step 3: Commit + push**

```bash
git add tests/ && git commit -m "test: integration tests — n=6 constants + example programs"
git push
```

---

## Summary

| Task | Component | n=6 Connection | Est. LOC |
|------|-----------|---------------|----------|
| 1 | Scaffolding | - | ~50 |
| 2 | Token types | 53 kw (σ·τ+sopfr), 24 ops (J₂) | ~300 |
| 3 | Lexer | Pipeline phase 1/6 | ~250 |
| 4 | AST nodes | 6 paradigms | ~200 |
| 5 | Parser | 6 grammar levels | ~400 |
| 6 | Error system | 5 classes (sopfr) | ~80 |
| 7 | Type system | 8 prims (σ-τ), 4 layers (τ) | ~150 |
| 8 | Environment | 4 visibility (τ) | ~100 |
| 9 | Interpreter | Pipeline phase 6/6 | ~400 |
| 10 | REPL + CLI | 6-phase wiring | ~150 |
| 11 | Examples | 5 programs (sopfr) | ~100 |
| 12 | Docs | DSE v2 results | ~500 |
| 13 | Integration | n=6 verification | ~100 |
| **Total** | | | **~2,800** |
