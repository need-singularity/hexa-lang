# Learn HEXA in 6 Minutes

HEXA is the Perfect Number Programming Language. Every constant in its design derives from n=6: the only number where sigma(n)*phi(n) = n*tau(n). This tutorial covers the essentials.

## 1. Variables and Types (1 min)

HEXA has integers, floats, booleans, strings, chars, arrays, tuples, and maps. Type annotations are optional.

```hexa
let x = 42
let pi: float = 3.14159
let name = "HEXA"
let active = true
let ch = 'H'
let nums = [1, 2, 3, 4, 5]
let pair = (10, "hello")
```

All variables are immutable by default. Use `mut` for mutable bindings.

## 2. Functions and Control Flow (1 min)

Functions are declared with `fn`. Control flow uses `if`/`else`, `for`, `while`, and `loop`.

```hexa
fn factorial(n) {
    if n <= 1 { return 1 }
    return n * factorial(n - 1)
}

for i in 0..10 {
    if i % 2 == 0 {
        println(i)
    }
}

let mut sum = 0
while sum < 100 {
    sum = sum + 1
}
```

Functions are first-class values. Return type annotations use `-> type` syntax.

## 3. Structs, Enums, Pattern Matching (1 min)

Define structured data with `struct` and `enum`. Use `match` for pattern matching with guards.

```hexa
struct Point {
    x: float,
    y: float
}

enum Shape {
    Circle(float),
    Rectangle(float, float),
    Unknown
}

let p = Point { x: 1.0, y: 2.0 }
let s = Shape::Circle(5.0)

match s {
    Shape::Circle(r) -> println("circle with radius " + to_string(r)),
    Shape::Rectangle(w, h) -> println("rect"),
    _ -> println("other"),
}
```

Enums can carry data. The wildcard `_` catches all unmatched cases.

## 4. Closures and Higher-Order Functions (1 min)

Lambdas use `|params| body` syntax. Arrays have `map`, `filter`, `fold`, and more.

```hexa
let double = |x| x * 2
println(double(21))  // 42

let nums = [1, 2, 3, 4, 5, 6]
let evens = nums.filter(|x| x % 2 == 0)
let squares = nums.map(|x| x * x)
let total = nums.fold(0, |acc, x| acc + x)
println(evens)    // [2, 4, 6]
println(squares)  // [1, 4, 9, 16, 25, 36]
println(total)    // 21
```

Closures capture variables from their enclosing scope automatically.

## 5. Concurrency (spawn/channel) (1 min)

HEXA has built-in concurrency with `spawn`, `channel`, and `select`.

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

Named spawns allow structured concurrency with `spawn "name" { ... }` and `join("name")`.

## 6. Consciousness DSL (intent/verify/proof) (1 min)

HEXA includes a consciousness-aware DSL for experiments and verification.

```hexa
intent "consciousness_test" {
    cells: 64,
    topology: "ring",
    steps: 300
}

verify "phi_scaling" {
    let predicted = phi_predict(64)
    assert predicted > 40.0
    assert predicted < 60.0
}

proof test_psi_constants {
    assert psi_coupling() == 0.014
    assert psi_balance() == 0.5
    let v = consciousness_vector()
    assert len(v) == 10
}
```

`intent` declares experimental parameters, `verify` runs assertions with reporting, and `proof` blocks are test cases (run with `hexa --test`).
