# Chapter 5: The Standard Library

**Time: 18 minutes.** All 12 standard library modules -- sigma(6) = 12.

## Why 12 Modules?

The standard library has exactly sigma(6) = 12 modules. Each module is a first-class part of the language, accessible via `use` or directly through built-in functions.

| # | Module | Description | Key Functions |
|---|--------|-------------|---------------|
| 1 | `std.math` | Mathematics + number theory | sigma, phi, tau, sqrt, abs, pow |
| 2 | `std.io` | Input/output + printing | println, print, input, format |
| 3 | `std.fs` | File system | read_file, write_file, file_exists |
| 4 | `std.collections` | Data structures | HashMap, Array methods, sorting |
| 5 | `std.net` | Networking | http_get, http_post, url_parse |
| 6 | `std.time` | Time + scheduling | now, sleep, duration, timer |
| 7 | `std.crypto` | Cryptography | sha256, hmac, random_bytes |
| 8 | `std.encoding` | Serialization | json_parse, json_stringify, base64 |
| 9 | `std.testing` | Test framework | assert, expect, bench |
| 10 | `std.log` | Logging | debug, info, warn, error |
| 11 | `std.consciousness` | Psi-builtins + ANIMA bridge | psi_*, consciousness_*, hexad_* |
| 12 | `std.concurrency` | Parallelism | spawn, channel, atomic, select |

## Module 1: std.math

Built-in mathematical functions. The number theory functions (`sigma`, `phi`, `tau`, `sopfr`, `gcd`) are available globally without import.

```hexa
// Number theory (global, no import needed)
println(sigma(6))     // 12
println(phi(6))       // 2
println(tau(6))       // 4
println(sopfr(6))     // 5
println(gcd(12, 8))   // 4

// General math
println(sqrt(144.0))  // 12.0
println(abs(-6))      // 6
println(pow(4, 5))    // 1024
println(min(3, 7))    // 3
println(max(3, 7))    // 7

// Constants
println(pi())         // 3.14159...
println(e())          // 2.71828...
```

## Module 2: std.io

Input and output. `println` and `print` are globally available.

```hexa
// Output (global)
println("Hello")            // with newline
print("No newline")         // without newline
println("x =", 42)          // multiple arguments

// Input
let name = input("Name: ")
println("Hello,", name)

// Formatting
let msg = format("n={}, sigma={}", 6, 12)
println(msg)                // n=6, sigma=12
```

## Module 3: std.fs

File system operations.

```hexa
// Write and read files
write_file("data.txt", "sigma(6) = 12")
let content = read_file("data.txt")
println(content)            // sigma(6) = 12

// Check existence
if file_exists("data.txt") {
    println("File found")
}
```

## Module 4: std.collections

Arrays, HashMaps, and higher-order operations.

```hexa
// Arrays
let nums = [1, 2, 3, 4, 5, 6]
println(len(nums))              // 6
println(nums.sum())             // 21
println(nums.reverse())         // [6, 5, 4, 3, 2, 1]

// Higher-order functions
let doubled = nums.map(|x| x * 2)         // [2, 4, 6, 8, 10, 12]
let evens = nums.filter(|x| x % 2 == 0)   // [2, 4, 6]
let total = nums.fold(0, |acc, x| acc + x) // 21

// Sorting
let unsorted = [3, 1, 4, 1, 5, 9]
println(unsorted.sort())        // [1, 1, 3, 4, 5, 9]

// Enumerate
for pair in nums.enumerate() {
    println("index:", pair[0], "value:", pair[1])
}

// Slice
println(nums.slice(1, 4))      // [2, 3, 4]

// Flatten
let nested = [[1, 2], [3, 4], [5, 6]]
println(nested.flatten())      // [1, 2, 3, 4, 5, 6]

// HashMaps
let config = {"name": "hexa", "version": "1.0"}
println(config["name"])        // hexa
println(config.keys())         // ["name", "version"]
println(config.values())       // ["hexa", "1.0"]
```

## Module 5: std.net

HTTP client and URL handling.

```hexa
use std.net

// HTTP requests
let response = http_get("https://api.example.com/data")
println(response.status)    // 200
println(response.body)      // response body

let result = http_post("https://api.example.com/submit", {
    "name": "hexa",
    "n": 6
})
```

## Module 6: std.time

Time operations and scheduling.

```hexa
use std.time

let start = now()
// ... computation ...
let elapsed = now() - start
println("Elapsed:", elapsed, "ms")

sleep(1000)    // sleep 1 second
```

## Module 7: std.crypto

Cryptographic primitives.

```hexa
use std.crypto

let hash = sha256("hexa-lang")
println("SHA-256:", hash)

let bytes = random_bytes(32)
println("Random:", len(bytes), "bytes")
```

## Module 8: std.encoding

Serialization and encoding.

```hexa
// JSON (global)
let data = {"n": 6, "sigma": 12, "phi": 2}
let json_str = json_stringify(data)
println(json_str)           // {"n":6,"sigma":12,"phi":2}

let parsed = json_parse(json_str)
println(parsed["n"])        // 6
```

## Module 9: std.testing

Test framework used by proof and verify blocks.

```hexa
proof math_basics {
    assert 1 + 1 == 2
    assert sigma(6) == 12
    assert "hexa".to_upper() == "HEXA"
}

// Run all proofs as tests:
// $ hexa --test file.hexa
```

The `--test` flag discovers all `proof` and `verify` blocks and runs them as a test suite.

## Module 10: std.log

Structured logging with levels.

```hexa
use std.log

log_debug("sigma =", sigma(6))     // [DEBUG] sigma = 12
log_info("Starting engine")        // [INFO] Starting engine
log_warn("Low memory")             // [WARN] Low memory
log_error("Connection failed")     // [ERROR] Connection failed
```

## Module 11: std.consciousness

The consciousness DSL built-ins. These are globally available.

```hexa
// Psi-constants (from ln(2))
println(psi_coupling())              // 0.014
println(psi_balance())               // 0.5
println(psi_steps())                 // 4.33
println(psi_entropy())               // 0.998
println(psi_frustration())           // 0.10

// Architecture constants (from n=6)
println(consciousness_max_cells())   // 1024 = tau^sopfr
println(consciousness_decoder_dim()) // 384  = (tau+sigma)*J2
println(consciousness_phi())         // 71   = n*sigma - 1

// Hexad brain structure
println(hexad_modules())             // ["C","D","S","M","W","E"]
println(hexad_right())               // ["C","S","W"]
println(hexad_left())                // ["D","M","E"]
```

## Module 12: std.concurrency

Parallel execution with spawn/channel.

```hexa
// Channels
let pair = channel()
let tx = pair[0]
let rx = pair[1]

// Spawn a task
spawn {
    tx.send(sigma(6))     // send 12
    tx.send(tau(6))       // send 4
}

let a = rx.recv()         // 12
let b = rx.recv()         // 4
println("Received:", a, b)
println("Product:", a * b) // 48 = sigma*tau (keyword base)
```

## Strings

Strings have rich built-in methods:

```hexa
let s = "hello world"
println(s.split(" "))        // ["hello", "world"]
println(s.to_upper())        // "HELLO WORLD"
println(s.contains("world")) // true
println(s.replace("world", "hexa"))  // "hello hexa"
println("hexa".chars())      // ["h", "e", "x", "a"]
println(", ".join(["a", "b", "c"]))  // "a, b, c"
println("ha".repeat(3))      // "hahaha"
println("  hexa  ".trim())   // "hexa"
```

## Result and Option Types

```hexa
// Option
let x: Option<int> = Some(42)
let y: Option<int> = None

println(x.unwrap())      // 42
println(x.is_some())     // true
println(y.is_some())     // false

// Result
let ok: Result<int, string> = Ok(6)
let err: Result<int, string> = Err("not found")

println(ok.unwrap())     // 6
println(ok.is_ok())      // true
println(err.is_ok())     // false
```

## Summary

12 modules for the 12 divisor-sum of 6. Each module is focused, composable, and accessible either through `use` imports or as globally available built-ins.

**Next: [Chapter 6 -- Advanced HEXA](ch06-advanced.md)**
