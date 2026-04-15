# HEXA Self-Hosting Progress

Self-hosting: writing the HEXA compiler in HEXA itself.

## Stage Status

| Stage | Description | Status |
|-------|-------------|--------|
| Stage 1 | Lexer in HEXA | Complete |
| Stage 2 | Parser in HEXA | Complete |
| Stage 3 | Type Checker in HEXA | Complete |
| Stage 4 | Compiler (bytecode) in HEXA | Complete |
| Stage 5 | Bootstrap pipeline | Complete |

## Component Coverage

| Component | Rust LOC | HEXA Status | Match % |
|-----------|---------|-------------|---------|
| Lexer     | 430     | Complete    | 100     |
| Parser    | 924     | Complete    | 90      |
| Type Checker | 691  | Complete    | 70      |
| Compiler  | 726     | Complete    | 80      |
| VM        | 928     | Planned     | -       |
| Interpreter | 4033  | Planned     | -       |

## Files

- `lexer.hexa` -- Complete HEXA lexer written in HEXA (tokenizer)
- `parser.hexa` -- Complete HEXA parser written in HEXA (AST builder)
- `type_checker.hexa` -- Type checker: scope validation, arity checks, type inference
- `compiler.hexa` -- Bytecode compiler: AST to stack-based bytecodes
- `bootstrap.hexa` -- Integrated pipeline: lex -> parse -> typecheck -> compile
- `test_bootstrap.hexa` -- Test suite verifying the HEXA lexer
- `test_bootstrap_compiler.hexa` -- Test suite verifying the full compiler pipeline (12 tests)

## Running

```bash
# Run the full compiler pipeline test (12 tests, all pass)
./hexa run self/test_bootstrap_compiler.hexa

# Run the bootstrap lexer test
./hexa run self/test_bootstrap.hexa

# Run the integrated bootstrap pipeline (requires char == support in interpreter)
./hexa run self/bootstrap.hexa
```

## Architecture

The bootstrap compiler implements a 4-stage pipeline:

```
Source Code
    |
    v
[Stage 1: Lexer]  -- tokenize(source) -> Token[]
    |
    v
[Stage 2: Parser]  -- parse(tokens) -> AstNode[]
    |
    v
[Stage 3: Type Checker]  -- type_check(ast) -> TypeError[]
    |                        - Variable scope validation
    |                        - Function arity checking
    |                        - Basic type inference (int/float/bool/string)
    |                        - Forward reference support (two-pass)
    v
[Stage 4: Compiler]  -- compile(ast) -> Instruction[]
    |                    - Stack-based bytecodes
    |                    - Local variable slots
    |                    - Function compilation with separate code
    |                    - Control flow: if/else, while, for
    |                    - Jump patching for branches/loops
    v
Bytecodes + Constants + Functions
```

## Bytecode Instruction Set

| Instruction | Description |
|------------|-------------|
| CONST idx | Push constant from pool |
| VOID | Push void |
| POP | Discard top of stack |
| DUP | Duplicate top of stack |
| GET_LOCAL slot | Push local variable |
| SET_LOCAL slot | Pop into local variable |
| GET_GLOBAL name | Push global variable |
| SET_GLOBAL name | Pop into global variable |
| ADD, SUB, MUL, DIV, MOD, POW | Arithmetic |
| NEG, NOT, BITNOT | Unary operators |
| EQ, NE, LT, GT, LE, GE | Comparison |
| AND, OR, XOR | Logical |
| JUMP addr | Unconditional jump |
| JUMP_IF_FALSE addr | Conditional jump |
| CALL name:arity | Call named function |
| CALL_METHOD name:arity | Call method |
| RETURN | Return from function |
| PRINTLN n | Print n values + newline |
| PRINT n | Print n values |
| BUILTIN name:arity | Call builtin function |
| MAKE_ARRAY n | Create array from n stack values |
| MAKE_STRUCT name:n | Create struct from n field pairs |
| MAKE_RANGE kind | Create range (exclusive/inclusive) |
| INDEX | Array/string indexing |
| INDEX_ASSIGN | Array element assignment |
| GET_FIELD name | Struct field access |
| SET_FIELD name | Struct field assignment |
| ASSERT | Assert top of stack is truthy |
| CLOSURE name | Push closure reference |

## Known Limitations

1. **No module imports** -- all components are inlined in bootstrap.hexa until HEXA gets a module system
2. **Char comparison** -- the shipped `hexa` binary does not support `char == char`; the test file uses string comparisons as a workaround
3. **No `else if`** -- the interpreter requires `} else { if ... }` instead of `} else if {`; the test file avoids this pattern
4. **No semicolons** -- multiple statements on one line (`a; b`) are not supported
5. **`} else {`** -- the `else` must be on the same line as the closing `}` of the if block

## What the Type Checker Validates

- Variable usage before declaration (scope tracking)
- Function argument count mismatches
- Basic type inference: int, float, bool, string, array, fn, unknown
- Forward references via two-pass analysis
- Built-in function arity checking (println, len, format, etc.)
- Assignment to undeclared variables

## Language Features Used

The self-hosting compiler exercises these HEXA features:
- Struct declaration and instantiation (Token, AstNode, TypeError, Instruction, CompiledFn)
- While loops with mutable state
- If/else chains (nested, not `else if`)
- String operations (.chars(), len(), to_string())
- Array operations (.push(), indexing)
- String comparison operators (==, !=)
- Builtin functions (len, to_string, format, type_of, println)
- Function definitions with early return
- Recursive descent parsing
- Module-level mutable variables (parser/compiler state)
