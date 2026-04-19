# ROI #143 — P7-7 Self-Compile Fixpoint Plan

**Date:** 2026-04-19
**Scope:** Planning only (no source edits this run, seed-freeze in effect).
**Estimate:** 24h strategic slot.
**Fixpoint gate (P5 precedent, project_selfhost_p5.md):** SHA256(hexa_2) == SHA256(hexa_3).

## 1. Current State Survey

### 1.1 Native-ready pieces (tonight's wiring)
| File | LOC | Status | Role |
|---|---|---|---|
| `self/codegen/mod.hexa` | 1,632 | wired | CodegenResult pipeline + call-fixup + symtab |
| `self/codegen/arm64.hexa` | 289 | wired | Base ARM64 encoders |
| `self/codegen/ir_to_arm64.hexa` | 636 | wired | IR walker -> ARM64 bytes (MOVZ/MOVK/ADD/SUB/MUL/AND/ORR/EOR/LSLV/LSRV/CMP/CSET/B.cond/BL/RET) |
| `self/codegen/ir_to_x86.hexa` | 830 | wired | IR walker -> x86_64 |
| `self/codegen/memop.hexa` | 332 | tonight (b17f3c7a) | typed load/store 8/16/32/64 both arches |
| `self/codegen/rt_symbols.hexa` | 234 | source-present, consumer-zero | `rt_target()="c"` frozen, flip knob |
| `self/codegen/x86_64.hexa` | 304 | wired | x86_64 encoders |
| `self/codegen/elf.hexa` / `macho.hexa` | 561 / 420 | wired | Object envelope |
| `self/codegen/regalloc_linear.hexa` | 150 | wired | Linear-scan (8 reg cap) |
| `self/codegen/peephole.hexa` | 252 | wired | 12-rule ARM64 peephole |
| `self/codegen/runtime_arm64.hexa` | 1,463 | partial | Native runtime body (WIP) |
| `tool/native_build.hexa` | 1,598 | wired | Zero-tool driver (`hexa -> native`) — inlines rt-routing helpers (commit a474928c) |
| `tool/verify_fixpoint.hexa` | 122 | skeleton | 3-stage SHA256 gate already codes to `build/hexa_fix{1,2,3}` |

### 1.2 Still clang-dependent (P7-7 must displace)
- `self/codegen_c2.hexa` (5,403 LOC) still emits `#include "runtime.c"` (L624) and is run via `hexa_v2 -> .c -> clang`.
- `self/runtime.c` (4,696 LOC) still required at link time when `rt_target()=="c"`.
- `build/hexa_stage0` = 6,117-byte Mach-O seed (2026-04-19 02:12). No `build/hexa_v2` binary in place — stage0 shim is the sole seed.
- `hexa_full.hexa` (17,273 LOC) has never been end-to-end native-compiled; scale coverage verified only for toy compile_add/fib/hello POCs (project_phase7_ir43_charpure40.md).
- rt/ modules (`math.hexa`, `math_abi.hexa`, `string.hexa`, `string_abi.hexa`, `convert.hexa`) transpile individually via hexa_v2 but are **not linked into stage0** yet — Step D of rebuild_stage0_rt_linkunit.md pending.

### 1.3 Scope boundary of "P7-7" in this doc
Per roadmap (`docs/plans/self-hosting-roadmap.md` L280-285) the fixpoint milestone is actually **P7-9**; P7-7 covers the runtime-hexa consolidation that unlocks it. Treating ROI #143 as "reach the fixpoint using the P7-7 rt/native path" — requires P7-7 (runtime) + P7-8 (hexa_full scale) + P7-9 (gate) closed together.

## 2. Fixpoint Protocol (exact byte-comparison)

### 2.1 Artefact paths (match verify_fixpoint.hexa)
```
seed   : build/hexa_stage0       (frozen, see seed-freeze note HX8)
src    : self/hexa_full.hexa     (17,273 LOC; SSOT compiler source)
hexa_1 : build/hexa_fix1         (seed compiles src)
hexa_2 : build/hexa_fix2         (hexa_1 compiles src)
hexa_3 : build/hexa_fix3         (hexa_2 compiles src)
log    : shared/hexa-lang/fixpoint_log.json  (append per run)
```

### 2.2 Gate
```
SHA256(hexa_2) == SHA256(hexa_3)  =>  fixpoint CLOSED
SHA256(hexa_1) != SHA256(hexa_2)  is tolerable on first run
                                   (seed was clang-linked; hexa_1 is native-emitted)
```

### 2.3 Cross-target variant (optional, post-close)
Run `hexa tool/verify_fixpoint.hexa --target=linux-x86_64` on Mac, then `hexa tool/verify_fixpoint.hexa` on ubu. Compare `hexa_3` from both — must be byte-identical if determinism holds.

## 3. Step-by-Step Path

### Phase A — P7-6 PoC consolidation (prereq, 0-2h)
- A1 Land rt-flip Step D (rebuild_stage0_rt_linkunit.md §4 Option A) — transpile rt/*.hexa and append to compile unit.
- A2 Run `verify_fixpoint.hexa --dry-run` gate added: compile **one** tiny `self/rt/test_math.hexa` via native_build, confirm non-zero Mach-O and SHA stable.
- A3 Inventory IR coverage gaps. `native_build.hexa` today handles FnDecl/ReturnStmt/IntLit/BinOp; hexa_full needs StringLit/ArrayLit/MapLit/Field/Index/Match/TryCatch (see self-hosting-roadmap.md L281).

### Phase B — Scaled native (4-8h)
- B1 For each missing AST kind, add `nb_lower_expr_<kind>` in native_build.hexa and a matching ir_to_arm64 path.
- B2 Use `self/poc/expr/*` and `self/poc/stmt/*` dispatcher stubs as coverage checklist.
- B3 After each kind, compile a 50-200 LOC smoke unit; persist SHA in a scratch log (`/tmp/p7_7_coverage.log`).
- B4 Green-gate: `hexa tool/native_build.hexa self/hexa_full.hexa -o build/hexa_fix_probe` exits 0, produces executable.

### Phase C — Self-host transition (6-10h)
- C1 Execute `build/hexa_fix_probe --version`; ensure it at least dumps a banner (proves it links and runs).
- C2 Replace clang path: compile any trivial `self/rt/test_math.hexa` using `build/hexa_fix_probe` instead of stage0. Compare behaviour to hexa_v2-compiled version.
- C3 Flip `cg_rt_target()` from `"c"` to `"rt"` once rt/*.hexa link unit is confirmed (Step C of rt_target_flip_wiring.md). **This is the point of no return for runtime.c.**
- C4 Rebuild hexa_stage0 once (FORCE=1) to take the flip. Sanity: println("ok") golden test.

### Phase D — First fixpoint cycle (2-3h)
- D1 `hexa tool/verify_fixpoint.hexa --target=darwin-arm64`.
- D2 Expected outcomes:
  - Best case: hexa_2 == hexa_3 on first attempt => CLOSED.
  - Typical case: 1-3 byte regions diverge => go to Phase E.
- D3 Log SHA triple + binary size to `shared/hexa-lang/fixpoint_log.json`.

### Phase E — Byte-diff investigation (4-8h if needed)
- E1 `cmp -l build/hexa_fix2 build/hexa_fix3 | head` to locate first delta region.
- E2 Classify (see Risk Register §5). Most common: symbol-table ordering, string-literal pool ordering, FP rounding in constants, relocation offsets.
- E3 Patch the non-determinism source, rerun Phase D.
- E4 Accept at most 3 iterations; if >3, escalate to n6 BT-113/234 review.

### Phase F — Closed state wiring (1h)
- F1 Append fixpoint closure entry to `shared/hexa/state.json` (CDO breakthrough).
- F2 Update self-hosting-roadmap.md: P7-7/8/9 checked.
- F3 Rename plan per HX10: hexa_v2 -> hexa, hexa_stage0 -> hexa. **Do NOT** do the rename in the same commit as fixpoint closure — separate commit for bisectability.

## 4. Test Harness Design — `tool/test_p7_7_fixpoint.hexa` sketch

Purpose: per-phase smoke harness complementing `verify_fixpoint.hexa` (which is whole-compiler). This file gates the cheaper sub-steps before committing to full 3x hexa_full compiles (each ~30s + memory risk per HX8).

```hexa
// tool/test_p7_7_fixpoint.hexa
// Cheap pre-gates for P7-7 fixpoint. Exits 0 on all green.

fn stage_hash(path: string) -> string {
    return exec("shasum -a 256 " + path).split(" ")[0].trim()
}

fn native_compile(src: string, out: string, seed: string) -> int {
    let cmd = seed + " tool/native_build.hexa " + src + " -o " + out
    return exec_with_status(cmd)[1]
}

fn gate_rt_transpile() -> bool {
    // P7-7 rt modules must transpile + link
    let rc = native_compile("self/rt/test_math.hexa", "build/test_rt_math", "build/hexa_stage0")
    if rc != 0 { return false }
    return exec_with_status("build/test_rt_math")[1] == 0
}

fn gate_hexa_full_native() -> bool {
    // hexa_full must produce SOME executable native bin
    let rc = native_compile("self/hexa_full.hexa", "build/hexa_fix_probe",
                            "build/hexa_stage0")
    return rc == 0 && file_exists("build/hexa_fix_probe")
}

fn gate_self_compile_one_step() -> bool {
    // hexa_fix_probe must at least boot on a trivial hello
    let rc = exec_with_status("build/hexa_fix_probe tool/native_build.hexa "
                            + "self/compile_hello.hexa -o /tmp/hello_fix").1
    return rc == 0 && file_exists("/tmp/hello_fix")
}

fn gate_twice_identical() -> bool {
    // The real fixpoint gate, but only run after the cheap gates pass
    if native_compile("self/hexa_full.hexa", "build/hexa_fix2",
                      "build/hexa_fix_probe") != 0 { return false }
    if native_compile("self/hexa_full.hexa", "build/hexa_fix3",
                      "build/hexa_fix2") != 0 { return false }
    return stage_hash("build/hexa_fix2") == stage_hash("build/hexa_fix3")
}

fn main() {
    if !gate_rt_transpile()       { eprintln("FAIL: rt transpile"); exit(1) }
    if !gate_hexa_full_native()   { eprintln("FAIL: hexa_full native"); exit(2) }
    if !gate_self_compile_one_step(){eprintln("FAIL: self-compile one step"); exit(3) }
    if !gate_twice_identical()    { eprintln("FAIL: not converged"); exit(4) }
    println("[p7_7] FIXPOINT CLOSED")
}
```

## 5. Risk Register

| # | Risk | Likelihood | Impact | Mitigation |
|---|------|---|---|---|
| R1 | **Symbol-ordering non-determinism** — HashMap iteration produces different symbol table order run-to-run | HIGH | byte divergence | Enforce sorted emission in `FuncSymbol` list; lint in tool/test_p7_7_fixpoint.hexa. Also applies to string literal pool. |
| R2 | **Timestamp/build-id baked in** — Mach-O LC_BUILD_VERSION / ELF build-id captures wall time | MED | byte divergence | Zero the relevant fields in macho.hexa / elf.hexa; document as "reproducible build" invariant. |
| R3 | **FP rounding in compile-time constants** | LOW | byte divergence | Force int-only AST constants in hexa_full; defer FP folding. |
| R4 | **PIC relocation variance** — BL/ADR offsets depend on emission order | MED | byte divergence | Lock `symtab_lookup` order; single-pass fixup pass. mod.hexa L76-80 already deterministic — confirm. |
| R5 | **Stripped vs unstripped symbols** | LOW | byte divergence | Run both sides through identical post-link (no strip; or always strip). |
| R6 | **xpcproxy flood on 3x rebuild** (HX8) | MED | mac panic | verify_fixpoint already uses stage0 seed path; ensure `tool/rebuild_stage0.hexa` wrapper is NOT invoked during the 3x compile — native_build is direct. |
| R7 | **Memory blow during hexa_full lower** — interp O(N^2) push (feedback_interp_push_quadratic.md) | MED | OOM | Stream IR instructions; avoid `arr=arr.push(x)` loops in ir_to_arm64 hot path. Use `parts.join("")` pattern. |
| R8 | **seed-freeze blocker** (project_hexa_v2_circular_rebuild_20260417.md) | ACTIVE | blocks rebuild_stage0.hexa | Do NOT rebuild stage0 during P7-7. Use existing `build/hexa_stage0` as frozen seed. Fix circular regression separately. |
| R9 | **IC-slot corruption** in codegen_c2 _ic_def_parts aliasing | KNOWN | stale cg c2 | P7-7 bypasses codegen_c2 entirely when rt_target="rt"; R9 becomes a non-blocker post-flip. |
| R10 | **rt ABI drift** — rt_*_v wrappers signatures desync with callers | MED | link error | Auto-generate from rt_symbols.hexa SSOT; add lint in tool/check_ssot_sync.hexa. |
| R11 | **Per-host Mach-O codesign field variance** | LOW | byte divergence | Run codesign --remove-signature before SHA; compare unsigned bytes. Adjust verify_fixpoint.hexa. |
| R12 | **Call-fixup offset drift** when peephole changes code length | LOW-MED | semantic bug, not just bytes | Run peephole BEFORE fixup; verify mod.hexa ordering — currently mod.hexa L20-29 shows peephole runs before macho_wrap which happens before fixup. Confirm. |

## 6. Time Budget (24h estimate)

| Phase | Hours | Cumulative | Confidence |
|---|---|---|---|
| A — Phase A prereq (rt Step D + coverage inventory) | 2 | 2 | HIGH |
| B — Scaled native (AST kinds + smoke) | 6 | 8 | MED |
| C — Self-host transition (flip cg_rt_target) | 4 | 12 | MED |
| D — First fixpoint cycle | 3 | 15 | MED-LOW |
| E — Byte-diff investigation (budgeted, may not all consume) | 6 | 21 | LOW |
| F — Closure wiring + doc + roadmap update | 1 | 22 | HIGH |
| Slack buffer | 2 | 24 | — |

**Overall confidence in 24h estimate: ~45%.** The dominant risks are B (AST coverage drifts; hexa_full has surface features not yet exercised by POCs) and E (byte-diff root-causing can spiral). P5 precedent gives confidence — it closed in a single session — but P7 native has ~3x more surface than P5 bytecode.

## 7. First-cut 2h De-risking Tasks (runnable next session)

Ordered by derisk-per-hour. No stage0 rebuild needed for any of these.

### T1 — **Bootstrap one IR module via the native encoder path (30 min)**
```
hexa tool/native_build.hexa self/rt/test_math.hexa -o /tmp/probe_rt
cmp <(hexa tool/native_build.hexa self/rt/test_math.hexa -o /dev/stdout) \
    <(hexa tool/native_build.hexa self/rt/test_math.hexa -o /dev/stdout)
```
Log bytes. If cmp diverges across two identical compiles, **R1/R2/R4 is already present today** and we must fix determinism before even trying hexa_full.

### T2 — **AST coverage scan (30 min)**
Grep `self/hexa_full.hexa` for AST kinds not currently covered in native_build.hexa lower paths. Produce `/tmp/p7_7_coverage_gaps.md`. This sizes Phase B precisely (MED estimate today).

### T3 — **Write & commit test_p7_7_fixpoint.hexa skeleton (30 min)**
Land §4 harness. All gates return `false` initially; that's fine — gives us the scaffold to iteratively green.

### T4 — **Determinism audit of macho.hexa / elf.hexa (30 min)**
Read wrap functions; list every field that is NOT a deterministic function of input bytes. Timestamps, UUIDs, build-ids, LC_UUID, strtab ordering. Expected output: <10 entries; becomes concrete patch list for Phase E.

**Expected after 2h:** We know (a) whether native_build is deterministic today, (b) the exact AST feature gap to hexa_full, (c) the determinism field list to neutralise. If T1 fails → escalate R1/R2 to blocker before Phase B. If T1 passes → Phase A-to-B transition becomes straight-line.

## References
- `docs/plans/self-hosting-roadmap.md` L276-285 (Phase 7 roadmap)
- `docs/plans/rt_target_flip_wiring.md` (flip mechanics)
- `docs/plans/rebuild_stage0_rt_linkunit.md` (Step D rt link)
- `tool/verify_fixpoint.hexa` (existing 3-stage SHA gate)
- `project_selfhost_p5.md` (P5 fixpoint precedent, 2026-04-10)
- `project_phase7_ir43_charpure40.md` (PoC scale)
- `project_hexa_v2_circular_rebuild_20260417.md` (seed-freeze constraint)
- `feedback_interp_push_quadratic.md` (memory blowup risk)
