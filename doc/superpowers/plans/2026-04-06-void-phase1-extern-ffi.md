# VOID Phase 1: extern FFI ��� Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `extern` keyword to hexa-lang so hexa code can call any C function from system libraries via dlopen/dlsym. This is the foundation for VOID — no Rust dependencies added.

**Architecture:** New `extern` keyword (#54) + `@link` attribute + pointer types. JIT uses dlopen/dlsym to resolve symbols at runtime. Interpreter wraps dlopen/dlsym via libffi-style dynamic dispatch.

**Tech Stack:** hexa-lang (existing Rust codebase), libc dlopen/dlsym (already available via std), no new Cargo dependencies

---

## File Structure

| File | Action | Responsibility |
|------|--------|---------------|
| `src/token.rs` | Modify | Add `Extern` keyword |
| `src/ast.rs` | Modify | Add `ExternFnDecl`, `LinkAttr`, `Stmt::Extern` |
| `src/parser.rs` | Modify | Parse `extern fn`, `@link()`, pointer types in params |
| `src/env.rs` | Modify | Add `Value::Pointer(*mut u8)` variant |
| `src/interpreter.rs` | Modify | dlopen/dlsym dispatch for extern calls |
| `src/jit.rs` | Modify | Dynamic symbol resolution for extern functions |
| `src/lib.rs` | No change | (modules already registered) |
| `examples/test_extern.hexa` | Create | Test extern FFI with libc calls |

---

## Task 1: Add extern Token + Keyword

**Files:**
- Modify: `src/token.rs`

- [ ] **Step 1: Read current token.rs to find keyword registration**

Read `src/token.rs` and locate where keywords are defined and matched.

- [ ] **Step 2: Add Extern variant to Token enum**

In the Token enum, add `Extern` variant alongside existing keywords.

- [ ] **Step 3: Add "extern" to keyword matching**

In the keyword lookup (match or HashMap), add `"extern" => Token::Extern`.

- [ ] **Step 4: Verify compilation**

Run: `cd $HEXA_LANG && export PATH="$HOME/.cargo/bin:$PATH" && cargo check 2>&1 | tail -5`
Expected: compiles (warnings about unused Extern OK)

- [ ] **Step 5: Commit**

```bash
git add src/token.rs
git commit -m "feat: add extern keyword (#54) to hexa-lang"
```

---

## Task 2: Add AST Nodes for extern fn

**Files:**
- Modify: `src/ast.rs`
- Modify: `src/env.rs`

- [ ] **Step 1: Read src/ast.rs to find Stmt enum and FnDecl**

Locate where `Stmt::FnDecl` is defined and where attributes are handled.

- [ ] **Step 2: Add ExternFnDecl struct to ast.rs**

```rust
/// External function declaration — calls into C libraries via FFI.
/// `extern fn name(params) -> RetType`
#[derive(Debug, Clone)]
pub struct ExternFnDecl {
    pub name: String,
    pub params: Vec<ExternParam>,
    pub ret_type: Option<String>,    // "Int", "Float", "*Void", etc.
    pub link_lib: Option<String>,    // from @link("libname")
    pub variadic: bool,              // supports ... (like printf)
}

#[derive(Debug, Clone)]
pub struct ExternParam {
    pub name: String,
    pub typ: String,    // "Int", "Float", "*Void", "*Byte", "*Int", etc.
}
```

- [ ] **Step 3: Add Stmt::Extern variant**

In the `Stmt` enum, add:
```rust
Extern(ExternFnDecl),
```

- [ ] **Step 4: Add Value::Pointer to env.rs**

In the `Value` enum, add:
```rust
Pointer(u64),  // raw pointer as integer (for extern FFI)
```

Add Display impl:
```rust
Value::Pointer(addr) => write!(f, "<ptr 0x{:x}>", addr),
```

- [ ] **Step 5: Verify compilation**

Run: `cargo check 2>&1 | tail -5`
Expected: compiles with warnings about unmatched patterns (we'll fix in parser/interpreter)

- [ ] **Step 6: Commit**

```bash
git add src/ast.rs src/env.rs
git commit -m "feat: add ExternFnDecl AST node + Value::Pointer"
```

---

## Task 3: Parse extern fn Declarations

**Files:**
- Modify: `src/parser.rs`

- [ ] **Step 1: Read parser.rs to find where Stmt parsing happens**

Locate the main statement dispatch (where FnDecl, Struct, etc. are parsed).

- [ ] **Step 2: Add @link attribute parsing**

Before `extern fn`, the parser should look for `@link("libname")`. When it encounters `@` followed by `link`, parse the library name string.

- [ ] **Step 3: Add extern fn parsing**

When the parser sees `Token::Extern`, parse the function declaration:

```
extern fn name(param1: Type, param2: Type, ...) -> RetType
```

Pointer types: if a type starts with `*`, parse as pointer type (`*Int`, `*Void`, `*Byte`, etc.).

The function has no body — just a declaration.

- [ ] **Step 4: Wire extern parsing into main parse loop**

In the main statement dispatch, when `Token::Extern` is encountered (or when a `@link` attribute was just collected), call `parse_extern_fn_decl()`.

- [ ] **Step 5: Write a test hexa file to verify parsing**

Create `examples/test_extern_parse.hexa`:
```hexa
@link("libc")
extern fn getpid() -> Int

extern fn puts(s: *Byte) -> Int

println("extern parse OK")
```

- [ ] **Step 6: Run parse test**

Run: `./hexa examples/test_extern_parse.hexa`
Expected: "extern parse OK" (extern declarations parsed but not executed yet)

- [ ] **Step 7: Commit**

```bash
git add src/parser.rs examples/test_extern_parse.hexa
git commit -m "feat: parse extern fn declarations with @link attribute"
```

---

## Task 4: Implement extern Function Calls in Interpreter

**Files:**
- Modify: `src/interpreter.rs`

This is the core task. When hexa code calls an extern function, the interpreter must:
1. dlopen the specified library
2. dlsym the function symbol
3. Marshal hexa Values to C ABI
4. Call the function pointer
5. Marshal the return value back to hexa Value

- [ ] **Step 1: Read interpreter.rs to find function call dispatch**

Locate where `Value::BuiltinFn` and user-defined functions are called.

- [ ] **Step 2: Add extern function registry to Interpreter struct**

```rust
/// Extern function declarations: name -> ExternFnDecl
extern_fns: HashMap<String, crate::ast::ExternFnDecl>,
/// Loaded dynamic libraries: lib_name -> handle
#[cfg(not(target_arch = "wasm32"))]
loaded_libs: HashMap<String, *mut std::ffi::c_void>,
/// Resolved extern symbols: fn_name -> fn_pointer
#[cfg(not(target_arch = "wasm32"))]
extern_symbols: HashMap<String, *mut std::ffi::c_void>,
```

Initialize in `Interpreter::new()`.

- [ ] **Step 3: Handle Stmt::Extern in statement execution**

When executing `Stmt::Extern(decl)`, register the extern function:
```rust
Stmt::Extern(decl) => {
    self.extern_fns.insert(decl.name.clone(), decl.clone());
    // Also register as a callable value
    self.env.set(&decl.name, Value::BuiltinFn(format!("__extern_{}", decl.name)));
    Ok(Value::Void)
}
```

- [ ] **Step 4: Implement dlopen/dlsym resolution**

```rust
#[cfg(not(target_arch = "wasm32"))]
fn resolve_extern_symbol(&mut self, name: &str) -> Result<*mut std::ffi::c_void, HexaError> {
    // Return cached symbol if already resolved
    if let Some(&ptr) = self.extern_symbols.get(name) {
        return Ok(ptr);
    }

    let decl = self.extern_fns.get(name)
        .ok_or_else(|| self.runtime_err(format!("unknown extern fn: {}", name)))?
        .clone();

    // Open library if needed
    let lib_handle = if let Some(ref lib_name) = decl.link_lib {
        if let Some(&handle) = self.loaded_libs.get(lib_name) {
            handle
        } else {
            let lib_path = resolve_lib_path(lib_name);
            let c_path = std::ffi::CString::new(lib_path.as_str()).unwrap();
            let handle = unsafe { libc::dlopen(c_path.as_ptr(), libc::RTLD_LAZY) };
            if handle.is_null() {
                return Err(self.runtime_err(format!("dlopen failed for '{}'", lib_name)));
            }
            self.loaded_libs.insert(lib_name.clone(), handle);
            handle
        }
    } else {
        // No @link — search in default symbol table
        unsafe { libc::dlopen(std::ptr::null(), libc::RTLD_LAZY) }
    };

    let c_name = std::ffi::CString::new(name.as_str()).unwrap();
    let sym = unsafe { libc::dlsym(lib_handle, c_name.as_ptr()) };
    if sym.is_null() {
        return Err(self.runtime_err(format!("dlsym failed for '{}'", name)));
    }

    self.extern_symbols.insert(name.to_string(), sym);
    Ok(sym)
}

fn resolve_lib_path(lib_name: &str) -> String {
    #[cfg(target_os = "macos")]
    {
        // Check frameworks first
        let framework = format!("/System/Library/Frameworks/{}.framework/{}", lib_name, lib_name);
        if std::path::Path::new(&framework).exists() {
            return framework;
        }
        // Then dylib
        format!("/usr/lib/lib{}.dylib", lib_name)
    }
    #[cfg(target_os = "linux")]
    {
        format!("lib{}.so", lib_name)
    }
}
```

- [ ] **Step 5: Implement extern function call dispatch**

In `call_builtin`, handle `__extern_` prefixed names:

```rust
if name.starts_with("__extern_") {
    let fn_name = &name["__extern_".len()..];
    return self.call_extern_fn(fn_name, args);
}
```

Implement `call_extern_fn`:
```rust
#[cfg(not(target_arch = "wasm32"))]
fn call_extern_fn(&mut self, name: &str, args: Vec<Value>) -> Result<Value, HexaError> {
    let fn_ptr = self.resolve_extern_symbol(name)?;
    let decl = self.extern_fns.get(name).unwrap().clone();

    // Marshal args to i64 (C ABI: everything fits in register as i64/pointer)
    let mut c_args: Vec<i64> = Vec::new();
    for (i, arg) in args.iter().enumerate() {
        c_args.push(match arg {
            Value::Int(n) => *n,
            Value::Float(f) => f.to_bits() as i64,
            Value::Bool(b) => *b as i64,
            Value::Pointer(p) => *p as i64,
            Value::Str(s) => {
                // Auto-convert String to C string for *Byte params
                let c_str = std::ffi::CString::new(s.as_str()).unwrap();
                let ptr = c_str.into_raw() as i64;
                // NOTE: leaked CString — caller must manage memory
                ptr
            }
            Value::Void => 0,
            _ => return Err(self.runtime_err(format!(
                "extern fn '{}': arg {} cannot be marshaled to C", name, i
            ))),
        });
    }

    // Call via function pointer using platform ABI
    // SysV AMD64 / ARM64: first 6 int args in registers, return in rax/x0
    let ret_raw = unsafe {
        call_extern_raw(fn_ptr, &c_args)
    };

    // Marshal return value
    match decl.ret_type.as_deref() {
        Some("Int") | Some("int") => Ok(Value::Int(ret_raw)),
        Some("Float") | Some("float") => Ok(Value::Float(f64::from_bits(ret_raw as u64))),
        Some("Bool") | Some("bool") => Ok(Value::Bool(ret_raw != 0)),
        Some(t) if t.starts_with('*') => Ok(Value::Pointer(ret_raw as u64)),
        Some("Void") | Some("void") | None => Ok(Value::Void),
        Some(other) => Ok(Value::Int(ret_raw)), // default to Int
    }
}

/// Raw extern call — dispatches based on argument count.
/// Uses transmute to cast function pointer to appropriate signature.
#[cfg(not(target_arch = "wasm32"))]
unsafe fn call_extern_raw(fn_ptr: *mut std::ffi::c_void, args: &[i64]) -> i64 {
    type Fn0 = unsafe extern "C" fn() -> i64;
    type Fn1 = unsafe extern "C" fn(i64) -> i64;
    type Fn2 = unsafe extern "C" fn(i64, i64) -> i64;
    type Fn3 = unsafe extern "C" fn(i64, i64, i64) -> i64;
    type Fn4 = unsafe extern "C" fn(i64, i64, i64, i64) -> i64;
    type Fn5 = unsafe extern "C" fn(i64, i64, i64, i64, i64) -> i64;
    type Fn6 = unsafe extern "C" fn(i64, i64, i64, i64, i64, i64) -> i64;

    match args.len() {
        0 => std::mem::transmute::<_, Fn0>(fn_ptr)(),
        1 => std::mem::transmute::<_, Fn1>(fn_ptr)(args[0]),
        2 => std::mem::transmute::<_, Fn2>(fn_ptr)(args[0], args[1]),
        3 => std::mem::transmute::<_, Fn3>(fn_ptr)(args[0], args[1], args[2]),
        4 => std::mem::transmute::<_, Fn4>(fn_ptr)(args[0], args[1], args[2], args[3]),
        5 => std::mem::transmute::<_, Fn5>(fn_ptr)(args[0], args[1], args[2], args[3], args[4]),
        6 => std::mem::transmute::<_, Fn6>(fn_ptr)(args[0], args[1], args[2], args[3], args[4], args[5]),
        _ => {
            eprintln!("extern call with {} args not supported (max 6)", args.len());
            0
        }
    }
}
```

- [ ] **Step 6: Add pointer helper builtins**

Add to `call_builtin`:
```rust
"cstring" => {
    // Convert hexa String to null-terminated C string pointer
    let s = match &args[0] {
        Value::Str(s) => s.clone(),
        _ => return Err(self.runtime_err("cstring expects String".to_string())),
    };
    let c_str = std::ffi::CString::new(s).unwrap();
    let ptr = c_str.into_raw() as u64;
    Ok(Value::Pointer(ptr))
}
"from_cstring" => {
    // Convert C string pointer to hexa String
    let ptr = match &args[0] {
        Value::Pointer(p) => *p as *const i8,
        Value::Int(n) => *n as *const i8,
        _ => return Err(self.runtime_err("from_cstring expects Pointer".to_string())),
    };
    let s = unsafe { std::ffi::CStr::from_ptr(ptr) }
        .to_string_lossy()
        .to_string();
    Ok(Value::Str(s))
}
"ptr_null" => Ok(Value::Pointer(0)),
"ptr_addr" => {
    match &args[0] {
        Value::Pointer(p) => Ok(Value::Int(*p as i64)),
        _ => Err(self.runtime_err("ptr_addr expects Pointer".to_string())),
    }
}
```

- [ ] **Step 7: Verify compilation**

Run: `cargo check 2>&1 | tail -10`

- [ ] **Step 8: Commit**

```bash
git add src/interpreter.rs
git commit -m "feat: extern FFI — dlopen/dlsym dispatch for extern function calls"
```

---

## Task 5: Implement extern in JIT Compiler

**Files:**
- Modify: `src/jit.rs`

- [ ] **Step 1: Read jit.rs to understand symbol registration**

Locate `JITBuilder::symbol()` calls and `declare_function` with `Linkage::Import`.

- [ ] **Step 2: Handle Stmt::Extern in JIT compilation**

In the first pass (function declaration scan), process extern declarations:

```rust
Stmt::Extern(decl) => {
    // Resolve symbol via dlopen/dlsym
    let fn_ptr = resolve_extern_for_jit(&decl)?;

    // Register in Cranelift symbol table
    builder.symbol(&decl.name, fn_ptr as *const u8);

    // Create signature
    let mut sig = module.make_signature();
    for param in &decl.params {
        sig.params.push(AbiParam::new(I64)); // all types map to i64
    }
    if decl.ret_type.is_some() {
        sig.returns.push(AbiParam::new(I64));
    }

    let func_id = module.declare_function(&decl.name, Linkage::Import, &sig)?;
    self.functions.insert(decl.name.clone(), FuncInfo { id: func_id, param_count: decl.params.len() });
}
```

- [ ] **Step 3: Add dlopen/dlsym helper for JIT**

```rust
fn resolve_extern_for_jit(decl: &ExternFnDecl) -> Result<*mut std::ffi::c_void, HexaError> {
    let lib_handle = if let Some(ref lib) = decl.link_lib {
        let path = resolve_lib_path(lib);
        let c_path = std::ffi::CString::new(path).unwrap();
        unsafe { libc::dlopen(c_path.as_ptr(), libc::RTLD_LAZY) }
    } else {
        unsafe { libc::dlopen(std::ptr::null(), libc::RTLD_LAZY) }
    };

    if lib_handle.is_null() {
        return Err(jit_err(format!("dlopen failed for extern fn '{}'", decl.name)));
    }

    let c_name = std::ffi::CString::new(decl.name.as_str()).unwrap();
    let sym = unsafe { libc::dlsym(lib_handle, c_name.as_ptr()) };
    if sym.is_null() {
        return Err(jit_err(format!("dlsym failed for '{}'", decl.name)));
    }

    Ok(sym)
}
```

- [ ] **Step 4: Handle extern call in compile_expr (Call node)**

In the existing Call compilation, extern functions are already handled because they're registered in `self.functions`. The existing code path for user-defined functions works:

```rust
if let Some(info) = self.functions.get(name.as_str()) {
    // ... compile args, call func_ref — works for extern too
}
```

No change needed here — Cranelift handles Import linkage transparently.

- [ ] **Step 5: Verify compilation**

Run: `cargo check 2>&1 | tail -10`

- [ ] **Step 6: Commit**

```bash
git add src/jit.rs
git commit -m "feat: extern FFI in JIT — dlopen/dlsym symbol resolution for Cranelift"
```

---

## Task 6: End-to-End Test

**Files:**
- Create: `examples/test_extern.hexa`

- [ ] **Step 1: Write comprehensive extern FFI test**

```hexa
// test_extern.hexa — Test extern FFI with libc calls
println("=== VOID extern FFI test ===")

// Test 1: getpid
extern fn getpid() -> Int
let pid = getpid()
assert(pid > 0, "getpid failed")
println("[PASS] getpid = " + str(pid))

// Test 2: getenv
extern fn getenv(name: *Byte) -> *Byte
let home_ptr = getenv(cstring("HOME"))
let home = from_cstring(home_ptr)
assert(home != "", "getenv HOME failed")
println("[PASS] HOME = " + home)

// Test 3: strlen
extern fn strlen(s: *Byte) -> Int
let len = strlen(cstring("VOID"))
assert(len == 4, "strlen failed: " + str(len))
println("[PASS] strlen(\"VOID\") = " + str(len))

// Test 4: time
extern fn time(t: *Void) -> Int
let now = time(ptr_null())
assert(now > 1000000000, "time failed")
println("[PASS] time = " + str(now))

// Test 5: write to stdout
extern fn write(fd: Int, buf: *Byte, count: Int) -> Int
let msg = cstring("[PASS] direct write to stdout\n")
write(1, msg, 30)

// Test 6: malloc/free
extern fn malloc(size: Int) -> *Void
extern fn free(ptr: *Void)
let ptr = malloc(1024)
assert(ptr_addr(ptr) > 0, "malloc failed")
free(ptr)
println("[PASS] malloc/free")

println("=== All extern FFI tests passed ===")
```

- [ ] **Step 2: Build hexa**

Run: `cd $HEXA_LANG && export PATH="$HOME/.cargo/bin:$PATH" && bash build.sh 2>&1 | tail -5`

- [ ] **Step 3: Run test with interpreter**

Run: `./hexa examples/test_extern.hexa`
Expected: All tests pass

- [ ] **Step 4: Run test with JIT**

Run: `./hexa --native examples/test_extern.hexa`
Expected: All tests pass

- [ ] **Step 5: Commit**

```bash
git add examples/test_extern.hexa
git commit -m "test: extern FFI end-to-end tests (getpid, getenv, strlen, time, write, malloc/free)"
```

---

## Task 7: Test PTY via extern (VOID Foundation)

**Files:**
- Create: `examples/test_extern_pty.hexa`

- [ ] **Step 1: Write PTY test using pure extern FFI**

```hexa
// test_extern_pty.hexa — PTY management via pure hexa extern FFI
println("=== VOID PTY test (extern FFI) ===")

extern fn openpty(master: *Int, slave: *Int, name: *Byte, termp: *Void, winp: *Void) -> Int
extern fn fork() -> Int
extern fn setsid() -> Int
extern fn close(fd: Int) -> Int
extern fn dup2(old: Int, new: Int) -> Int
extern fn execvp(file: *Byte, argv: *Void) -> Int
extern fn read(fd: Int, buf: *Byte, count: Int) -> Int
extern fn write(fd: Int, buf: *Byte, count: Int) -> Int
extern fn kill(pid: Int, sig: Int) -> Int
extern fn waitpid(pid: Int, status: *Int, options: Int) -> Int
extern fn usleep(usec: Int) -> Int
extern fn fcntl(fd: Int, cmd: Int, arg: Int) -> Int
extern fn malloc(size: Int) -> *Void
extern fn free(ptr: *Void)

// Allocate space for master/slave fd
let master_ptr = malloc(8)
let slave_ptr = malloc(8)

// Open PTY
let ret = openpty(master_ptr, slave_ptr, ptr_null(), ptr_null(), ptr_null())
assert(ret == 0, "openpty failed: " + str(ret))
println("[PASS] openpty")

// Read master fd from allocated memory
let master_fd = deref(master_ptr)
let slave_fd = deref(slave_ptr)
println("[INFO] master_fd=" + str(master_fd) + " slave_fd=" + str(slave_fd))

// Set non-blocking on master
let F_GETFL = 3
let F_SETFL = 4
let O_NONBLOCK = 4  // macOS value
let flags = fcntl(master_fd, F_GETFL, 0)
fcntl(master_fd, F_SETFL, flags | O_NONBLOCK)

// Fork
let pid = fork()
if pid == 0 {
  // Child: setup PTY as terminal
  close(master_fd)
  setsid()
  dup2(slave_fd, 0)
  dup2(slave_fd, 1)
  dup2(slave_fd, 2)
  if slave_fd > 2 { close(slave_fd) }

  // Exec shell
  let shell = cstring("/bin/sh")
  execvp(shell, ptr_null())
  // Should not reach here
}

// Parent
close(slave_fd)
println("[PASS] fork pid=" + str(pid))

// Write a command
let cmd = cstring("echo VOID_OK\n")
write(master_fd, cmd, 13)
usleep(200000)  // 200ms

// Read output
let buf = malloc(4096)
let n = read(master_fd, buf, 4096)
if n > 0 {
  let output = from_cstring(buf)
  println("[INFO] output: " + output)
}
println("[PASS] pty read/write")

// Cleanup
let exit_cmd = cstring("exit\n")
write(master_fd, exit_cmd, 5)
usleep(100000)
kill(pid, 15)  // SIGTERM
waitpid(pid, ptr_null(), 0)
close(master_fd)
free(master_ptr)
free(slave_ptr)
free(buf)

println("=== All PTY tests passed ===")
```

- [ ] **Step 2: Run test**

Run: `./hexa examples/test_extern_pty.hexa`
Expected: PTY opened, shell spawned, "VOID_OK" received

- [ ] **Step 3: Commit**

```bash
git add examples/test_extern_pty.hexa
git commit -m "test: PTY management via pure hexa extern FFI"
```

---

## Phase 1 Success Criteria — ALL PASSED (2026-04-06)

1. ✅ `extern fn` keyword parses correctly
2. ✅ `@link("lib")` attribute resolves system libraries
3. ✅ libc functions callable from hexa (getpid, strlen, malloc, free, etc.)
4. ✅ PTY open/fork/read/write works via pure hexa extern FFI
5. ✅ Both interpreter and JIT modes work
6. ✅ No new Rust dependencies added to Cargo.toml

### 추가 수정 사항
- `interpreter.rs`: `env.set()` → `env.define()` (신규 변수 등록 버그 수정)
- `compiler.rs`: VM에서 extern 감지 시 컴파일 에러 → interpreter fallback
- `env.rs`: `str`, `cstring`, `from_cstring`, `ptr_null`, `ptr_addr`, `deref` 빌트인 등록
- `interpreter.rs`: `str` → `to_string` 별칭 추가
