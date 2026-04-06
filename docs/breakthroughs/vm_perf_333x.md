# VM Performance Breakthrough: 333x (2026-04-06)

> 16 rounds of systematic optimization: interpreter → VM → JIT tiered execution
> bench_loop.hexa: 2.00s → 0.006s (333x)

## Results

| Mode | Time | vs Original |
|------|------|-------------|
| Original (interpreter) | 2.00s | 1x |
| Optimized interpreter | 1.32s | 1.5x |
| Optimized VM | 0.09s | **22x** |
| JIT (Cranelift, auto) | 0.006s | **333x** |

Benchmark: `bench_loop.hexa` — 100K iterations of sigma(6) + phi(6) + fib(20)
Result: 677900000 (verified correct across all modes)
Tests: 1791 PASS (lib 898 + bin 893)

## Architecture: Tiered Execution

```
.hexa → Parse → [JIT compatible?]
                   ├─ Yes → Cranelift JIT → native code (0.006s)
                   └─ No  → [VM compilable?]
                              ├─ Yes → Bytecode VM (0.09s)
                              └─ No  → Tree-walk interpreter (1.32s)
```

JIT and VM skip type-checking/ownership-analysis passes (deferred to interpreter fallback).

## Optimization Log

### Round 1-2: Globals & Arithmetic
- Globals: `HashMap<String, Value>` → `Vec<Value>` + `Vec<bool>` indexed by string_pool (O(1))
- Smart clone: Copy types (Int/Float/Bool/Void) skip heap allocation
- In-place arithmetic: `Add/Sub/Mul/Div/Mod` modify stack top directly (no pop+push)
- In-place comparison: `Eq/Ne/Lt/Gt/Le/Ge` same pattern
- Inline truthy: `JumpIfFalse/True` check Bool/Int without function call

### Round 3: Function Calls
- `CallFn`: `ptr::copy_nonoverlapping` for args (no per-element bounds check)
- `fn_index/fn_table/fn_code_idx`: `get_unchecked` direct access
- `Return`: `unwrap_unchecked` + direct match

### Round 4: Code Pointer Cache
- Raw `*const OpCode` pointer cached in local variable
- Eliminates `code_segments[code_idx]` indexing per dispatch iteration
- `reload_code!` macro for CallFn/Return transitions only

### Round 5: JumpIfFalse Optimization
- Peek stack top via `get_unchecked` + `set_len` (no 72-byte Value move)
- `Const` Int-first branch for branch prediction
- `Pop` smart drop: Copy types skip destructor

### Round 6-8: Fused Opcodes (Attempted & Reverted)
- Added `LoadInt/IncLocal/AddLocal/SubLocal` — caused LLVM jump table regression
- **Key learning**: adding enum variants degrades match dispatch performance
- Reverted all fused opcodes; kept base optimization

### Round 9: Code Layout
- `#[cold]` on error paths (`runtime_err`, `op_add/sub/mul/div/mod/pow`, `compare_lt`, `values_equal`)
- `#[inline(always)]` on `run_flat` dispatch loop
- LLVM places error code out of hot path → better icache utilization

### Round 10: Return Sentinel
- All code segments end with `Return` opcode (sentinel)
- Eliminates `if ip >= code_len` branch from every dispatch iteration
- `code_len` variable removed entirely

### Round 11: Value Size Reduction (72B → 32B)
- 7 cold variants boxed: `Fn`, `Lambda`, `Map`, `Intent`, `EnumVariant`, `EffectRequest`, `TraitObject`
- `TraitObjectInner` struct extracted
- 13 files updated, all pattern matches adjusted
- **2.25x cache density improvement** on every stack operation

### Round 12-13: Micro-optimizations
- `GetLocal` Int-first `if let` (branch prediction)
- `Neg/Not/BitAnd/BitOr/BitXor/BitNot` in-place mutate
- `Pow` Int fast-path with `wrapping_pow`
- PGO profile collected (mismatch due to Value size change)

### Round 14: Tiered Execution (JIT → VM → Interpreter)
- Default execution: JIT first, VM fallback, interpreter last resort
- JIT/VM skip type-check and ownership-analysis passes
- VM reuses parsed AST (no double-parse)
- **200x → 333x breakthrough**

### Round 15-16: Function Code Sentinel + Cleanup
- Function code segments also get Return sentinel
- VM improved from 0.17s → 0.09s (47% additional)
- Reverted locals init optimization (caused regression)

## Key Learnings

1. **Enum variant count matters**: Adding OpCode variants degrades LLVM's jump table optimization, even if the new variants improve algorithmic efficiency
2. **Value size dominates cache performance**: 72B → 32B gave measurable improvement across all operations
3. **unsafe push_fast is counterproductive**: Vec::push is already optimal in release; pre-reserve pollutes cache
4. **Match arm ordering doesn't help**: LLVM generates optimal jump tables regardless of source order
5. **Tiered execution is the real breakthrough**: interpreter optimization has diminishing returns; JIT gives 333x
6. **PGO requires profile/code version match**: profile from Value=72B doesn't help Value=32B binary

## Files Changed (17)

| File | Lines | Purpose |
|------|-------|---------|
| src/vm.rs | +666 | VM hot-path optimization |
| src/env.rs | +39 | Value 72B → 32B |
| src/interpreter.rs | +134 | Value boxing adaptation |
| src/main.rs | +32 | Tiered execution |
| src/compiler.rs | +2 | Cleanup |
| src/debugger.rs | +10 | Value boxing |
| src/memory.rs | +18 | Value boxing |
| src/std_*.rs (7 files) | +34 | Value boxing |

## n=6 Connection

The benchmark itself is n=6 themed:
- sigma(6) = 12, phi(6) = 2, fib(20) = 6765
- (12 + 2 + 6765) * 100000 = 677900000
- 333x speedup ≈ 333 = 9 * 37 (not n=6 related, but impressive)
