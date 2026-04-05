# Hexa Language Conversion Cheatsheet

> For AI assistants and developers converting code from Python, JavaScript, Go, and Rust into Hexa.
> Based on actual working interpreter behavior as of the current codebase.

---

## 1. Quick Reference

### Types

| Type     | Literal Examples               |
|----------|-------------------------------|
| `int`    | `42`, `1_000_000`, `-7`       |
| `float`  | `3.14`, `0.5`                 |
| `bool`   | `true`, `false`               |
| `string` | `"hello"`, `"line\nnewline"` |
| `char`   | (chars via string indexing)    |
| `array`  | `[1, 2, 3]`                  |
| `tuple`  | `(1, "a", true)`             |
| `map`    | `#{ "key": value }`          |
| `void`   | (no value / unit)             |

### Variables

```hexa
let x = 42                  // immutable binding
let name: string = "hexa"   // with type annotation
x = 100                     // reassignment is allowed (let is NOT const)

const PI_APPROX = 3.14159   // compile-time constant, cannot be reassigned
comptime const N = 6        // compile-time evaluated constant
static counter = 0          // module-level mutable global
```

### Functions

```hexa
fn add(a: int, b: int) -> int {
    return a + b
}

// Pure function (cannot perform effects)
pure fn double(x: int) -> int {
    x * 2
}

// Compile-time function
comptime fn factorial(n: int) -> int {
    if n <= 1 { return 1 }
    return n * factorial(n - 1)
}
```

### Lambdas / Closures

```hexa
let add = |x, y| x + y
let mul = |x| x * factor        // captures 'factor' from enclosing scope
let calc = |x, y| {             // block body
    let sum = x + y
    return sum * 2
}
```

### Control Flow

```hexa
// If/else (expression — returns a value)
if x > 0 {
    "positive"
} else {
    "non-positive"
}

// For loop (over range or array)
for i in 0..10 { println(i) }        // exclusive: 0 to 9
for i in 0..=10 { println(i) }       // inclusive: 0 to 10
for item in my_array { println(item) }

// While loop
while x > 0 {
    x = x - 1
}

// Infinite loop
loop {
    if done { return result }
}
```

### Match Expression

```hexa
match x {
    1 -> "one"
    2 -> "two"
    _ -> "other"
}

// Enum matching with destructuring
match shape {
    Shape::Circle(r) -> r * r * 3
    Shape::Rect(w) -> w * w
}

// Match with guards
match wrapper {
    Wrapper::Val(n) if n > 100 -> "big"
    Wrapper::Val(n) if n > 10 -> "medium"
    Wrapper::Val(n) -> "small"
    _ -> "unknown"
}
```

### Comments

```hexa
// Single-line comment (only style available)
```

---

## 2. Python to Hexa

### Variables

```python
# Python
x = 42
name = "hello"
PI = 3.14159
```

```hexa
// Hexa
let x = 42
let name = "hello"
const PI = 3.14159
```

### Functions

```python
# Python
def add(a, b):
    return a + b

greet = lambda name: f"hello {name}"
```

```hexa
// Hexa
fn add(a: int, b: int) -> int {
    return a + b
}

let greet = |name| "hello " + name
```

### Control Flow

```python
# Python
if x > 0:
    print("positive")
elif x == 0:
    print("zero")
else:
    print("negative")

for i in range(10):
    print(i)

for item in my_list:
    print(item)

while x > 0:
    x -= 1
```

```hexa
// Hexa
if x > 0 {
    println("positive")
} else if x == 0 {
    println("zero")
} else {
    println("negative")
}

for i in 0..10 {
    println(i)
}

for item in my_array {
    println(item)
}

while x > 0 {
    x = x - 1       // no -= operator
}
```

### Data Structures

```python
# Python
nums = [1, 2, 3]
nums.append(4)
length = len(nums)

person = {"name": "Alice", "age": 30}
keys = person.keys()
```

```hexa
// Hexa
let nums = [1, 2, 3]
nums = nums.push(4)         // push returns a new array
let length = len(nums)       // or nums.len()

let person = #{ "name": "Alice", "age": 30 }
let k = keys(person)        // or person.keys()
```

### List Comprehensions / Functional

```python
# Python
doubled = [x * 2 for x in nums]
evens = [x for x in nums if x % 2 == 0]
total = sum(nums)
```

```hexa
// Hexa
let doubled = nums.map(|x| x * 2)
let evens = nums.filter(|x| x % 2 == 0)
let total = nums.sum()
```

### Error Handling

```python
# Python
try:
    result = risky_operation()
except Exception as e:
    print(f"error: {e}")
    
raise ValueError("something went wrong")
```

```hexa
// Hexa
try {
    let result = risky_operation()
} catch e {
    println("error:", e)
}

throw "something went wrong"
```

### Classes to Structs

```python
# Python
class Point:
    def __init__(self, x, y):
        self.x = x
        self.y = y

    def distance(self):
        return (self.x**2 + self.y**2)**0.5

p = Point(3, 4)
print(p.distance())
```

```hexa
// Hexa
struct Point {
    x: float,
    y: float
}

impl Point {
    fn distance(self) -> float {
        return sqrt(self.x * self.x + self.y * self.y)
    }
}

let p = Point { x: 3.0, y: 4.0 }
println(p.distance())
```

### String Formatting

```python
# Python
name = "world"
msg = f"hello {name}"
formatted = "{} + {} = {}".format(1, 2, 3)
```

```hexa
// Hexa
let name = "world"
let msg = "hello " + name
let formatted = format("{} + {} = {}", 1, 2, 3)
```

---

## 3. JavaScript to Hexa

### Variables

```javascript
// JavaScript
let x = 42;
const PI = 3.14159;
let arr = [1, 2, 3];
```

```hexa
// Hexa
let x = 42
const PI = 3.14159
let arr = [1, 2, 3]
// No semicolons needed. Newlines are statement terminators.
```

### Functions

```javascript
// JavaScript
function add(a, b) { return a + b; }
const double = (x) => x * 2;
const calc = (x, y) => { return (x + y) * 2; };
```

```hexa
// Hexa
fn add(a: int, b: int) -> int { return a + b }
let double = |x| x * 2
let calc = |x, y| {
    return (x + y) * 2
}
```

### Objects to Structs/Maps

```javascript
// JavaScript
const point = { x: 3, y: 4 };
console.log(point.x);

class Circle {
    constructor(r) { this.radius = r; }
    area() { return Math.PI * this.radius ** 2; }
}
```

```hexa
// Hexa — as a map (dynamic)
let point = #{ "x": 3, "y": 4 }
println(point["x"])           // map access uses string keys

// Hexa — as a struct (typed)
struct Circle {
    radius: float
}
impl Circle {
    fn area(self) -> float {
        return PI * self.radius * self.radius
    }
}
let c = Circle { radius: 5.0 }
println(c.area())
```

### Array Methods

```javascript
// JavaScript
const doubled = arr.map(x => x * 2);
const evens = arr.filter(x => x % 2 === 0);
const sum = arr.reduce((a, b) => a + b, 0);
const sorted = [...arr].sort();
const joined = arr.join(", ");
```

```hexa
// Hexa
let doubled = arr.map(|x| x * 2)
let evens = arr.filter(|x| x % 2 == 0)
let total = arr.fold(0, |a, b| a + b)     // or arr.sum() for numeric
let sorted = arr.sort()
let joined = arr.join(", ")
```

### Promises/Async to Spawn/Channel

```javascript
// JavaScript
async function fetchData() {
    const result = await fetch(url);
    return result;
}
```

```hexa
// Hexa — concurrency uses spawn + channels
let pair = channel()
let tx = pair[0]
let rx = pair[1]

spawn {
    let data = http_get("https://example.com")
    tx.send(data)
}

let result = rx.recv()
```

### Error Handling

```javascript
// JavaScript
try { riskyCall(); } catch (e) { console.error(e); }
throw new Error("failed");
```

```hexa
// Hexa
try { risky_call() } catch e { println(e) }
throw "failed"
```

### Modules

```javascript
// JavaScript
import { double } from './math.js';
export function triple(x) { return x * 3; }
```

```hexa
// Hexa — file modules
// In math.hexa:
pub fn double(x: int) -> int { return x * 2 }

// In main.hexa:
use math
let result = math::double(6)   // 12
```

---

## 4. Rust to Hexa

Hexa's syntax is closest to Rust. Key differences are noted.

### Variables

```rust
// Rust
let x: i64 = 42;
let mut counter = 0;
const PI: f64 = 3.14159;
static GLOBAL: i64 = 100;
```

```hexa
// Hexa
let x: int = 42
let counter = 0           // all let bindings are reassignable (no mut needed)
const PI = 3.14159         // immutable constant
static GLOBAL = 100        // module-level mutable variable
```

> **Key difference**: In Hexa, `let` bindings can be reassigned (unlike Rust's default immutability). Use `const` for true immutability.

### Functions

```rust
// Rust
fn add(a: i64, b: i64) -> i64 { a + b }
fn greet(name: &str) -> String { format!("hello {}", name) }
```

```hexa
// Hexa
fn add(a: int, b: int) -> int { return a + b }
fn greet(name: string) -> string { return format("hello {}", name) }
```

> **Key difference**: Hexa requires explicit `return` in most cases. The last expression in a block is NOT automatically returned (except in if/match expressions).

### Structs and Impl

```rust
// Rust
struct Point { x: f64, y: f64 }
impl Point {
    fn new(x: f64, y: f64) -> Self { Point { x, y } }
    fn magnitude(&self) -> f64 { (self.x*self.x + self.y*self.y).sqrt() }
}
let p = Point::new(3.0, 4.0);
println!("{}", p.magnitude());
```

```hexa
// Hexa
struct Point { x: float, y: float }
impl Point {
    fn new(x: float, y: float) -> Point {
        return Point { x: x, y: y }      // no shorthand field init
    }
    fn magnitude(self) -> float {         // no & — just self
        return sqrt(self.x * self.x + self.y * self.y)
    }
}
let p = Point::new(3.0, 4.0)
println(p.magnitude())
```

### Enums and Pattern Matching

```rust
// Rust
enum Shape { Circle(f64), Rect(f64, f64) }
match shape {
    Shape::Circle(r) => std::f64::consts::PI * r * r,
    Shape::Rect(w, h) => w * h,
}
```

```hexa
// Hexa
enum Shape { Circle(float), Rect(float) }
match shape {
    Shape::Circle(r) -> PI * r * r
    Shape::Rect(w) -> w * w        // single data field per variant
}
```

> **Key difference**: Hexa enum variants carry 0 or 1 data fields (not tuples). Use `->` instead of `=>` in match arms. No commas between arms.

### Traits

```rust
// Rust
trait Describable { fn describe(&self) -> String; }
impl Describable for Point {
    fn describe(&self) -> String { format!("Point({}, {})", self.x, self.y) }
}
```

```hexa
// Hexa
trait Describable {
    fn describe(self) -> string
}
impl Describable for Point {
    fn describe(self) -> string {
        return format("Point({}, {})", self.x, self.y)
    }
}
```

### Option and Result

```rust
// Rust
let x: Option<i32> = Some(42);
let y: Option<i32> = None;
let ok: Result<i32, String> = Ok(42);
let err: Result<i32, String> = Err("fail".to_string());
```

```hexa
// Hexa — built-in constructors
let x = Some(42)
let y = None
let ok = Ok(42)
let err = Err("fail")

// Methods work the same
x.unwrap()           // 42
x.is_some()          // true
y.unwrap_or(0)       // 0
ok.is_ok()           // true
err.unwrap_or(0)     // 0
```

### Ownership (Simplified)

```rust
// Rust
let v = vec![1, 2, 3];
let w = v;            // v is moved
// println!("{:?}", v);  // ERROR: use after move
```

```hexa
// Hexa
let v = own [1, 2, 3]
let w = move v
// println(v)          // ERROR: cannot access 'v': value has been moved

let b = borrow w       // read-only reference
// b = [4, 5, 6]       // ERROR: cannot mutate borrowed value
```

### Macros

```rust
// Rust
macro_rules! double { ($x:expr) => { $x + $x }; }
let r = double!(5);   // 10
```

```hexa
// Hexa
macro! double {
    ($x:expr) => {
        $x + $x
    }
}
let r = double!(5)     // 10
```

### Concurrency

```rust
// Rust
use std::sync::mpsc;
let (tx, rx) = mpsc::channel();
std::thread::spawn(move || { tx.send(42).unwrap(); });
let val = rx.recv().unwrap();
```

```hexa
// Hexa
let pair = channel()
let tx = pair[0]
let rx = pair[1]
spawn { tx.send(42) }
let val = rx.recv()
```

---

## 5. Go to Hexa

### Variables

```go
// Go
x := 42
var name string = "hello"
const Pi = 3.14159
```

```hexa
// Hexa
let x = 42
let name: string = "hello"
const PI = 3.14159
```

### Functions

```go
// Go
func add(a, b int) int { return a + b }
func divide(a, b float64) (float64, error) {
    if b == 0 { return 0, errors.New("division by zero") }
    return a / b, nil
}
```

```hexa
// Hexa
fn add(a: int, b: int) -> int { return a + b }
fn divide(a: float, b: float) -> float {
    if b == 0.0 { throw "division by zero" }
    return a / b
}
// Call with error handling:
try {
    let result = divide(10.0, 0.0)
} catch e {
    println("error:", e)
}
```

### Structs and Methods

```go
// Go
type Point struct { X, Y float64 }
func (p Point) Distance() float64 {
    return math.Sqrt(p.X*p.X + p.Y*p.Y)
}
p := Point{X: 3, Y: 4}
fmt.Println(p.Distance())
```

```hexa
// Hexa
struct Point { x: float, y: float }
impl Point {
    fn distance(self) -> float {
        return sqrt(self.x * self.x + self.y * self.y)
    }
}
let p = Point { x: 3.0, y: 4.0 }
println(p.distance())
```

### Interfaces to Traits

```go
// Go
type Stringer interface {
    String() string
}
func (p Point) String() string {
    return fmt.Sprintf("(%f, %f)", p.X, p.Y)
}
```

```hexa
// Hexa
trait Stringer {
    fn to_string(self) -> string
}
impl Stringer for Point {
    fn to_string(self) -> string {
        return format("({}, {})", self.x, self.y)
    }
}
```

### Goroutines/Channels

```go
// Go
ch := make(chan int)
go func() { ch <- 42 }()
val := <-ch
```

```hexa
// Hexa
let pair = channel()
let tx = pair[0]
let rx = pair[1]
spawn { tx.send(42) }
let val = rx.recv()
```

### Slices/Arrays

```go
// Go
nums := []int{1, 2, 3, 4, 5}
nums = append(nums, 6)
sub := nums[1:3]
```

```hexa
// Hexa
let nums = [1, 2, 3, 4, 5]
nums = nums.push(6)
let sub = nums.slice(1, 3)
```

### Error Handling

```go
// Go
result, err := doSomething()
if err != nil {
    log.Fatal(err)
}
```

```hexa
// Hexa (two styles)

// Style 1: try/catch
try {
    let result = do_something()
} catch e {
    println("fatal:", e)
    exit(1)
}

// Style 2: Result type
let result = do_something()    // returns Ok(val) or Err(msg)
if result.is_err() {
    println("fatal:", result)
    exit(1)
}
let val = result.unwrap()
```

### For Loops

```go
// Go
for i := 0; i < 10; i++ { fmt.Println(i) }
for _, v := range items { fmt.Println(v) }
for { /* infinite */ }
```

```hexa
// Hexa
for i in 0..10 { println(i) }
for v in items { println(v) }
loop { /* infinite */ }
```

---

## 6. Common Patterns

### Loops

```hexa
// Range-based for
for i in 0..10 { println(i) }       // 0 to 9
for i in 0..=10 { println(i) }      // 0 to 10

// Array iteration
for item in [1, 2, 3] { println(item) }

// While
let i = 0
while i < 10 {
    println(i)
    i = i + 1
}

// Infinite loop
loop {
    if should_stop { return }
}
```

### Error Handling

```hexa
// try/catch (catches runtime errors and thrown values)
try {
    let val = risky()
} catch e {
    println("caught:", e)
}

// throw (throws an error value)
throw "something went wrong"

// Result type (functional style)
let r = Ok(42)
if r.is_ok() { println(r.unwrap()) }

let e = Err("fail")
println(e.unwrap_or(0))   // 0

// Option type
let x = Some(42)
let y = None
println(x.unwrap())        // 42
println(y.unwrap_or(0))    // 0
println(x.is_some())       // true
println(y.is_none())       // true
```

### Closures and Higher-Order Functions

```hexa
// Lambda syntax
let double = |x| x * 2

// Passing functions as arguments
fn apply(f, x: int) -> int {
    return f(x)
}
apply(double, 6)   // 12

// Closure captures
let factor = 10
let scale = |x| x * factor
scale(5)   // 50

// Method chaining with lambdas
[1, 2, 3, 4, 5, 6]
    .filter(|x| x % 2 == 0)
    .map(|x| x * x)
    // result: [4, 16, 36]
```

### Concurrency (spawn + channel)

```hexa
let pair = channel()
let tx = pair[0]
let rx = pair[1]

spawn {
    tx.send(10)
    tx.send(20)
}

let a = rx.recv()   // 10
let b = rx.recv()   // 20

// try_recv returns Option (non-blocking)
let maybe = rx.try_recv()
if maybe.is_some() {
    println(maybe.unwrap())
}
```

### Structs

```hexa
struct Point {
    x: float,
    y: float
}

let p = Point { x: 3.0, y: 4.0 }
println(p.x, p.y)

// Methods via impl
impl Point {
    fn magnitude(self) -> float {
        return sqrt(self.x * self.x + self.y * self.y)
    }
}
println(p.magnitude())

// derive macros
derive(Display) for Point    // adds .display() method
derive(Debug) for Point      // adds .debug() method
derive(Eq) for Point         // adds .eq(other) method
```

### Enums

```hexa
// Simple enum
enum Color { Red, Green, Blue }
let c = Color::Red

// Enum with data
enum Shape { Circle(float), Rect(float) }
let s = Shape::Circle(5.0)

// Pattern matching
match s {
    Shape::Circle(r) -> PI * r * r
    Shape::Rect(w) -> w * w
}
```

### Pattern Matching

```hexa
// Match on literals
match x {
    1 -> "one"
    2 -> "two"
    _ -> "other"       // wildcard
}

// Match on enum variants with destructuring
match opt {
    Some(v) -> v * 2
    None -> 0
}

// Match with guards
match val {
    Wrapper::Val(n) if n > 100 -> "big"
    Wrapper::Val(n) -> "small"
    _ -> "unknown"
}
```

### Traits

```hexa
trait Shape {
    fn area(self) -> float
    fn name(self) -> string
}

struct Circle { radius: float }

impl Shape for Circle {
    fn area(self) -> float {
        return PI * self.radius * self.radius
    }
    fn name(self) -> string {
        return "circle"
    }
}

let c = Circle { radius: 5.0 }
println(c.name(), "area:", c.area())
```

### Modules

```hexa
// In mylib.hexa:
pub fn double(x: int) -> int { return x * 2 }
pub let MAGIC = 42

// In main.hexa:
use mylib
println(mylib::double(6))   // 12
println(mylib::MAGIC)       // 42

// Nested modules:
use utils::helper
helper::greet()
```

### Algebraic Effects

```hexa
// Define an effect
effect Ask {
    fn question(prompt: str) -> str
}

// Use the effect inside a handle block
let answer = handle {
    Ask.question("what is your name?")
} with {
    Ask.question(prompt) => {
        resume("Alice")    // inject a value back
    }
}
// answer == "Alice"
```

### Comptime (Compile-Time Execution)

```hexa
// Comptime block — evaluated at compile time
let table = comptime {
    let result = []
    for i in 0..10 {
        result = result.push(i * i)
    }
    result
}
// table is [0, 1, 4, 9, 16, 25, 36, 49, 64, 81]

// Comptime function
comptime fn factorial(n: int) -> int {
    if n <= 1 { return 1 }
    return n * factorial(n - 1)
}

const FACT_10 = comptime { factorial(10) }
```

### Macros

```hexa
// Simple macro
macro! hello {
    () => { "hello from macro" }
}
let msg = hello!()

// Macro with expression arguments
macro! double {
    ($x:expr) => { $x + $x }
}
double!(5)   // 10

// Macro with repetition
macro! sum {
    ($($x:expr),*) => {
        let acc = 0
        $(acc = acc + $x)*
        acc
    }
}
sum!(1, 2, 3, 4, 5)   // 15

// Invoke with brackets too
double![7]   // 14
```

---

## 7. Builtins Reference

### I/O

| Function               | Signature                       | Description                        |
|------------------------|--------------------------------|-------------------------------------|
| `print(...)`           | `(any...) -> void`             | Print without newline               |
| `println(...)`         | `(any...) -> void`             | Print with newline                  |
| `print_err(msg)`       | `(any) -> void`                | Print to stderr                     |

### Type Conversion

| Function               | Signature                       | Description                        |
|------------------------|--------------------------------|-------------------------------------|
| `to_string(val)`       | `(any) -> string`              | Convert to string                   |
| `to_int(val)`          | `(string\|float\|bool) -> int` | Convert to integer                  |
| `to_float(val)`        | `(string\|int\|bool) -> float` | Convert to float                    |
| `type_of(val)`         | `(any) -> string`              | Returns type name as string         |

### Math

| Function               | Signature                       | Description                        |
|------------------------|--------------------------------|-------------------------------------|
| `abs(x)`               | `(int\|float) -> int\|float`   | Absolute value                      |
| `min(a, b)`            | `(num, num) -> num`            | Minimum of two values               |
| `max(a, b)`            | `(num, num) -> num`            | Maximum of two values               |
| `floor(x)`             | `(float) -> int`               | Floor                               |
| `ceil(x)`              | `(float) -> int`               | Ceiling                             |
| `round(x)`             | `(float) -> int`               | Round to nearest integer            |
| `sqrt(x)`              | `(num) -> float`               | Square root                         |
| `pow(base, exp)`       | `(num, num) -> num`            | Power                               |
| `log(x)`               | `(num) -> float`               | Natural log (ln)                    |
| `log2(x)`              | `(num) -> float`               | Base-2 logarithm                    |
| `sin(x)`               | `(num) -> float`               | Sine (radians)                      |
| `cos(x)`               | `(num) -> float`               | Cosine (radians)                    |
| `tan(x)`               | `(num) -> float`               | Tangent (radians)                   |
| `gcd(a, b)`            | `(int, int) -> int`            | Greatest common divisor             |

### Math Constants

| Name  | Value         |
|-------|--------------|
| `PI`  | 3.14159...   |
| `E`   | 2.71828...   |

### String Formatting

| Function                     | Signature                         | Description                   |
|------------------------------|----------------------------------|-------------------------------|
| `format(template, args...)`  | `(string, any...) -> string`     | Format with `{}` placeholders |

### Collections

| Function               | Signature                       | Description                        |
|------------------------|--------------------------------|-------------------------------------|
| `len(val)`             | `(string\|array\|map) -> int`  | Length                              |
| `keys(map)`            | `(map) -> array`               | Get map keys                        |
| `values(map)`          | `(map) -> array`               | Get map values                      |
| `has_key(map, key)`    | `(map, string) -> bool`        | Check if map contains key           |

### Option/Result Constructors

| Function               | Signature                       | Description                        |
|------------------------|--------------------------------|-------------------------------------|
| `Some(val)`            | `(any) -> Option`              | Wrap in Some variant                |
| `None`                 | (value)                        | The None variant                    |
| `Ok(val)`              | `(any) -> Result`              | Wrap in Ok variant                  |
| `Err(val)`             | `(any) -> Result`              | Wrap in Err variant                 |

### File I/O

| Function                       | Signature                        | Description                  |
|--------------------------------|---------------------------------|-------------------------------|
| `read_file(path)`              | `(string) -> string\|error`     | Read file contents            |
| `write_file(path, content)`    | `(string, string) -> void`      | Write string to file          |
| `append_file(path, content)`   | `(string, string) -> void`      | Append string to file         |
| `file_exists(path)`            | `(string) -> bool`              | Check if file exists          |
| `delete_file(path)`            | `(string) -> bool`              | Delete a file                 |

### OS/System

| Function               | Signature                       | Description                        |
|------------------------|--------------------------------|-------------------------------------|
| `args()`               | `() -> array`                  | Command-line arguments              |
| `env_var(name)`        | `(string) -> string\|void`     | Get environment variable            |
| `exit(code?)`          | `(int?) -> never`              | Exit process                        |
| `clock()`              | `() -> float`                  | Current unix timestamp (seconds)    |
| `sleep(ms)`            | `(int) -> void`                | Sleep for milliseconds              |
| `exec(cmd)`            | `(string) -> string`           | Execute shell command, return stdout|
| `exec_with_status(cmd)`| `(string) -> Map`              | Execute command, return #{stdout, stderr, status}|

### User Input

| Function               | Signature                       | Description                        |
|------------------------|--------------------------------|-------------------------------------|
| `input(prompt?)`       | `(string?) -> string`          | Read line from stdin                |
| `readline(prompt?)`    | `(string?) -> string`          | Alias for input                     |

### Random

| Function               | Signature                       | Description                        |
|------------------------|--------------------------------|-------------------------------------|
| `random()`             | `() -> float`                  | Random float 0..1                   |
| `random_int(min, max)` | `(int, int) -> int`            | Random integer in range             |

### JSON

| Function               | Signature                       | Description                        |
|------------------------|--------------------------------|-------------------------------------|
| `json_parse(str)`      | `(string) -> any`              | Parse JSON string to value          |
| `json_stringify(val)`  | `(any) -> string`              | Convert value to JSON string        |

### Char Utilities

| Function               | Signature                       | Description                        |
|------------------------|--------------------------------|-------------------------------------|
| `is_alpha(c)`          | `(char) -> bool`               | Is alphabetic                       |
| `is_digit(c)`          | `(char) -> bool`               | Is ASCII digit                      |
| `is_alphanumeric(c)`   | `(char) -> bool`               | Is alphanumeric                     |
| `is_whitespace(c)`     | `(char) -> bool`               | Is whitespace                       |

### Concurrency

| Function               | Signature                       | Description                        |
|------------------------|--------------------------------|-------------------------------------|
| `channel()`            | `() -> (sender, receiver)`     | Create sender/receiver pair         |

### Number Theory (n=6)

| Function               | Signature                       | Description                        |
|------------------------|--------------------------------|-------------------------------------|
| `sigma(n)`             | `(int) -> int`                 | Sum of divisors                     |
| `phi(n)`               | `(int) -> int`                 | Euler's totient                     |
| `tau(n)`               | `(int) -> int`                 | Number of divisors                  |
| `sopfr(n)`             | `(int) -> int`                 | Sum of prime factors with repeats   |

### String Methods (called on string values)

| Method                         | Description                                |
|--------------------------------|-------------------------------------------|
| `.len()`                       | String length                              |
| `.contains(sub)`               | Check substring                            |
| `.split(delim)`                | Split into array                           |
| `.trim()`                      | Trim whitespace                            |
| `.to_upper()`                  | Uppercase                                  |
| `.to_lower()`                  | Lowercase                                  |
| `.replace(old, new)`           | Replace occurrences                        |
| `.starts_with(prefix)`         | Check prefix                               |
| `.ends_with(suffix)`           | Check suffix                               |
| `.chars()`                     | Array of chars                             |
| `.join(array)`                 | Join array with this string as separator   |
| `.repeat(n)`                   | Repeat string n times                      |
| `.is_empty()`                  | Check if empty                             |
| `.index_of(sub)`               | First index of substring (-1 if not found) |
| `.substring(start, end)`       | Substring by char indices                  |
| `.pad_left(width, char?)`      | Left-pad to width                          |
| `.pad_right(width, char?)`     | Right-pad to width                         |
| `.parse_int()`                 | Parse string as int                        |
| `.parse_float()`               | Parse string as float                      |

### Array Methods (called on array values)

| Method                         | Description                                |
|--------------------------------|-------------------------------------------|
| `.len()`                       | Array length                               |
| `.push(val)`                   | Returns new array with val appended        |
| `.contains(val)`               | Check if array contains value              |
| `.reverse()`                   | Returns reversed array                     |
| `.sort()`                      | Returns sorted array                       |
| `.map(fn)`                     | Map function over elements                 |
| `.filter(fn)`                  | Filter elements by predicate               |
| `.fold(init, fn)`              | Reduce/fold with accumulator               |
| `.enumerate()`                 | Array of (index, value) tuples             |
| `.sum()`                       | Sum of numeric array                       |
| `.min()`                       | Minimum value                              |
| `.max()`                       | Maximum value                              |
| `.join(sep)`                   | Join elements with separator string        |
| `.slice(start, end)`           | Sub-array by indices                       |
| `.flatten()`                   | Flatten nested arrays one level            |

### Map Methods (called on map values)

| Method         | Description               |
|---------------|---------------------------|
| `.len()`       | Number of entries          |
| `.keys()`      | Array of key strings       |
| `.values()`    | Array of values            |

### Sender Methods

| Method           | Description                     |
|-----------------|----------------------------------|
| `.send(val)`     | Send value through channel       |

### Receiver Methods

| Method           | Description                           |
|-----------------|----------------------------------------|
| `.recv()`        | Receive value (blocking)               |
| `.try_recv()`    | Try to receive (returns Option)        |

### Option Methods

| Method                | Description                           |
|----------------------|----------------------------------------|
| `.is_some()`          | Returns true if Some                   |
| `.is_none()`          | Returns true if None                   |
| `.unwrap()`           | Extracts value (panics on None)        |
| `.unwrap_or(default)` | Extracts value or returns default      |

### Result Methods

| Method                | Description                           |
|----------------------|----------------------------------------|
| `.is_ok()`            | Returns true if Ok                     |
| `.is_err()`           | Returns true if Err                    |
| `.unwrap()`           | Extracts value (panics on Err)         |
| `.unwrap_or(default)` | Extracts value or returns default      |

---

## 8. Gotchas and Differences

### No semicolons
Hexa uses newlines as statement terminators. Never use `;`.

### No `+=`, `-=`, `*=` operators
```hexa
// WRONG: x += 1
x = x + 1    // correct
```

### `let` is reassignable
Unlike Rust, `let` bindings can be reassigned. Use `const` for immutability.
```hexa
let x = 1
x = 2       // OK
const Y = 1
Y = 2       // ERROR: cannot reassign to constant
```

### `push` returns a new array
Arrays are immutable values. `push` does not mutate in place.
```hexa
let arr = [1, 2, 3]
arr.push(4)           // returns [1, 2, 3, 4] but arr is still [1, 2, 3]
arr = arr.push(4)     // correct way to "append"
```

### Match uses `->` not `=>`
```hexa
// WRONG: match x { 1 => "one" }
match x { 1 -> "one" }    // correct
```
Note: `=>` is used only in effect handlers within `handle...with` blocks.

### Enum variants carry 0 or 1 values
```hexa
// WRONG: enum Pair { Two(int, int) }
enum Wrapper { Val(int) }    // OK: 0 or 1 data field
```

### No implicit return
Functions require explicit `return` statements. The last expression in a function body is NOT automatically returned (unlike Rust).
```hexa
fn add(a: int, b: int) -> int {
    return a + b       // explicit return required
}
```

### String concatenation with `+`
```hexa
let msg = "hello" + " " + "world"   // string concatenation
```

### Map literals use `#{ }` syntax
```hexa
let m = #{ "name": "Alice", "age": 30 }
let val = m["name"]      // index with string key
```

### Range is exclusive by default
```hexa
for i in 0..5 { }     // 0, 1, 2, 3, 4
for i in 0..=5 { }    // 0, 1, 2, 3, 4, 5
```

### `proof` blocks only run in test mode
```hexa
proof my_test {
    assert 1 + 1 == 2
}
// Only executes with: ./hexa --test myfile.hexa
```

### No string interpolation
```hexa
// WRONG: "hello ${name}"
format("hello {}", name)               // use format()
"hello " + name                         // or string concatenation
```

### No `break` or `continue`
Use `return` from within a function, or restructure logic with conditionals.

### Type annotations are optional
```hexa
fn add(a: int, b: int) -> int { ... }   // annotated
let x = 42                               // inferred as int
let y: float = 3.14                      // explicitly annotated
```

### Logical operators
```hexa
// Use && and || (not 'and'/'or')
if x > 0 && x < 100 { ... }
if a || b { ... }
// Negation: !
if !done { ... }
```

### Struct field init requires explicit key-value
```hexa
// WRONG (no shorthand): Point { x, y }
Point { x: x, y: y }    // correct
```
