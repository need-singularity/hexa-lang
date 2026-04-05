# Hexa Language Reference (for AI Code Generation)

> Copy this into your AI assistant's system prompt or CLAUDE.md to generate correct Hexa code.
> Based on working interpreter behavior. Last updated: 2026-04.

---

## Quick Rules

- No semicolons -- newline-separated statements
- `let` is mutable by default, `const` is immutable
- Explicit `return` required (no implicit last-expression return)
- Match arms use `->` not `=>`
- String concat with `+`, no interpolation -- use `format("{}", val)`
- Arrays: `[1, 2, 3]`, Maps: `#{"key": val}`
- Range: `0..10` (exclusive), `0..=10` (inclusive)
- Comments: `//` only (no block comments)
- No `+=`, `-=`, `++`, `--` operators -- use `x = x + 1`
- Enum variants carry 0 or 1 data fields (no tuples)
- No trailing commas required in match arms

---

## Types

| Type     | Literals                        |
|----------|---------------------------------|
| `int`    | `42`, `-7`, `1_000_000`        |
| `float`  | `3.14`, `0.5`                  |
| `bool`   | `true`, `false`                |
| `string` | `"hello"`, `"line\nnewline"`   |
| `void`   | (no value / unit)              |
| `Array`  | `[1, 2, 3]`                    |
| `Tuple`  | `(1, "a", true)`               |
| `Map`    | `#{ "key": value }`            |
| `Option` | `Some(42)`, `None`             |
| `Result` | `Ok(42)`, `Err("fail")`        |

---

## Variable Declaration

```hexa
let x = 42                  // mutable (can reassign)
let name: string = "hexa"   // with type annotation
const PI = 3.14159           // immutable, cannot reassign
static counter = 0           // module-level mutable global
```

---

## Functions

```hexa
fn add(a: int, b: int) -> int {
    return a + b
}

// No return type annotation needed if void
fn greet(name: string) {
    println("Hello", name)
}

// Pure function (cannot perform side effects)
pure fn double(x: int) -> int {
    return x * 2
}

// Compile-time function
comptime fn factorial(n: int) -> int {
    if n <= 1 { return 1 }
    return n * factorial(n - 1)
}
```

---

## Lambdas / Closures

```hexa
let double = |x| x * 2
let add = |x, y| x + y
let calc = |x, y| {
    let sum = x + y
    return sum * 2
}

// Closures capture enclosing scope
let factor = 10
let scale = |x| x * factor    // captures 'factor'
```

---

## Control Flow

### If / Else

```hexa
if x > 0 {
    println("positive")
} else if x == 0 {
    println("zero")
} else {
    println("negative")
}
```

### For Loops

```hexa
for i in 0..10 { println(i) }           // 0 to 9 (exclusive)
for i in 0..=10 { println(i) }          // 0 to 10 (inclusive)
for item in my_array { println(item) }   // array iteration
```

### While / Loop

```hexa
let mut i = 0
while i < 10 {
    println(i)
    i = i + 1
}

loop {
    if done { return result }
}
```

### Match

```hexa
match x {
    1 -> "one",
    2 -> "two",
    _ -> "other"
}

// Enum destructuring
match shape {
    Shape::Circle(r) -> PI * r * r,
    Shape::Rect(w) -> w * w
}

// Match with guards
match val {
    Wrapper::Val(n) if n > 100 -> "big",
    Wrapper::Val(n) if n > 10 -> "medium",
    Wrapper::Val(n) -> "small",
    _ -> "unknown"
}

// Multi-line match arms
match state {
    ConnState::Idle -> {
        match event {
            Event::Connect(port) -> ConnState::Connecting(port),
            _ -> ConnState::Idle
        }
    },
    ConnState::Closed -> ConnState::Closed
}
```

---

## Structs and Impl

```hexa
struct Point {
    x: float,
    y: float
}

impl Point {
    fn new(x: float, y: float) -> Point {
        return Point { x: x, y: y }     // no shorthand field init
    }

    fn magnitude(self) -> float {        // self, not &self
        return sqrt(self.x * self.x + self.y * self.y)
    }

    fn display(self) -> string {
        return format("({}, {})", self.x, self.y)
    }
}

let p = Point { x: 3.0, y: 4.0 }
println(p.magnitude())
```

---

## Enums

```hexa
// Simple enum
enum Color { Red, Green, Blue }

// Enum with data (one field per variant)
enum Shape {
    Circle(float),
    Square(float),
    None
}

// Usage
let s = Shape::Circle(5.0)
match s {
    Shape::Circle(r) -> println("radius:", r),
    Shape::Square(side) -> println("side:", side),
    Shape::None -> println("nothing")
}
```

---

## Traits

```hexa
trait Describable {
    fn describe(self) -> string
}

impl Describable for Point {
    fn describe(self) -> string {
        return format("Point({}, {})", self.x, self.y)
    }
}
```

---

## Error Handling

```hexa
// try / catch / throw
try {
    let result = risky_operation()
} catch e {
    println("error:", e)
}

throw "something went wrong"

// Result type
let r = Ok(42)
if r.is_ok() { println(r.unwrap()) }

let e = Err("fail")
println(e.unwrap_or(0))    // 0

// Option type
let x = Some(42)
let y = None
x.unwrap()          // 42
y.unwrap_or(0)      // 0
x.is_some()         // true
y.is_none()         // true
```

---

## Concurrency

```hexa
// Channels: create a tx/rx pair
let pair = channel()
let tx = pair[0]
let rx = pair[1]

// Spawn a green thread
spawn {
    tx.send(42)
}

let val = rx.recv()
println(val)    // 42
```

---

## Algebraic Effects

```hexa
effect Logger { fn log(msg: string) -> int }

fn do_work() { Logger.log("started"); Logger.log("done") }

let result = handle { do_work() } with {
    Logger.log(msg) => { println("[LOG]", msg); resume(1) }
}
```

---

## Ownership (Optional)

```hexa
let v = own [1, 2, 3]   // explicitly owned
let w = move v           // v is now invalid
let b = borrow w         // read-only
drop w                   // explicit deallocation
```

---

## Array Methods

```hexa
let arr = [1, 2, 3, 4, 5]

len(arr)                    // 5 (also: arr.len())
arr.push(6)                 // returns new array with 6 appended
arr.map(|x| x * 2)         // [2, 4, 6, 8, 10]
arr.filter(|x| x > 2)      // [3, 4, 5]
arr.fold(0, |a, b| a + b)  // 15
arr.sum()                   // 15
arr.sort()                  // sorted copy
arr.join(", ")              // "1, 2, 3, 4, 5"
arr.slice(1, 3)             // [2, 3]
arr[0]                      // 1 (index access)
```

---

## Maps, Strings, Modules, Tests

```hexa
// Maps
let m = #{ "name": "Alice", "age": 30 }
m["name"]                    // "Alice"
keys(m)                      // ["name", "age"]
has_key(m, "name")           // true

// Strings (no interpolation)
"hello " + "world"           // concat with +
format("{} is {}", "x", 42)  // "x is 42"
to_string(42)                // "42"

// Modules
use math                     // imports math.hexa
math::double(6)              // call with :: prefix

// Proof blocks (tests) -- run with: ./hexa --test file.hexa
proof my_test {
    assert 1 + 1 == 2
    assert len([1, 2, 3]) == 3
}
```

---

## Builtins Quick Reference

**I/O:** `println(args...)` `print(args...)` `print_err(args...)`
**Math:** `abs(x)` `min(a,b)` `max(a,b)` `floor(x)` `ceil(x)` `round(x)` `sqrt(x)` `pow(base,exp)` `log(x)` `log2(x)` `sin(x)` `cos(x)` `tan(x)` -- constants: `PI` `E`
**Convert:** `to_string(x)` `to_int(x)` `to_float(x)` `format(fmt, args...)` (uses `{}` placeholders)
**Collections:** `len(x)` `keys(map)` `values(map)` `has_key(map,key)` `Set()`
**File I/O:** `read_file(path)` `write_file(path,data)` `append_file(path,data)` `file_exists(path)` `delete_file(path)`
**Concurrency:** `channel()` -> `[tx, rx]`, `spawn { }`, `tx.send(val)`, `rx.recv()`, `join(future)`
**System:** `clock()` `now()` `timestamp()` `elapsed()` `sleep(ms)` `args()` `env_var(name)` `exit(code)` `type_of(x)`
**JSON:** `json_parse(str)` `json_stringify(val)`
**Encoding:** `base64_encode(str)` `base64_decode(str)` `hex_encode(str)` `hex_decode(str)`
**HTTP:** `http_get(url)` `http_post(url, body)`
**Random:** `random()` (float 0..1) `random_int(min, max)`
**Char:** `is_alpha(c)` `is_digit(c)` `is_alphanumeric(c)` `is_whitespace(c)`
**Number Theory:** `sigma(n)` `phi(n)` `tau(n)` `gcd(a,b)` `sopfr(n)`

---

## Common Patterns

### 1. Accumulate into Array

```hexa
let mut result = []
for i in 0..10 {
    result = result.push(i * i)
}
```

### 2. Struct with Methods

```hexa
struct Counter { value: int }
impl Counter {
    fn new() -> Counter { return Counter { value: 0 } }
    fn inc(self) -> Counter { return Counter { value: self.value + 1 } }
    fn get(self) -> int { return self.value }
}
```

### 3. Enum State Machine

```hexa
enum State { Ready, Running(int), Done }
fn step(s) {
    match s {
        State::Ready -> State::Running(0),
        State::Running(n) if n >= 10 -> State::Done,
        State::Running(n) -> State::Running(n + 1),
        State::Done -> State::Done
    }
}
```

### 4. Pipeline Processing

```hexa
let data = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
let result = data.filter(|x| x % 2 == 0).map(|x| x * x)
```

### 5. Concurrent Workers

```hexa
let pair = channel()
let tx = pair[0]
let rx = pair[1]
spawn { tx.send(expensive_compute()) }
let answer = rx.recv()
```

### 6. Error-Safe Wrapper

```hexa
fn safe_divide(a: float, b: float) -> float {
    if b == 0.0 { throw "division by zero" }
    return a / b
}
try {
    println(safe_divide(10.0, 0.0))
} catch e {
    println("Error:", e)
}
```

### 7. Map as Config

```hexa
let config = #{ "host": "localhost", "port": 8080, "debug": true }
if has_key(config, "debug") {
    println("Debug mode:", config["debug"])
}
```

### 8. Recursive Function

```hexa
fn fib(n: int) -> int {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}
```

### 9. Proof-Driven Development

```hexa
fn add(a: int, b: int) -> int { return a + b }

proof test_add {
    assert add(2, 3) == 5
    assert add(-1, 1) == 0
    assert add(0, 0) == 0
}
```

---

## Gotchas (Things AI Gets Wrong)

- **No semicolons.** Newlines separate statements. Adding `;` is a syntax error.
- **`let` is mutable.** Unlike Rust, `let x = 5` allows `x = 10` later. Use `const` for immutability.
- **Explicit `return` required.** `fn add(a, b) { a + b }` does NOT return -- you need `return a + b`.
- **Match uses `->` not `=>`.** Write `1 -> "one"`, not `1 => "one"`.
- **No string interpolation.** Use `format("{} items", count)` or `"prefix " + to_string(n)`.
- **No `+=`, `-=`, `++`, `--`.** Write `x = x + 1`.
- **`push` returns a new array.** `arr.push(x)` does not mutate; use `arr = arr.push(x)`.
- **Enum variants hold 0 or 1 field.** `Pair(int, int)` is invalid; wrap in a struct instead.
- **`self` not `&self`.** In `impl` methods, write `fn method(self)`, not `fn method(&self)`.
- **No field shorthand.** Write `Point { x: x, y: y }`, not `Point { x, y }`.
- **`channel()` returns an array.** Access tx/rx as `pair[0]` and `pair[1]`.
- **`spawn` uses blocks not closures.** Write `spawn { ... }`, not `spawn(|| { ... })`.
- **`proof` not `test`.** Test blocks use the `proof` keyword and run with `--test`.
- **`assert` not `assert_eq`.** Use `assert expr == expected` inside proof blocks.
- **Map literal prefix.** Maps use `#{ }` not just `{ }` (bare braces are blocks).
- **No `else if` keyword fusion.** It is `} else if {`, not `} elif {` or `elsif`.
