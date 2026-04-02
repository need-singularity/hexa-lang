# HEXA-LANG Quick Reference

## Keywords (53)

| Group | Keywords | Count |
|-------|----------|-------|
| Control Flow | `if` `else` `match` `for` `while` `loop` | 6 |
| Type Decl | `type` `struct` `enum` `trait` `impl` | 5 |
| Functions | `fn` `return` `yield` `async` `await` | 5 |
| Variables | `let` `mut` `const` `static` | 4 |
| Modules | `mod` `use` `pub` `crate` | 4 |
| Memory | `own` `borrow` `move` `drop` | 4 |
| Concurrency | `spawn` `channel` `select` `atomic` | 4 |
| Effects | `effect` `handle` `resume` `pure` | 4 |
| Proofs | `proof` `assert` `invariant` `theorem` | 4 |
| Meta | `macro` `derive` `where` `comptime` | 4 |
| Errors | `try` `catch` `throw` `panic` `recover` | 5 |
| AI/Consciousness | `intent` `generate` `verify` `optimize` | 4 |

## Operators (24)

| Category | Operators |
|----------|-----------|
| Arithmetic | `+` `-` `*` `/` `%` `**` |
| Comparison | `==` `!=` `<` `>` `<=` `>=` |
| Logical | `&&` `\|\|` `!` `^^` |
| Bitwise | `&` `\|` `^` `~` |
| Assignment | `=` `:=` |
| Special | `..` `..=` `->` |

## Builtin Functions (55+)

### I/O
| Function | Description |
|----------|-------------|
| `print(args...)` | Print without newline |
| `println(args...)` | Print with newline |
| `print_err(msg)` | Print to stderr |
| `read_file(path)` | Read file to string |
| `write_file(path, content)` | Write string to file |
| `file_exists(path)` | Check if file exists |

### Type Conversion
| Function | Description |
|----------|-------------|
| `type_of(val)` | Get type name as string |
| `to_string(val)` | Convert to string |
| `to_int(val)` | Convert to integer |
| `to_float(val)` | Convert to float |
| `len(val)` | Length of string/array/map |

### Math
| Function | Description |
|----------|-------------|
| `abs(n)` | Absolute value |
| `min(a, b)` | Minimum of two values |
| `max(a, b)` | Maximum of two values |
| `floor(f)` | Floor to integer |
| `ceil(f)` | Ceiling to integer |
| `round(f)` | Round to nearest integer |
| `sqrt(n)` | Square root |
| `pow(base, exp)` | Exponentiation |
| `log(n)` | Natural logarithm |
| `log2(n)` | Base-2 logarithm |
| `sin(n)` | Sine |
| `cos(n)` | Cosine |
| `tan(n)` | Tangent |

### Constants
| Name | Value |
|------|-------|
| `PI` | 3.14159... |
| `E` | 2.71828... |

### Number Theory (n=6)
| Function | Description |
|----------|-------------|
| `sigma(n)` | Sum of divisors |
| `phi(n)` | Euler's totient |
| `tau(n)` | Number of divisors |
| `gcd(a, b)` | Greatest common divisor |
| `sopfr(n)` | Sum of prime factors with repetition |

### Collections
| Function | Description |
|----------|-------------|
| `keys(map)` | Get map keys as array |
| `values(map)` | Get map values as array |
| `has_key(map, key)` | Check if map has key |
| `Set()` | Create empty set |

### Concurrency
| Function | Description |
|----------|-------------|
| `channel()` | Create (sender, receiver) pair |
| `join(name)` | Wait for named spawn |
| `sleep(ms)` | Sleep for milliseconds |

### Format
| Function | Description |
|----------|-------------|
| `format(template, args...)` | Format string with `{}` placeholders |

### OS
| Function | Description |
|----------|-------------|
| `args()` | Get command-line arguments |
| `env_var(name)` | Get environment variable |
| `exit(code)` | Exit process |
| `clock()` | Current time as float seconds |

### Time
| Function | Description |
|----------|-------------|
| `now()` | Current ISO 8601 timestamp |
| `timestamp()` | Unix timestamp (seconds) |
| `elapsed(start)` | Seconds since start timestamp |

### Encoding
| Function | Description |
|----------|-------------|
| `base64_encode(str)` | Encode to base64 |
| `base64_decode(str)` | Decode from base64 |
| `hex_encode(str)` | Encode to hex |
| `hex_decode(str)` | Decode from hex |

### JSON
| Function | Description |
|----------|-------------|
| `json_parse(str)` | Parse JSON string to value |
| `json_stringify(val)` | Convert value to JSON string |

### HTTP
| Function | Description |
|----------|-------------|
| `http_get(url)` | HTTP GET request |
| `http_post(url, data)` | HTTP POST request |

### Random
| Function | Description |
|----------|-------------|
| `random()` | Random float 0..1 |
| `random_int(min, max)` | Random integer in range |

### Char Utilities
| Function | Description |
|----------|-------------|
| `is_alpha(ch)` | Is alphabetic |
| `is_digit(ch)` | Is digit |
| `is_alphanumeric(ch)` | Is alphanumeric |
| `is_whitespace(ch)` | Is whitespace |

### Option/Result Constructors
| Function | Description |
|----------|-------------|
| `Some(val)` | Wrap in Option::Some |
| `None` | Option::None value |
| `Ok(val)` | Wrap in Result::Ok |
| `Err(val)` | Wrap in Result::Err |

### Memory
| Function | Description |
|----------|-------------|
| `mem_stats()` | Returns {stack_used, stack_cap, heap_used, heap_cap, arena_used, arena_cap} |
| `mem_region(value)` | Returns "stack", "heap", or "arena" for a given value |
| `arena_reset()` | Reset arena allocator, freeing all arena memory |
| `mem_budget()` | Returns budget breakdown (bytes per region) |

### Consciousness (Psi-Constants)
| Function | Description |
|----------|-------------|
| `psi_coupling()` | Alpha coupling constant (0.014) |
| `psi_balance()` | Balance point (0.5) |
| `psi_steps()` | Consciousness steps (4.33) |
| `psi_entropy()` | Information entropy (0.998) |
| `psi_frustration()` | Frustration constant (0.10) |
| `consciousness_vector()` | 10D consciousness vector |
| `consciousness_phi()` | Phi constant (71) |
| `consciousness_max_cells()` | Max cells (1024) |
| `consciousness_decoder_dim()` | Decoder dimension (384) |
| `phi_predict(cells)` | Predict Phi for N cells |
| `laws(n)` | Get consciousness law text |
| `meta_laws(n)` | Get meta-law text |
| `hexad_modules()` | Six hexad module names |
| `hexad_right()` | Right brain modules (C, S, W) |
| `hexad_left()` | Left brain modules (D, M, E) |

## String Methods

| Method | Description |
|--------|-------------|
| `.len()` | String length |
| `.contains(sub)` | Check substring |
| `.split(delim)` | Split into array |
| `.trim()` | Remove whitespace |
| `.to_upper()` | Uppercase |
| `.to_lower()` | Lowercase |
| `.replace(old, new)` | Replace substring |
| `.starts_with(prefix)` | Check prefix |
| `.ends_with(suffix)` | Check suffix |
| `.chars()` | Convert to char array |
| `.join(array)` | Join array with separator |
| `.repeat(n)` | Repeat N times |
| `.is_empty()` | Check if empty |
| `.index_of(sub)` | Find substring index (-1 if not found) |
| `.substring(start, end)` | Extract substring |
| `.pad_left(width, char?)` | Left-pad to width |
| `.pad_right(width, char?)` | Right-pad to width |
| `.parse_int()` | Parse as integer |
| `.parse_float()` | Parse as float |

## Array Methods

| Method | Description |
|--------|-------------|
| `.len()` | Array length |
| `.push(val)` | Append (returns new array) |
| `.contains(val)` | Check membership |
| `.reverse()` | Reverse (returns new array) |
| `.sort()` | Sort (returns new array) |
| `.map(fn)` | Transform elements |
| `.filter(fn)` | Filter elements |
| `.fold(init, fn)` | Reduce to single value |
| `.enumerate()` | Pairs of (index, value) |
| `.sum()` | Sum of numeric elements |
| `.min()` | Minimum element |
| `.max()` | Maximum element |
| `.join(sep)` | Join as string |
| `.slice(start, end)` | Extract subarray |
| `.flatten()` | Flatten nested arrays |

## Map Methods

| Method | Description |
|--------|-------------|
| `.len()` | Number of entries |
| `.keys()` | Get keys as array |
| `.values()` | Get values as array |

## Enum Methods (Option/Result)

| Method | Description |
|--------|-------------|
| `.is_some()` | Is Option::Some |
| `.is_none()` | Is Option::None |
| `.unwrap()` | Extract value (panics on None/Err) |
| `.unwrap_or(default)` | Extract or use default |
| `.is_ok()` | Is Result::Ok |
| `.is_err()` | Is Result::Err |

## CLI Commands

```
hexa <file.hexa>              Run a file (tree-walk interpreter)
hexa --test <file.hexa>       Run proof blocks as tests
hexa --vm <file.hexa>         Run with bytecode VM
hexa --native <file.hexa>     Run with Cranelift JIT
hexa --lsp                    Start language server
hexa --bench                  Run performance benchmarks
hexa init <name>              Create new project
hexa run                      Run src/main.hexa from project
hexa test                     Run all tests in tests/
hexa fmt <file.hexa>          Format source file in place
hexa fmt --check <file.hexa>  Check formatting without modifying
hexa lint <file.hexa>         Run linter
hexa add <pkg>                Add dependency to hexa.toml
hexa publish                  Create package manifest
hexa dream <file.hexa>        Consciousness exploration mode
hexa build --target <t> <f>   Cross-compile (cpu/esp32/fpga/wgpu)
hexa verify-cross <file.hexa> Verify cross-platform consistency
hexa --mem-stats <file.hexa>  Show memory region usage after execution
hexa --memory-budget N <file> Set total memory budget to N bytes
```
