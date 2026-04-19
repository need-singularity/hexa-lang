# `syscall` builtin — P7-7a spec

Purpose: replace every libc function in `self/runtime.c` with a direct kernel call from pure hexa. Unblocks P7-7b/c/d/e and the whole C/Rust elimination track.

Decision: **builtin function**, not attribute. Rationale below.

## Form

```hexa
syscall(num: int, args...: int) -> int
```

- Variadic: 0–6 int args (Linux + Darwin both cap at 6 register args).
- Returns: int (syscall result). Negative on error; caller checks and either extracts errno-equivalent or inspects.
- All args passed as int (pointers coerce via `hexa_ptr_addr` or identity).
- `@pure` incompatible (syscall has side effects).

### Examples

```hexa
// Linux x86_64: sys_write(fd=1, buf, len) = 1
fn write_linux(fd: int, buf: int, len: int) -> int {
    return syscall(1, fd, buf, len)
}

// Darwin ARM64: sys_write(fd, buf, len) = 4 (BSD) via svc #0x80
fn write_darwin(fd: int, buf: int, len: int) -> int {
    return syscall(4, fd, buf, len)
}
```

Portable wrapper lives in `self/rt/syscall.hexa` — per-target const tables select the right number.

## Why builtin, not `@syscall` attr

Considered `@syscall(4) fn write(fd, buf, len) -> int` (declarative). Rejected:

1. **Call-site visibility**: syscall should read as a direct call in source. `@syscall` hides it at def site, source readers see a normal fn call and must jump to def.
2. **Parser burden**: attributes need collection + validation + forwarded to IR. Builtin is 1 name in the global builtin table.
3. **Dispatch is already per-target**: syscall numbers differ Linux/Darwin. Wrapping in per-target consts (below) is cleaner than per-target attrs.
4. **Consistency with `exec`, `print`**: existing I/O builtins don't use attrs. `syscall` joins them at the same level.

## Codegen

### ARM64 (Linux + Darwin)

Register setup:

```
MOV X16, #num         ; syscall number in X16 (Darwin) or X8 (Linux)
MOV X0..X5, args      ; up to 6 args in X0-X5
SVC  #0x80            ; Darwin
SVC  #0               ; Linux
; result now in X0
```

Darwin uses `svc #0x80` with syscall# in `X16` and BSD layer offset `0x2000000` baked into the number. Linux uses `svc #0` with syscall# in `X8`.

### x86_64 (Linux)

```
MOV RAX, num          ; syscall number in RAX
MOV RDI, arg0         ; SysV calling convention RDI/RSI/RDX/R10/R8/R9
MOV RSI, arg1
MOV RDX, arg2
MOV R10, arg3         ; note: R10 (not RCX — syscall overwrites RCX with RIP)
MOV R8,  arg4
MOV R9,  arg5
SYSCALL
; result in RAX
```

### Emit site

- `self/codegen/arm64.hexa` — add `emit_syscall_arm64(target_os, num, args)`.
- `self/codegen/x86_64.hexa` — add `emit_syscall_x86(num, args)`.
- `self/ir/opcodes.hexa` — add `OP_SYSCALL` opcode.
- `self/ir/lowering.hexa` — recognize `syscall(n, ...)` call → emit `OP_SYSCALL` IR instr.
- `self/codegen/ir_to_arm64.hexa` + `vm_to_arm64.hexa` — lower `OP_SYSCALL` to the sequence above.
- Register allocation: args must land in X0-X5 (arm64) or RDI/RSI/RDX/R10/R8/R9 (x86). Use force-live-range hint in regalloc, spill conflicts as needed.

### Clobbers

- ARM64: X0-X17 potentially clobbered (caller-saved). Regalloc treats `OP_SYSCALL` as a def of X0 and a clobber of X1-X17.
- x86_64: RAX, RCX, R11 clobbered by `syscall` insn itself; RDI/RSI/RDX/R10/R8/R9 hold inputs. Regalloc treats `OP_SYSCALL` as def RAX + clobber RCX/R11 (rest are already live-out of the call).

## Per-target syscall tables

`self/rt/syscall.hexa` exports per-target constants:

```hexa
// Linux x86_64 / ARM64 unified (subset)
const SYS_LINUX_READ         = 0
const SYS_LINUX_WRITE        = 1
const SYS_LINUX_OPEN         = 2
const SYS_LINUX_CLOSE        = 3
const SYS_LINUX_STAT         = 4
const SYS_LINUX_FSTAT        = 5
const SYS_LINUX_LSEEK        = 8
const SYS_LINUX_MMAP         = 9
const SYS_LINUX_MUNMAP       = 11
const SYS_LINUX_BRK          = 12
const SYS_LINUX_RT_SIGACTION = 13
const SYS_LINUX_IOCTL        = 16
const SYS_LINUX_PIPE         = 22
const SYS_LINUX_CLOCK_GETTIME= 228
const SYS_LINUX_EXIT_GROUP   = 231
const SYS_LINUX_GETDENTS64   = 217
const SYS_LINUX_GETRANDOM    = 318
const SYS_LINUX_UNLINKAT     = 263
const SYS_LINUX_NANOSLEEP    = 35
const SYS_LINUX_FORK         = 57
const SYS_LINUX_EXECVE       = 59
const SYS_LINUX_WAIT4        = 61

// Darwin ARM64 (BSD layer; OR 0x2000000 at svc)
const SYS_DARWIN_WRITE       = 0x2000004
const SYS_DARWIN_READ        = 0x2000003
const SYS_DARWIN_OPEN        = 0x2000005
const SYS_DARWIN_CLOSE       = 0x2000006
const SYS_DARWIN_EXIT        = 0x2000001
const SYS_DARWIN_MMAP        = 0x20000C5
const SYS_DARWIN_MUNMAP      = 0x2000049
const SYS_DARWIN_FSTAT64     = 0x20000BD
const SYS_DARWIN_GETDIRENTRIES = 0x2000154
const SYS_DARWIN_UNLINK      = 0x200000A
const SYS_DARWIN_FORK        = 0x2000002
const SYS_DARWIN_EXECVE      = 0x200003B
const SYS_DARWIN_WAIT4       = 0x2000007
```

Source: Linux `arch/x86/entry/syscalls/syscall_64.tbl` + Darwin `bsd/kern/syscalls.master`.

## Portable wrappers

`self/rt/syscall.hexa` provides the per-target dispatch once. Callers use neutral names:

```hexa
fn sys_write(fd: int, buf: int, len: int) -> int {
    if target_is_linux() { return syscall(SYS_LINUX_WRITE, fd, buf, len) }
    return syscall(SYS_DARWIN_WRITE, fd, buf, len)
}
```

`target_is_linux()` / `target_is_darwin()` are compile-time constants populated by the codegen target (`--target=linux-x86_64` vs `--target=darwin-arm64`). At codegen dead code elim removes the unreachable branch.

## Intrinsic tests

`self/rt/test_syscall.hexa`:

```hexa
fn main() {
    let msg = "hello from syscall\n"
    let addr = hexa_ptr_addr(msg)        // cstring pointer
    let rc = sys_write(1, addr, 19)
    sys_exit(0)
}
```

Success criteria:
- Compiles to ARM64 Mach-O + Linux x86_64 ELF, no clang, no runtime.c linkage.
- Binary size ≤ 4 KB (single page __TEXT).
- Prints "hello from syscall\n" and exits 0.

## Risks & mitigations

| Risk | Mitigation |
|---|---|
| Darwin SIP blocks ad-hoc syscall binaries | ad-hoc codesign (`codesign -s -`) in build script (already in P7-6 pipeline) |
| Regalloc spills into X16 (scratch) corrupt syscall num | X16 reserved from allocator pool during OP_SYSCALL, pre-assigned |
| Darwin ABI changes between OS versions | cap at syscalls that have been stable since macOS 11; document minimum |
| Float args (e.g. nanosleep timespec) | timespec is struct of ints — pack via hexa_ptr_alloc + ptr_write, pass ptr. No float syscalls needed for P7-7 scope |
| errno encoding | Linux: negative return = -errno. Darwin ARM64: carry flag set on error, value in X0 = errno. ARM64 codegen must read PSTATE carry after svc and translate to negative return to normalize |

## Phase gates

- [x] Spec written (this doc)
- [ ] `self/rt/syscall.hexa` (skeleton with per-target tables + portable wrappers)
- [ ] `OP_SYSCALL` opcode added to `self/ir/opcodes.hexa`
- [ ] `emit_syscall_arm64` / `emit_syscall_x86` in codegen
- [ ] `lower_call` in `self/ir/lowering.hexa` recognizes `syscall(...)`
- [ ] Regalloc clobber rules for OP_SYSCALL
- [ ] Darwin errno translation (PSTATE carry → -errno)
- [ ] `self/rt/test_syscall.hexa` PASS on ARM64 Mach-O + x86_64 ELF
- [ ] Grammar.jsonl entry SPEC_SYSCALL_BUILTIN

## Deliverables

1. `self/rt/syscall.hexa` — 120 LOC, per-target tables + sys_* wrappers
2. `self/codegen/syscall.hexa` — 80 LOC, IR lowering helpers
3. `self/codegen/arm64.hexa` — +40 LOC, syscall emit helper
4. `self/codegen/x86_64.hexa` — +40 LOC, syscall emit helper
5. `self/ir/lowering.hexa` — +30 LOC, `syscall(...)` call → OP_SYSCALL
6. `shared/hexa-lang/grammar.jsonl` — +1 spec entry SPEC_SYSCALL_BUILTIN
7. `self/rt/test_syscall.hexa` — 30 LOC smoke test
