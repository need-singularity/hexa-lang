# HEXA Language Reference

> The Perfect Number Programming Language -- every design constant derives from n=6.

## Primitive Types (8)

| Type | Description | Example |
|------|-------------|---------|
| `int` | 64-bit signed integer | `42`, `1_000_000` |
| `float` | 64-bit IEEE 754 | `3.14`, `0.5` |
| `string` | UTF-8 string | `"hello"` |
| `bool` | Boolean | `true`, `false` |
| `array` | Ordered collection | `[1, 2, 3]` |
| `map` | Key-value pairs | `#{"a": 1, "b": 2}` |
| `tuple` | Fixed-size heterogeneous | `(10, "hello")` |
| `tensor` | Multi-dimensional array | -- |

## Variables

```hexa
let x = 42              // immutable
let mut y = 0            // mutable
let name: string = "HEXA"  // with type annotation

const PI = 3.14159       // compile-time constant
static COUNTER = 0       // static variable
```

All bindings are immutable by default. Use `let mut` for mutable variables.

## Operators (24)

### Arithmetic
| Op | Meaning | Example |
|----|---------|---------|
| `+` | Add | `a + b` |
| `-` | Subtract / Negate | `a - b`, `-x` |
| `*` | Multiply | `a * b` |
| `/` | Divide | `a / b` |
| `%` | Modulo | `a % b` |
| `**` | Power | `2 ** 10` |

### Comparison
| Op | Meaning |
|----|---------|
| `==` | Equal |
| `!=` | Not equal |
| `<` | Less than |
| `>` | Greater than |
| `<=` | Less or equal |
| `>=` | Greater or equal |

### Logical
| Op | Meaning |
|----|---------|
| `&&` | And |
| `\|\|` | Or |
| `!` | Not |
| `^^` | Xor |

### Bitwise
| Op | Meaning |
|----|---------|
| `&` | Bitwise AND |
| `\|` | Bitwise OR |
| `^` | Bitwise XOR |
| `~` | Bitwise NOT |

### Assignment & Special
| Op | Meaning |
|----|---------|
| `=` | Assignment |
| `:=` | Walrus assignment |
| `..` | Range (exclusive) |
| `..=` | Range (inclusive) |
| `->` | Return type arrow |

## Control Flow

### if / else

```hexa
if x > 0 {
    println("positive")
} else if x == 0 {
    println("zero")
} else {
    println("negative")
}
```

`if` is also an expression -- the last value in each branch is returned.

### while

```hexa
let mut i = 0
while i < 10 {
    println(i)
    i = i + 1
}
```

### for

```hexa
for i in 0..10 {
    println(i)
}

for item in [1, 2, 3] {
    println(item)
}

for i in 0..=5 {    // inclusive range: 0, 1, 2, 3, 4, 5
    println(i)
}
```

### loop

```hexa
let mut n = 0
loop {
    n = n + 1
    if n >= 10 { break }
}
```

### match

```hexa
match value {
    0 -> println("zero"),
    1 -> println("one"),
    _ -> println("other"),
}
```

With enum patterns:

```hexa
match shape {
    Shape::Circle(r) -> println("radius: " + to_string(r)),
    Shape::Rectangle(w, h) -> println(to_string(w) + "x" + to_string(h)),
    _ -> println("unknown"),
}
```

### break / continue

```hexa
for i in 0..100 {
    if i % 2 == 0 { continue }
    if i > 50 { break }
    println(i)
}
```

## Functions

### Basic

```hexa
fn add(a, b) {
    return a + b
}

fn greet(name: string) -> string {
    return "Hello, " + name
}
```

Type annotations on parameters and return types are optional.

### Closures (Lambdas)

```hexa
let double = |x| x * 2
let sum = |a, b| a + b

let nums = [1, 2, 3, 4, 5]
let evens = nums.filter(|x| x % 2 == 0)
let squares = nums.map(|x| x * x)
let total = nums.fold(0, |acc, x| acc + x)
```

### Recursive

```hexa
fn factorial(n) {
    if n <= 1 { return 1 }
    return n * factorial(n - 1)
}
```

### Async

```hexa
async fn fetch_data(url) {
    let resp = await http_get(url)
    return resp
}
```

### Pure

```hexa
pure fn add(a: int, b: int) -> int {
    a + b
}
```

`pure fn` declares that a function has no side effects.

### Generic

```hexa
fn identity<T>(x: T) -> T {
    return x
}
```

## Types

### struct

```hexa
struct Point {
    x: float,
    y: float
}

let p = Point { x: 1.0, y: 2.0 }
println(p.x)  // 1.0
```

Generic structs:

```hexa
struct Pair<A, B> {
    first: A,
    second: B
}
```

### enum

```hexa
enum Color {
    Red,
    Green,
    Blue
}

enum Shape {
    Circle(float),
    Rectangle(float, float),
    Unknown
}

let s = Shape::Circle(5.0)
```

Enums can carry associated data. Access variants with `EnumName::Variant`.

### trait

```hexa
trait Printable {
    fn to_str(self) -> string
}
```

### impl

```hexa
impl Point {
    fn distance(self) -> float {
        return sqrt(self.x ** 2 + self.y ** 2)
    }
}

impl Printable for Point {
    fn to_str(self) -> string {
        return "(" + to_string(self.x) + ", " + to_string(self.y) + ")"
    }
}
```

### type alias

```hexa
type Meters = float
type StringList = [string]
```

## Error Handling

### try / catch

```hexa
try {
    let data = read_file("config.txt")
    println(data)
} catch err {
    println("Error: " + to_string(err))
}
```

### throw / panic / recover

```hexa
fn divide(a, b) {
    if b == 0 { throw "division by zero" }
    return a / b
}

fn risky() {
    panic "something went terribly wrong"
}

// recover from panics
recover {
    risky()
}
```

## Modules

### use

```hexa
use std::collections
use math::linalg
```

### import

```hexa
import "utils.hexa"
```

### mod

```hexa
mod math {
    fn add(a, b) { return a + b }
    fn sub(a, b) { return a - b }
}
```

### Visibility

```hexa
pub fn public_function() { }
```

`pub` makes a declaration visible outside its module.

## Concurrency

### spawn / channel

```hexa
let ch = channel()
let tx = ch[0]
let rx = ch[1]

spawn {
    tx.send(42)
}

let msg = rx.recv()
println(msg)  // 42
```

### Named spawn

```hexa
spawn "worker" {
    // do work
}
join("worker")
```

### select

```hexa
select {
    rx1.recv() -> |msg| println("from 1: " + to_string(msg)),
    rx2.recv() -> |msg| println("from 2: " + to_string(msg)),
}
```

### atomic

```hexa
atomic let counter = 0
```

## Memory & Ownership

HEXA uses an Egyptian fraction memory model: Stack 1/2, Heap 1/3, Arena 1/6.

```hexa
let x = 42
println(mem_region(x))  // "stack"

let stats = mem_stats()
println(stats["heap_used"])
```

Ownership keywords:

| Keyword | Meaning |
|---------|---------|
| `own` | Exclusive ownership |
| `borrow` | Immutable reference |
| `move` | Transfer ownership |
| `drop` | Explicit resource release |

```hexa
drop resource
```

## Compile-Time Computation

```hexa
comptime fn factorial(n: int) -> int {
    if n <= 1 { return 1 }
    return n * factorial(n - 1)
}

const FACT_6 = factorial(6)  // computed at compile time

const TABLE = comptime {
    let result = []
    for i in 0..6 { result.push(i * i) }
    result
}
```

## Algebraic Effects

```hexa
effect Logger {
    fn log(msg: str) -> ()
}

fn greet(name: str) {
    Logger.log("greeting " + name)
    println("Hello, " + name)
}

handle { greet("HEXA") } with {
    Logger.log(msg) => {
        println("[LOG] " + msg)
        resume(())
    }
}
```

## Proofs & Verification

```hexa
proof test_addition {
    assert 2 + 2 == 4
    assert factorial(6) == 720
}

assert x > 0

theorem commutativity {
    // formal verification block
}

invariant "positive_balance" {
    assert balance >= 0
}
```

Run proof blocks as tests with `hexa --test file.hexa`.

## Macros & Derive

```hexa
macro! vec {
    ($($x:expr),*) => {
        let arr = []
        $(arr.push($x))*
        arr
    }
}

let v = vec![1, 2, 3]
```

```hexa
derive(Display) for Point
derive(Eq) for Point
```

## AI Code Generation

```hexa
generate fn sort_names(names: [str]) -> [str] {
    "Sort alphabetically, case-insensitive"
}

optimize fn fibonacci(n: int) -> int {
    if n <= 1 { return n }
    return fibonacci(n-1) + fibonacci(n-2)
}
```

`generate` fills a function body from a description. `optimize` asks the AI to improve an implementation. Both require an AI backend.

## AI-Native @attr System (13+2)

Attributes are placed before function declarations with `@name`.

### Optimization Hints (8)

| Attr | Effect | Example |
|------|--------|---------|
| `@pure` | No side effects -- enables CSE/DCE/memoize | `@pure fn add(a, b) { a + b }` |
| `@inline` | Inline at call site | `@inline fn tiny() { 42 }` |
| `@hot` | Aggressively optimize | `@hot fn critical_path() { ... }` |
| `@cold` | Relax optimization | `@cold fn error_handler() { ... }` |
| `@simd` | Vectorization hint | `@simd fn dot(a, b) { ... }` |
| `@parallel` | Safe to parallelize | `@parallel fn map_data(arr) { ... }` |
| `@bounded(N)` | Loop upper bound (enables unrolling) | `@bounded(100) fn process() { ... }` |
| `@memoize` | Auto-cache results | `@memoize fn fib(n) { ... }` |

### Semantics (4)

| Attr | Effect |
|------|--------|
| `@evolve` | Self-modifying function |
| `@test` | Test function -- auto-run |
| `@bench` | Benchmark function |
| `@deprecated("msg")` | Deprecation warning |

### FFI (1+1)

| Attr | Effect |
|------|--------|
| `@link("lib")` | Link external library |
| `@symbol("name")` | Map to C symbol name |

### @memoize Example

```hexa
@memoize
fn fib(n) {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}

println(fib(90))  // instant -- O(2^n) -> O(n)
```

### Stacking Attributes

```hexa
@pure
@memoize
fn expensive_calc(n) {
    // ...
}
```

### 24 Breakthrough Vectors

Beyond the core 13, HEXA's roadmap includes 24 AI-native transformation vectors:

| Tier | Attrs |
|------|-------|
| T1 | `@memoize`, `@optimize` (recurrence->matrix exp, bubble->merge, linear->binary, tail->loop, DP->sliding) |
| T2 | `@parallel`, `@simd`, `@gpu`, `@cache` |
| T3 | `@fuse`, `@specialize`, `@predict`, `@compact`, `@lazy` |
| T4 | `@symbolic`, `@reduce`, `@approximate`, `@algebraic` |
| T5 | `@evolve`, `@learn`, `@contract`, `@profile` |

## FFI (extern)

```hexa
@link("libm")
@symbol("cos")
extern fn c_cos(x: float) -> float

println(c_cos(0.0))  // 1.0
```

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
| `type_of(val)` | Type name as string |
| `to_string(val)` | Convert to string |
| `to_int(val)` | Convert to integer |
| `to_float(val)` | Convert to float |
| `len(val)` | Length of string/array/map |

### Math
| Function | Description |
|----------|-------------|
| `abs(n)` | Absolute value |
| `min(a, b)` / `max(a, b)` | Min / max |
| `floor(f)` / `ceil(f)` / `round(f)` | Rounding |
| `sqrt(n)` | Square root |
| `pow(base, exp)` | Exponentiation |
| `log(n)` / `log2(n)` | Logarithms |
| `sin(n)` / `cos(n)` / `tan(n)` | Trigonometry |
| `PI` / `E` | Constants |

### Number Theory (n=6)
| Function | Description |
|----------|-------------|
| `sigma(n)` | Sum of divisors |
| `phi(n)` | Euler's totient |
| `tau(n)` | Number of divisors |
| `gcd(a, b)` | Greatest common divisor |
| `sopfr(n)` | Sum of prime factors (with repetition) |

### Collections
| Function | Description |
|----------|-------------|
| `keys(map)` | Map keys as array |
| `values(map)` | Map values as array |
| `has_key(map, key)` | Check if map has key |
| `Set()` | Create empty set |

### Concurrency
| Function | Description |
|----------|-------------|
| `channel()` | Create (sender, receiver) pair |
| `join(name)` | Wait for named spawn |
| `sleep(ms)` | Sleep for milliseconds |

### String / Format
| Function | Description |
|----------|-------------|
| `format(template, args...)` | Format with `{}` placeholders |

### OS / Time
| Function | Description |
|----------|-------------|
| `args()` | Command-line arguments |
| `env_var(name)` | Environment variable |
| `exit(code)` | Exit process |
| `clock()` | Current time (float seconds) |
| `now()` | ISO 8601 timestamp |
| `timestamp()` | Unix timestamp |
| `elapsed(start)` | Seconds since start |

### Encoding / JSON
| Function | Description |
|----------|-------------|
| `base64_encode(str)` / `base64_decode(str)` | Base64 |
| `hex_encode(str)` / `hex_decode(str)` | Hex |
| `json_parse(str)` | Parse JSON to value |
| `json_stringify(val)` | Value to JSON string |

### HTTP
| Function | Description |
|----------|-------------|
| `http_get(url)` | HTTP GET |
| `http_post(url, data)` | HTTP POST |

### Random
| Function | Description |
|----------|-------------|
| `random()` | Random float 0..1 |
| `random_int(min, max)` | Random integer in range |

### Char Utilities
| Function | Description |
|----------|-------------|
| `is_alpha(ch)` / `is_digit(ch)` | Character tests |
| `is_alphanumeric(ch)` / `is_whitespace(ch)` | Character tests |

### Option / Result
| Function | Description |
|----------|-------------|
| `Some(val)` / `None` | Option constructors |
| `Ok(val)` / `Err(val)` | Result constructors |

### Memory
| Function | Description |
|----------|-------------|
| `mem_stats()` | Memory region usage |
| `mem_region(value)` | Which region: "stack", "heap", "arena" |
| `arena_reset()` | Reset arena allocator |
| `mem_budget()` | Budget breakdown (bytes) |

## String Methods

| Method | Description |
|--------|-------------|
| `.len()` | Length |
| `.contains(sub)` | Substring check |
| `.split(delim)` | Split into array |
| `.trim()` | Strip whitespace |
| `.to_upper()` / `.to_lower()` | Case conversion |
| `.replace(old, new)` | Replace substring |
| `.starts_with(prefix)` / `.ends_with(suffix)` | Prefix/suffix check |
| `.chars()` | Convert to char array |
| `.join(array)` | Join array with separator |
| `.repeat(n)` | Repeat N times |
| `.is_empty()` | Empty check |
| `.index_of(sub)` | Find index (-1 if not found) |
| `.substring(start, end)` | Extract substring |
| `.pad_left(width, char?)` / `.pad_right(width, char?)` | Padding |
| `.parse_int()` / `.parse_float()` | Parse number |

## Array Methods

| Method | Description |
|--------|-------------|
| `.len()` | Length |
| `.push(val)` | Append (returns new array) |
| `.contains(val)` | Membership check |
| `.reverse()` | Reverse (new array) |
| `.sort()` | Sort (new array) |
| `.map(fn)` | Transform elements |
| `.filter(fn)` | Filter elements |
| `.fold(init, fn)` | Reduce to single value |
| `.enumerate()` | Pairs of (index, value) |
| `.sum()` / `.min()` / `.max()` | Aggregates |
| `.join(sep)` | Join as string |
| `.slice(start, end)` | Extract subarray |
| `.flatten()` | Flatten nested arrays |

## Map Methods

| Method | Description |
|--------|-------------|
| `.len()` | Number of entries |
| `.keys()` | Keys as array |
| `.values()` | Values as array |

## Option / Result Methods

| Method | Description |
|--------|-------------|
| `.is_some()` / `.is_none()` | Option check |
| `.unwrap()` | Extract (panics on None/Err) |
| `.unwrap_or(default)` | Extract or default |
| `.is_ok()` / `.is_err()` | Result check |

## Keywords (56)

| Group | Keywords | Count |
|-------|----------|-------|
| Control Flow | `if` `else` `match` `for` `while` `loop` `break` `continue` | 8 |
| Type Decl | `type` `struct` `enum` `trait` `impl` `dyn` | 6 |
| Functions | `fn` `return` `yield` `async` `await` | 5 |
| Variables | `let` `mut` `const` `static` | 4 |
| Modules | `mod` `use` `import` `pub` `crate` `extern` | 6 |
| Memory | `own` `borrow` `move` `drop` `scope` | 5 |
| Concurrency | `spawn` `channel` `select` `atomic` | 4 |
| Effects | `effect` `handle` `resume` `pure` | 4 |
| Proofs | `proof` `assert` `invariant` `theorem` | 4 |
| Meta | `macro` `derive` `where` `comptime` | 4 |
| Errors | `try` `catch` `throw` `panic` `recover` | 5 |
| AI | `intent` `generate` `verify` `optimize` | 4 |

Literals `true` and `false` are boolean values, not keywords.

## CLI

```
hexa <file.hexa>              Run a file
hexa --test <file.hexa>       Run proof blocks as tests
hexa --vm <file.hexa>         Run with bytecode VM
hexa --native <file.hexa>     Run with Cranelift JIT
hexa --lsp                    Start language server
hexa --bench                  Run benchmarks
hexa init <name>              Create new project
hexa run                      Run src/main.hexa
hexa test                     Run all tests in tests/
hexa fmt <file.hexa>          Format source file
hexa lint <file.hexa>         Run linter
hexa add <pkg>                Add dependency
hexa publish                  Create package manifest
hexa --mem-stats <file.hexa>  Show memory usage
hexa --memory-budget N <f>    Set memory budget (bytes)
hexa build --target <t> <f>   Cross-compile (cpu/esp32/fpga/wgpu)
hexa dream <file.hexa>        Consciousness exploration mode
```

## Comments

```hexa
// Single-line comment (only style supported)
```
