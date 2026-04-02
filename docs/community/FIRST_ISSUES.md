# Good First Issues

Six tasks for new contributors -- one per paradigm (n=6).

Each issue is self-contained, well-scoped, and teaches you a different part of the HEXA codebase. Pick the one that matches your interest.

---

## 1. Imperative: Add `break` and `continue` to Loops

**Paradigm**: Imperative
**Difficulty**: Easy
**File**: `src/interpreter.rs`
**n=6 connection**: Control flow group has 6 keywords (n=6). `break` and `continue` are common loop control additions.

**What to do:**
- `break` exits the innermost loop immediately
- `continue` skips to the next iteration
- These are already recognized as tokens -- they need interpreter support

**Steps:**
1. Look at how `for` and `while` loops are evaluated in `interpreter.rs`
2. Add `Stmt::Break` and `Stmt::Continue` handling (use a special return value or error variant)
3. Add tests in the `#[cfg(test)]` module
4. Add an example: `examples/break_continue.hexa`

**Test your work:**
```hexa
for i in 1..10 {
    if i == 6 { break }    // stop at the perfect number
    println(i)
}
// Should print: 1 2 3 4 5
```

---

## 2. Functional: Implement `zip` for Arrays

**Paradigm**: Functional
**Difficulty**: Easy
**File**: `src/interpreter.rs` (array method dispatch)
**n=6 connection**: Array methods are part of the functional paradigm toolkit.

**What to do:**
- `[1,2,3].zip([4,5,6])` should return `[[1,4], [2,5], [3,6]]`
- Truncate to the shorter array if lengths differ

**Steps:**
1. Find where array methods (`map`, `filter`, `fold`) are dispatched in `interpreter.rs`
2. Add a `"zip"` case that takes another array as argument
3. Return an array of pairs (2-element arrays)
4. Add tests

**Test your work:**
```hexa
let a = [1, 2, 3]
let b = [4, 5, 6]
let zipped = a.zip(b)
println(zipped)          // [[1, 4], [2, 5], [3, 6]]
assert len(zipped) == 3
```

---

## 3. OOP: Add a `to_string` Method for Enums

**Paradigm**: Object-oriented
**Difficulty**: Medium
**File**: `src/interpreter.rs` (enum handling)
**n=6 connection**: Display is one of the derive macros, tied to the type system (tau(6) = 4 layers).

**What to do:**
- Enum values should have a `.to_string()` method that returns the variant name
- `derive(Display)` should work for enums, not just structs

**Steps:**
1. Find how struct `derive(Display)` is implemented
2. Extend it to handle enum variants
3. For variants with data, format as `Variant(data)`
4. Add tests

**Test your work:**
```hexa
enum Color { Red, Green, Blue }
let c = Color::Red
println(c.to_string())   // "Red"
```

---

## 4. Concurrent: Add `channel` Buffering

**Paradigm**: Concurrent
**Difficulty**: Medium
**File**: `src/interpreter.rs` (channel implementation)
**n=6 connection**: Concurrency group has 4 keywords (tau(6) = 4). Buffered channels are a natural extension.

**What to do:**
- `channel()` creates an unbuffered channel (current behavior)
- `channel(6)` should create a buffered channel with capacity 6
- `send()` on a full buffered channel should block (or return an error)

**Steps:**
1. Find the channel implementation in `interpreter.rs`
2. Add an optional capacity parameter
3. Use a bounded queue instead of an unbounded one when capacity is specified
4. Add tests with multiple senders

**Test your work:**
```hexa
let pair = channel(6)    // buffer of 6 (the perfect number)
let tx = pair[0]
let rx = pair[1]

// Send 6 messages without blocking
for i in 1..=6 {
    tx.send(i)
}

// Receive all 6
for i in 1..=6 {
    println(rx.recv())
}
```

---

## 5. Logic/Proof: Add `invariant` Block Support

**Paradigm**: Logic/Proof
**Difficulty**: Medium
**File**: `src/proof_engine.rs`, `src/interpreter.rs`
**n=6 connection**: Proof group has 4 keywords (tau(6) = 4): proof, assert, invariant, theorem. `invariant` is recognized but not yet fully implemented.

**What to do:**
- An `invariant` block declares a condition that must hold before and after a code block
- It runs automatically when entering and exiting the associated scope

**Steps:**
1. Look at how `proof` blocks work in `proof_engine.rs`
2. Add `invariant` as a new block type that checks its assertions twice (pre and post)
3. Wire it into the interpreter
4. Add tests

**Test your work:**
```hexa
let mut balance = 100

invariant "balance_non_negative" {
    assert balance >= 0
}

balance = balance - 50   // ok
println(balance)         // 50
// invariant re-checks: balance >= 0? yes
```

---

## 6. AI-Native: Improve `generate` Error Messages

**Paradigm**: AI-native
**Difficulty**: Easy
**File**: `src/llm.rs`
**n=6 connection**: AI group has 4 keywords (tau(6) = 4). Better error messages improve the LLM fallback experience.

**What to do:**
- When `generate` fails (no LLM available, network error, invalid output), the error message should be helpful
- Include: what was attempted, why it failed, and what the user can do

**Steps:**
1. Read `src/llm.rs` to understand the LLM integration
2. Identify error paths (no API key, network timeout, invalid response)
3. Add structured error messages using the same format as `src/error.rs`
4. Include "did you mean?" suggestions when appropriate

**Test your work:**
```bash
# Without an LLM configured:
$ hexa -e 'generate "a fibonacci function"'
Error: LLM backend not configured
  |
  = help: set HEXA_LLM_API_KEY environment variable
  = help: or install a local model with `hexa config --llm local`
  = note: generate requires an LLM backend (see docs/book/ch06-advanced.md)
```

---

## How to Claim an Issue

1. Comment on the corresponding GitHub Issue saying you want to work on it
2. Fork the repo, create a branch: `git checkout -b first-issue/N`
3. Implement the feature with tests
4. Open a PR referencing the issue number

If you get stuck, ask in the issue thread. We are happy to help.
