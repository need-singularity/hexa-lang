# Step D Readiness Survey: build_stage0 링크 유닛 확장 (rt/*.hexa transpile & link)

**Date:** 2026-04-19  
**Status:** Ready for implementation (low risk, mid complexity)  
**Estimated LOC delta to rebuild_stage0.hexa:** 80–120 lines  

---

## Section 1: Current Build Pipeline

The stage0 rebuild pipeline (commit 3b99e34c + related) follows this flow:

1. **Flatten phase** (`scripts/rebuild_stage0.hexa:L657–756`):
   - Entry: `flatten_imports.hexa` (L434) discovers transitive `use` imports from `self/hexa_full.hexa`
   - Output: single flattened .hexa file (L614: `/tmp/hexa_full_flat.hexa` or cache)
   - Caching: content-hash key (L501: `flatten_key`) enables cache reuse if .hexa graph unchanged

2. **Transpile phase** (`scripts/rebuild_stage0.hexa:L759–771`; `build_stage0.hexa:L208–209`):
   - Binary: `hexa_v2` (or dedup4 variant, L52–90 in build_stage0.hexa)
   - Command: `hexa_v2 <flattened.hexa> <output.c>`
   - Produces: `/tmp/hexa_full_regen.c` (or `HEXA_STAGE0_C` override)
   - Includes runtime at line 624 of codegen_c2.hexa: `parts = parts.push("#include \"runtime.c\"\n\n")`

3. **Compile phase** (`scripts/rebuild_stage0.hexa:L804–820`; `build_stage0.hexa:L224–241`):
   - Compiler: clang (or `CLANG` env override)
   - Flags: `-O2 -std=c11 -Wno-trigraphs` + arch-specific (`-D_GNU_SOURCE` on Linux, `-lm -ldl` for libm/dlfcn)
   - Input: transpiled C + `self/runtime.c` (copied as sibling, L220 in build_stage0.hexa)
   - Output: `build/hexa_stage0` (binary)

4. **Post-link** (`scripts/rebuild_stage0.hexa:L872–883`; `build_stage0.hexa:L288–299`):
   - macOS codesign (ad-hoc, required for AppleSystemPolicy)
   - Sanity test: run trivial `println("ok")` through stage0, expect "ok\n42\n" (L160–210 in rebuild_stage0.hexa)
   - Shim install (flock+RSS-cap defense, commit ca46279)

---

## Section 2: How runtime.c is #included

**Mechanism:** The C code generator inlines the `#include` directive at emit time, not as a file concatenation.

**Location:**
- `self/codegen_c2.hexa:L624` — SSOT emit site:
  ```hexa
  parts = parts.push("#include \"runtime.c\"\n\n")
  ```
- Also mirrored in `self/native/hexa_cc.c:8579` (legacy transpiler):
  ```c
  parts = hexa_array_push(parts, hexa_str("#include \"runtime.c\"\n\n"));
  ```

**Implication:** The C preprocessor resolves `#include "runtime.c"` at clang compile time. The path is relative to the compile invocation's `-I` flag (currently `-I self`). No manual concatenation needed **today**, but **to add rt/*.hexa symbols**, we must transpile them to C and make them available as include files or object modules.

---

## Section 3: The 5 rt/ Files — Parse & Dependency Analysis

### Per-file Status

| File | Parse | Uses | Dependencies | Comment |
|------|-------|------|--------------|---------|
| `math.hexa` | ✓ OK | none | — | Pure hexa math kernels (no deps) |
| `math_abi.hexa` | ✓ OK | `rt::math`, `rt::convert` | math, convert | HexaVal ↔ float marshalling layer |
| `string.hexa` | ✓ OK | none | — | Pure hexa string ops (no deps) |
| `string_abi.hexa` | ✓ OK | `rt::string`, `rt::convert` | string, convert | HexaVal ↔ string marshalling layer |
| `convert.hexa` | ✓ OK | `rt::fmt`, `rt::string`, `rt::alloc` | fmt, string, alloc, syscall | Value coercion suite |

**Chain:** `math_abi` → `{math, convert}` | `string_abi` → `{string, convert}` | `convert` → `{fmt, string, alloc}` | `alloc` → `syscall`

**Key finding:** All 5 target files parse cleanly. Dependency graph is acyclic (DAG).

### Transpile Readiness

Both files compile via stage0 already (e.g. `hexa_v2 self/rt/math.hexa /tmp/math.c` works). No `@pure` limitation — codegen_c2 handles them (all 5 are pure functions or simple control flow).

---

## Section 4: Two Strategic Options

### Option A: Transpile rt/*.hexa separately → append C to runtime.c (or link objects)

**Steps:**
1. Before clang compile (in `rebuild_stage0.hexa:L810–820`), transpile each of the 5 rt/ files:
   ```hexa
   hexa_v2 self/rt/math.hexa /tmp/rt_math.c
   hexa_v2 self/rt/math_abi.hexa /tmp/rt_math_abi.c
   hexa_v2 self/rt/convert.hexa /tmp/rt_convert.c
   hexa_v2 self/rt/string.hexa /tmp/rt_string.c
   hexa_v2 self/rt/string_abi.hexa /tmp/rt_string_abi.c
   ```

2. **Concat strategy** (simplest):
   ```bash
   cat /tmp/rt_math.c /tmp/rt_math_abi.c /tmp/rt_convert.c \
       /tmp/rt_string.c /tmp/rt_string_abi.c >> /tmp/hexa_full_regen.c
   ```

3. **Link strategy** (compile each to .o, then linker merges):
   ```bash
   clang -c /tmp/rt_math.c -o /tmp/rt_math.o
   # ... repeat for all 5
   clang ... hexa_full_regen.c /tmp/rt_math.o ... -o build/hexa_stage0
   ```

**Edits to rebuild_stage0.hexa:**
- Add helper fn (15 lines): `fn transpile_rt_files(hexa_dir, rt_files) → [string]` returning list of .c paths
- Insert before build_stage0 invocation (L810): call helper, capture outputs
- If concat: append all .c to `src_c` (5 lines)
- If link: add `.o` files to clang command (3 lines)

**Risks:**
- Symbol collisions: if rt/math.hexa and rt/string.hexa define same helper names (low risk — they don't)
- Initialization order: rt_convert.hexa uses rt_alloc.hexa — need topological sort of includes (mitigation: list order matters; compute DAG order once)
- Build time: +50–100ms per transpile invocation (5 × hexa_v2 runs)
- Seed freeze: these transpiles happen **after** flatten, so seed binary is unaffected

**Confidence:** High (isolated, no feedback loop)

---

### Option B: Add rt/math, rt/string, rt/convert to codegen_c2's input graph via flatten

**Steps:**
1. Modify `self/hexa_full.hexa` or the entry point fed to flatten_imports to include:
   ```hexa
   use "rt/math"
   use "rt/string"
   use "rt/convert"
   ```

2. flatten_imports already handles transitive deps (L289–332 in rebuild_stage0.hexa), so it will pull in fmt/alloc/syscall automatically.

3. Single transpile pass emits all symbols in one .c file; no concat/link needed.

**Edits to rebuild_stage0.hexa:**
- **Zero edits** to rebuild_stage0.hexa itself (the `use` directives go in hexa_full.hexa)
- Modify `self/hexa_full.hexa` (3–4 lines): add use statements at top

**Risks:**
- **Modularity break:** codegen_c2.hexa becomes responsible for rt symbols; if rt_target flips and codegen_c2 is in use elsewhere (native_build.hexa), it emits rt_*_v symbols globally
- **Seed freeze:** the flatten graph changes, so rebuild_stage0's flatten_key changes → cache invalidation (acceptable)
- **Test burden:** rt/ files expose @pure fns that must not raise exceptions; codegen must trust their semantics

**Confidence:** Medium (simpler code, but broader scope)

---

## Section 5: Fast-Lane MVP

**Smallest viable change to make math-only flip work:**

1. **Option A + math-only scope:**
   - Transpile only `rt/math.hexa`, `rt/math_abi.hexa`, `rt/convert.hexa` to C
   - Append to `hexa_full_regen.c`
   - No string wrappers needed yet

2. **Code location in rebuild_stage0.hexa:**
   - Insert at L798 (pre-build, after transpile cache logic):
     ```hexa
     // Transpile rt math linkunit
     let rt_math_c = "/tmp/rt_math.c"
     let rt_math_abi_c = "/tmp/rt_math_abi.c"
     let rt_convert_c = "/tmp/rt_convert.c"
     
     if env("SKIP_TRANSPILE") == "" {
       exec_with_status("" + hexa_v2 + " self/rt/math.hexa " + rt_math_c)
       exec_with_status("" + hexa_v2 + " self/rt/math_abi.hexa " + rt_math_abi_c)
       exec_with_status("" + hexa_v2 + " self/rt/convert.hexa " + rt_convert_c)
     }
     
     // Append to transpiled C
     exec("cat " + rt_math_c + " " + rt_math_abi_c + " " + rt_convert_c + " >> " + src_c)
     ```

3. **Estimated LOC:** 20 lines (per-file transpile calls + concat)

4. **Seed freeze:** Not affected (happens post-flatten, pre-link)

5. **Flip activation:** Change `cg_rt_target() → "rt"` in codegen_c2.hexa (1 line), rebuild stage0 (triggers build), and math symbols auto-resolve.

---

## Section 6: Open Questions

1. **Does codegen_c2 emit correct C for all rt/math.hexa constructs?** ✓ Verified — no @pure-only limitation; `while` loops, array ops, type dispatch all work.

2. **Does flatten_imports resolve `use self::rt::math` correctly?** — Partial: `use "self/rt/math"` works, but `use self::` syntax (with `::`) may need parsing update. **Action:** Test `hexa scripts/flatten_imports.hexa self/rt/math_abi.hexa /tmp/flat_test.hexa` to confirm.

3. **What symbols must NOT be removed from runtime.c after flip?** — LOAD_BEARING 261 (per rt_target_flip_wiring.md:94). ROUTABLE 43 (safe to remove once rt/convert+math_abi+string_abi are linked). Audit needed before cutover.

4. **Does rt_to_float properly handle the HexaVal NaN-box format at C link time?** — Yes: rt_to_float reads `typeof()` dispatch, which codegen_c2 lowers to tag-bit inspection. No ABI mismatch.

5. **What build-time regressions should we monitor?** — Per-rebuild +50–100ms (5 hexa_v2 invokes). Cache will offset if rt/ files unchanged between runs.

---

## Report (200 words)

**Findings:**

The rt/ linkunit expansion is well-scoped and low-risk. All 5 target files (math, math_abi, string, string_abi, convert) parse cleanly and have acyclic dependencies. The current build pipeline can integrate transpiled rt/ C at two points: **(A) post-transpile concatenation** (simplest, 20–80 LOC in rebuild_stage0.hexa), or **(B) flatten graph inclusion** (cleaner architecture, 3 LOC in hexa_full.hexa, requires flatten syntax audit).

**Recommendation:**

**Option A (Math-only MVP first)** for immediate activation. Transpile rt/math.hexa + rt/math_abi.hexa + rt/convert.hexa separately in rebuild_stage0.hexa's build section (L798–820), append to the transpiled C before clang link. This requires ~20 lines and zero codegen changes. Once working and tested, generalize to string (Option A full) or refactor to Option B if architecture review warrants it.

**Confidence:** High (isolated, proven parse/transpile, DAG dependencies).

**Top 3 blockers:**

1. **flatten_imports syntax:** Does `use self::rt::convert` parse correctly (vs. `use "self/rt/convert"`)? Need test before Option B is viable.

2. **Symbol collision audit:** Verify no name collisions between rt_*.h symbols and runtime.c (low risk, but required pre-flip).

3. **LOAD_BEARING cutover:** Identify which runtime.c symbols MUST stay (261 per plan). Removal is post-flip work, not Step D blocker, but the cutover plan needs commitment.

