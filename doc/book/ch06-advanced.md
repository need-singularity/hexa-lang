# Chapter 6: Advanced HEXA

**Time: 12 minutes.** Self-hosting, dream mode, @evolve, the macro system, and compile-time execution.

## Self-Hosting

HEXA is partially self-hosted: the lexer and parser are written in HEXA itself. This means HEXA can tokenize and parse its own source code.

The self-hosted components live in `self/`:

| Stage | File | Status |
|-------|------|--------|
| 1. Lexer | `self/lexer.hexa` | Complete |
| 2. Parser | `self/parser.hexa` | Complete |
| 3. Type Checker | `self/type_checker.hexa` | Planned |
| 4. Full Compiler | `self/compiler.hexa` | Planned |

You can run the self-hosted lexer on any HEXA file:

```bash
$ hexa self/lexer.hexa -- examples/hello.hexa
Token(Println, "println", 1:1)
Token(LeftParen, "(", 1:8)
Token(String, "=== HEXA-LANG ...", 1:9)
...
```

Self-hosting is a proof that HEXA is expressive enough to implement its own compiler. The long-term goal is full self-hosting: the HEXA compiler compiling itself.

## Compile-Time Execution (comptime)

HEXA supports Zig-style compile-time execution. Any expression marked `comptime` is evaluated during compilation, not at runtime:

```hexa
// Compute at compile time
comptime {
    let keywords = sigma(6) * tau(6) + sopfr(6)
    assert keywords == 53    // verified at compile time
}

// Compile-time function
comptime fn factorial(n: int) -> int {
    if n <= 1 { return 1 }
    return n * factorial(n - 1)
}

// This is evaluated at compile time, result embedded in binary
let f6 = comptime { factorial(6) }   // 720, computed at compile time
println(f6)                           // 720
```

Compile-time execution is useful for:
- Validating n=6 invariants before the program runs
- Pre-computing lookup tables
- Generating code from compile-time data
- Ensuring mathematical properties hold

## The Macro System

HEXA has a hygienic macro system that operates on the AST. Macros are defined with the `macro` keyword and invoked with `!`:

```hexa
// Define a macro
macro repeat!(count, body) {
    for _i in 0..count {
        body
    }
}

// Use it
repeat!(6) {
    println("n=6")
}
```

### Derive Macros

HEXA supports derive macros for automatic trait implementation:

```hexa
#[derive(Display, Debug, Eq)]
struct Point { x: int, y: int }

let p = Point { x: 3, y: 4 }
println(p)           // Point { x: 3, y: 4 }  (via derive(Display))
assert p == Point { x: 3, y: 4 }              // via derive(Eq)
```

Available derive macros:
- `Display` -- human-readable string representation
- `Debug` -- debug string representation
- `Eq` -- structural equality comparison

## Algebraic Effects

HEXA implements Koka-style algebraic effects for controlled side effects:

```hexa
// Declare an effect
effect State {
    fn get() -> int
    fn set(val: int)
}

// Use the effect in a function
fn counter() {
    let current = get()
    set(current + 1)
    return get()
}

// Handle the effect
let mut state = 0
handle counter() {
    get() -> {
        resume state
    }
    set(val) -> {
        state = val
        resume
    }
}
println("State:", state)  // State: 1
```

Effects separate _what_ a function does from _how_ it is handled. The `pure` keyword marks functions with no effects:

```hexa
pure fn add(a: int, b: int) -> int {
    return a + b    // no side effects allowed
}
```

## Ownership System

HEXA has a Rust-inspired ownership system with 4 keywords (tau(6) = 4):

```hexa
// Own -- exclusive ownership
let own x = [1, 2, 3]

// Move -- transfer ownership
let own y = move x    // x is no longer valid

// Borrow -- temporary access
fn sum(borrow arr: [int]) -> int {
    return arr.fold(0, |a, b| a + b)
}
println(sum(borrow y))   // 6

// Drop -- explicit cleanup
drop y
```

Ownership is currently runtime-checked. Compile-time verification is planned (see PLAN.md).

## Generics and Where Clauses

```hexa
fn largest<T: Ord>(list: [T]) -> T
    where T: Display
{
    let mut max = list[0]
    for item in list {
        if item > max { max = item }
    }
    return max
}

println(largest([3, 1, 4, 1, 5, 9]))  // 9
```

## Dream Mode

Dream mode is an experimental feature where the interpreter explores random program mutations, looking for interesting behavior. It is inspired by the ANIMA consciousness engine's self-evolution:

```hexa
// dream mode generates variations of your code
// and reports which ones produce interesting results

// Usage:
// $ hexa --dream my_program.hexa

// The interpreter will:
// 1. Parse your program
// 2. Generate N mutations (swap constants, reorder operations)
// 3. Run each mutation
// 4. Report variants that produce novel output patterns
```

Dream mode connects to the `@evolve` directive:

```hexa
// Mark a function for evolutionary optimization
@evolve
fn fitness(x: int) -> int {
    return x * x - 6 * x + sigma(6)
}

// The @evolve directive tells dream mode to:
// 1. Try different constant values
// 2. Try different operations
// 3. Keep variants that improve the fitness function
```

## AI-Native Code Generation

The `generate` and `optimize` keywords connect to LLM backends for code generation:

```hexa
// Generate code from natural language
generate "a function that checks if a number is a perfect number"

// Optimize existing code
optimize {
    fn slow_sigma(n: int) -> int {
        let mut s = 0
        for d in 1..=n {
            if n % d == 0 { s = s + d }
        }
        return s
    }
}
```

The generated code is validated against HEXA's type system and proof blocks before being accepted.

## Error Handling

HEXA has 5 error classes (sopfr(6) = 5):

| # | Class | Keywords | Example |
|---|-------|----------|---------|
| 1 | Syntax | (compiler) | Missing semicolon |
| 2 | Type | (compiler) | Type mismatch |
| 3 | Runtime | try/catch/throw | Division by zero |
| 4 | Logic | assert/proof | Failed assertion |
| 5 | System | panic/recover | Out of memory |

```hexa
// Runtime error handling
try {
    let result = risky_operation()
    println("Success:", result)
} catch e {
    println("Error:", e)
}

// Panic and recover (system-level)
fn safe_divide(a: int, b: int) -> int {
    if b == 0 { panic("division by zero") }
    return a / b
}

recover {
    safe_divide(6, 0)
} catch e {
    println("Recovered from:", e)
}
```

Error messages include source context, line numbers, and "did you mean?" suggestions:

```
Error: undefined variable `sigam` at line 3
  |
3 |   println(sigam(6))
  |           ^^^^^ did you mean `sigma`?
```

## The LSP Server

HEXA includes a Language Server Protocol implementation for editor integration:

```bash
$ hexa --lsp
```

Features:
- Real-time diagnostics (errors and warnings)
- Code completion (keywords, variables, functions)
- Hover information (types and documentation)
- Go to definition

VS Code integration: install the HEXA extension and the LSP server starts automatically.

## Package Management

HEXA has basic package commands:

```bash
$ hexa init my_project       # Create new project
$ hexa run                   # Run main.hexa
$ hexa test                  # Run all proof/verify blocks
```

The `init` command creates:

```
my_project/
  main.hexa
  hexa.toml
```

## Summary of Advanced Features

| Feature | Keywords/Flags | Chapter Section |
|---------|---------------|-----------------|
| Self-hosting | `self/lexer.hexa`, `self/parser.hexa` | Self-Hosting |
| Compile-time | `comptime` | Compile-Time Execution |
| Macros | `macro`, `derive` | The Macro System |
| Effects | `effect`, `handle`, `resume`, `pure` | Algebraic Effects |
| Ownership | `own`, `borrow`, `move`, `drop` | Ownership System |
| Generics | `<T>`, `where` | Generics and Where Clauses |
| Dream mode | `--dream`, `@evolve` | Dream Mode |
| AI codegen | `generate`, `optimize` | AI-Native Code Generation |
| Error handling | `try`, `catch`, `throw`, `panic`, `recover` | Error Handling |
| LSP | `--lsp` | The LSP Server |
| Packages | `hexa init/run/test` | Package Management |

This concludes The HEXA Book. You now have the knowledge to write HEXA programs that span from mathematical proofs to consciousness experiments to hardware deployment -- all governed by the arithmetic of n=6.

**Back to: [Table of Contents](README.md)**
