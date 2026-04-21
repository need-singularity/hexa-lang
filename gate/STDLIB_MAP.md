# Stdlib Audit: Pure Hexa Modules to Eliminate Shell Subprocesses

**Generated:** 2026-04-21  
**Scope:** `/Users/ghost/core/hexa-lang/self/runtime/*_pure.hexa` modules that can replace `exec()` calls in gate/ files.

## Import Syntax (Confirmed Working)

For files in `/Users/ghost/core/hexa-lang/gate/`, use:
```hexa
use "runtime/url_pure"
use "runtime/path_pure"
use "runtime/json_mini_pure"
use "runtime/file_pure"
use "runtime/string_pure"
```

The `use` directive resolves relative to the hexa-lang root. The path is `runtime/<module>` (no `self/` prefix, no `.hexa` extension).

**Test invocation** (confirmed 2026-04-21):
```bash
cd /Users/ghost/core/hexa-lang
hexa eval < /tmp/test_import.hexa
# Output:
#   hello%20world
#   hello world
```

---

## Module-by-Module Export Map

### 1. url_pure.hexa

**Location:** `self/runtime/url_pure.hexa` (22 lines, 2 pub fn)

| Export | Signature | Purpose | Replaces |
|--------|-----------|---------|----------|
| `url_encode_pure(s)` | `(str) -> str` | RFC 3986 percent-encoding (keeps `[A-Za-z0-9-_.~]` verbatim, rest → `%XX` uppercase hex) | N/A (no gate usage) |
| `url_decode_pure(s)` | `(str) -> str` | Reverse: `%XX` → byte, `+` → space (form-urlencoded flavor). Lenient on malformed. | `exec("printf '%s' '<input>' \| python3 -c 'urllib.parse.unquote_plus(...)'")` |

**Gate Files Using URL Decode:**  
None currently in gate/. However, `hexa_url.hexa` (line 41–67) already implements `url_decode()` as a local function. **Could migrate to stdlib** if gate files start receiving URL-encoded query params.

**Risk/Benefit:**
- **Benefit:** Reusable across projects; RFC 3986 compliant.
- **Risk:** Mini module, gate/ already has inline version; migration deferred to phase 2.

---

### 2. path_pure.hexa

**Location:** `self/runtime/path_pure.hexa` (130 lines, 6 pub fn)

| Export | Signature | Purpose | Replaces |
|--------|-----------|---------|----------|
| `basename_pure(path)` | `(str) -> str` | Everything after last `/`. Strips trailing slashes. Returns `""` for `/`. | `exec("basename '<path>'")` – **currently used 4+ times in gate/** |
| `dirname_pure(path)` | `(str) -> str` | Everything before last `/`. Returns `""` for bare names, `/` for roots. | `exec("dirname '<path>'")` – **used 1 time in gate/** |
| `ext_pure(path)` | `(str) -> str` | File extension WITHOUT leading dot (e.g., `foo.tar.gz` → `gz`). Returns `""` if none. | N/A (not used in gate/) |
| `stem_pure(path)` | `(str) -> str` | Basename with final extension removed (e.g., `foo.tar.gz` → `foo.tar`). | N/A (not used in gate/) |
| `join_path_pure(a, b)` | `(str, str) -> str` | Join two path segments with `/`, normalizing slashes. Never produces `a//b`. | N/A (not used in gate/, but useful for future) |
| `is_absolute_pure(path)` | `(str) -> bool` | True iff path starts with `/`. | N/A (not used in gate/) |

**Gate Files Using Path Functions:**

| File | Line | Pattern | Replacement |
|------|------|---------|-------------|
| `claude_statusline.hexa` | 14 | `exec("basename '" + root + "'")` | `basename_pure(root)` |
| `lint.hexa` | 359 | `exec("basename '" + path + "' 2>/dev/null")` | `basename_pure(path)` |
| `lint.hexa` | 572 | `exec("basename '" + path + "' 2>/dev/null")` | `basename_pure(path)` |
| `lint.hexa` | 592 | `exec("basename '" + root + "' 2>/dev/null")` | `basename_pure(root)` |
| `post_edit.hexa` | 182 | `exec("basename '" + file_path + "'")` | `basename_pure(file_path)` |
| `pre_tool_guard.hexa` | 236 | `exec("dirname '" + file_path + "'")` | `dirname_pure(file_path)` |

**Risk/Benefit:**
- **Benefit:** Immediate subprocess elimination (6 calls saved). POSIX-only (no Windows support needed). Tested module.
- **Risk:** Gate files already handle errors with `2>/dev/null`; pure module returns `""` on invalid input (same outcome). Type safety: `path_pure` validates input type.

**Action:** ✅ **MIGRATE IMMEDIATELY** – 6 lines to replace, zero behavioral risk.

---

### 3. json_mini_pure.hexa

**Location:** `self/runtime/json_mini_pure.hexa` (158 lines, 5 pub fn)

| Export | Signature | Purpose | Replaces |
|--------|-----------|---------|----------|
| `json_escape_string_pure(s)` | `(str) -> str` | Escape chars + wrap in double quotes. Returns `"<escaped>"` (the full quoted literal). | N/A (gate/ doesn't build JSON strings) |
| `json_encode_int_pure(n)` | `(int) -> str` | Integer → decimal string. Returns `"0"` for non-ints. | N/A |
| `json_encode_string_array_pure(arr)` | `(array) -> str` | `["a","b","c"]` format. Each element escaped. | N/A (gate/ doesn't build JSON arrays) |
| `json_format_object_int_pure(keys, values)` | `(array, array) -> str` | `{"k":v,...}` from two parallel arrays. Non-string keys coerced. | N/A |
| `json_unescape_string_pure(s)` | `(str) -> str` | Inverse: expects quoted input `"..."`, returns unescaped raw string. Lenient (unknown escapes passed through). Does NOT handle `\uXXXX`. | N/A (gate/ reads JSON with `jq`, doesn't manually unescape) |

**Scope Note:** json_mini_pure is **encoding-only** (no decoding). Gate files use `jq` to **read/query** JSON (parsing + selection), not to build it. For parsing (reading files, merging, querying), would need `jq` equivalent or hand-rolled JSON parser—neither exists yet in pure hexa.

**Gate Files Using jq:**

| File | Line | Pattern | Complexity | Replacement |
|------|------|---------|-----------|-------------|
| `claude_statusline.hexa` | 19 | `jq -r -f '<jq_prog_file>' '<json_file>'` | jq script file (external) | No pure equivalent (need jq interpreter) |
| `lint.hexa` | 393 | `jq -r '.commands \| keys[]'` | Simple key extraction | Could use manual parsing if JSON structure known |
| `lint.hexa` | 450 | `jq -r --arg p '<val>' '(.format_presets // {}) \| has($p) \| tostring'` | Conditional lookup | No pure equivalent (complex jq syntax) |
| `lint.hexa` | 469 | `jq -r '.updated // ""'` | Default value selection | Simple field extraction |
| `lint.hexa` | 528 | `jq -r '.projects \| keys \| .[]'` | Array flattening | Manual iteration over known structure |
| `lint.hexa` | 1280 | `jq -r --arg d '<domain>' '.edges[] \| select(.from==$d) \| .to'` | Filtering + map | Complex selection (no pure equivalent) |

**Risk/Benefit:**
- **Benefit:** Encoding is straightforward (string escaping).
- **Risk:** Decoding/querying JSON requires a full parser. jq is small but powerful; hand-rolling JSON parser in hexa would be **100+ lines** for a tiny subset. Not viable for phase 2.

**Action:** ⏸️ **DEFER** – json_mini_pure is encoding-only. Gate files need jq for **parsing + querying**. Recommend:
  1. For simple field reads: hand-roll string search (e.g., `".field": "value"` → `str_index_of`).
  2. For complex queries: keep jq (no substitute yet).
  3. Future: consider a `json_mini_parser_pure` if jq becomes a bottleneck.

---

### 4. file_pure.hexa

**Location:** `self/runtime/file_pure.hexa` (122 lines, 6 pub fn for text I/O)

| Export | Signature | Purpose | Replaces |
|--------|-----------|---------|----------|
| `read_file_pure(path)` | `(str) -> str` | Read entire file as string. Returns `""` on error (no exception). | `exec("cat '<path>'")` – **used 3+ times in gate/** |
| `write_file_pure(path, content)` | `(str, str) -> bool` | Write content, overwriting. Returns success flag. | `exec("echo '...' > '<path>'")` (rare in gate/) |
| `append_file_pure(path, content)` | `(str, str) -> bool` | Append to file. Returns success flag. | `exec("echo '...' >> '<path>'")` (rare in gate/) |
| `file_exists_pure(path)` | `(str) -> bool` | Check if file exists. | `exec("test -f '<path>' && echo y || echo n")` – **used 3+ times in gate/** |
| `delete_file_pure(path)` | `(str) -> bool` | Delete file. Returns success. | N/A (gate/ doesn't delete) |
| `dir_exists_pure(path)` | `(str) -> bool` | Check if dir exists. | `exec("test -d '<path>' && echo y || echo n")` – **used occasionally** |

**Gate Files Using File I/O:**

| File | Line | Pattern | Replacement |
|------|------|---------|-------------|
| `hexa_url.hexa` | 178 | `read_file(p)` | Already using stdlib! (no exec call) |
| `lint.hexa` | 542, 1505, 1576, 1611 | `read_file(full)` | Already using stdlib! |
| `lint.hexa` | 2362 | `exec("cat '" + rulefile + "' 2>/dev/null")` | `read_file_pure(rulefile)` |
| `lint.hexa` | 3250 | `exec("cat '" + ps + "' 2>/dev/null")` | `read_file_pure(ps)` |
| `lint.hexa` | 3310, 3359 | `exec("cat '" + tb + "' 2>/dev/null")` | `read_file_pure(tb)` |
| `claude_statusline.hexa` | 16 | `exec("test -f '" + active + "' && echo y || echo n")` | `file_exists_pure(active)` – **but returns bool, not "y"/"n" string** |
| `post_edit.hexa` | 81, 115 | `exec("test -f '" + lint + "' && echo y || echo n")` | `file_exists_pure(lint)` – **adjust comparison** |

**Risk/Benefit:**
- **Benefit:** Many gate/ files already use `read_file()` (builtin). Pure version is drop-in. `file_exists` saves subprocess.
- **Risk:** Existing code checks for `"y"` / `"n"` strings; `file_exists_pure` returns `bool`. **Requires minor refactoring** at call sites.

**Action:** ✅ **MIGRATE WITH CAUTION** – 3 `cat` calls + 3 `test -f` calls can be replaced. Call sites using `== "y"` must change to `if file_exists_pure(path)` (no string comparison). Low risk, high visibility.

---

### 5. spawn_pure.hexa

**Location:** `self/runtime/spawn_pure.hexa` (123 lines, 6 pub fn)

| Export | Signature | Purpose | Replaces |
|--------|-----------|---------|----------|
| `rt_spawn(f, arg)` | `((T) -> U, T) -> handle` | Execute function immediately, store result, return handle. | N/A (cooperative, not background) |
| `rt_spawn2(f, a, b)` | `((T, U) -> V, T, U) -> handle` | 2-arg variant. | N/A |
| `rt_spawn3(f, a, b, c)` | Variant | 3-arg variant. | N/A |
| `rt_spawn_thunk(f)` | `(() -> T) -> handle` | Zero-arg spawn. | N/A |
| `rt_join(h)` | `(handle) -> T` | Retrieve stored result. | N/A (no gate/ usage) |
| `rt_join_all(handles)` | `(array) -> array` | Join all handles in order. | N/A |
| `rt_reset()` | `() -> void` | Clear task store. | N/A |

**Scope Note:** spawn_pure is **cooperative scheduling** (run-to-completion). Gate files use:
- `exec("nohup ... &")` – **background spawn** (async, returns immediately with PID).
- Not addressed by spawn_pure (which is sync-only, for parallel_for / prefetch).

**Gate Files Using Background Spawn:**
- `hexa_url.hexa` line 116: `exec(bg_cmd)` → dispatches `drill_bg_spawn.sh` in background.
- No other gate files spawn background tasks.

**Risk/Benefit:**
- **Benefit:** None for gate/ (spawn_pure is cooperative/sync, not async).
- **Risk:** Misuse risk if confused with threading.

**Action:** ❌ **NOT APPLICABLE** – spawn_pure is for parallel_for, not background subprocess. Gate files need `exec(&)` (shell backgrounding), which spawn_pure doesn't address. Keep shell call.

---

### 6. ffi_path_pure.hexa

**Location:** `self/runtime/ffi_path_pure.hexa` (96 lines, 1 pub fn)

| Export | Signature | Purpose | Replaces |
|--------|-----------|---------|----------|
| `ffi_extract_libname_pure(path)` | `(str) -> str` | Strip `lib` prefix and `.dylib`/`.so`/`.so.N` suffix from library path. Returns bare name or `""`. | N/A (gate/ doesn't manipulate library paths) |

**Risk/Benefit:**
- **Benefit:** Useful for FFI code, not gate/.
- **Risk:** None (orthogonal to gate/).

**Action:** ❌ **NOT APPLICABLE TO gate/** – Specific to FFI dlopen() calls. No gate/ usage.

---

### 7. string_pure.hexa (Bonus Coverage)

**Location:** `self/runtime/string_pure.hexa` (425 lines, 16 pub fn)

Relevant exports:

| Export | Signature | Purpose | Replaces |
|--------|-----------|---------|----------|
| `str_contains_pure(s, sub)` | `(str, str) -> bool` | Check substring presence (returns bool, not count). | `exec("grep -c '<pattern>' '<file>'")` – **used many times in gate/** but for **counting** (not just checking) |
| `str_trim_pure(s)` | `(str) -> str` | Strip leading/trailing whitespace. | N/A (gate/ uses `.trim()` builtin) |

**Note:** Gate files heavily use `grep -c` (count matches), which returns an integer. string_pure's `str_contains_pure` only checks presence (bool). **Not suitable for replacement** without additional line-counting logic.

**Action:** ⏸️ **DEFER** – string_pure covers trimming (already builtin `.trim()`), but grep counting would need a separate `str_count_matches_pure()` function (doesn't exist). Keep grep calls for now.

---

## Summary: Replacement Opportunities

### High Priority (Immediate Action)

| pure module | export | gate file : line | replaces | impact |
|-------------|--------|------------------|----------|--------|
| path_pure | `basename_pure(path)` | claude_statusline.hexa : 14 | `exec("basename '" + root + "'")` | 1 subprocess saved |
| path_pure | `basename_pure(path)` | lint.hexa : 359 | `exec("basename '" + path + "' 2>/dev/null")` | 1 subprocess saved |
| path_pure | `basename_pure(path)` | lint.hexa : 572 | `exec("basename '" + path + "' 2>/dev/null")` | 1 subprocess saved |
| path_pure | `basename_pure(path)` | lint.hexa : 592 | `exec("basename '" + root + "' 2>/dev/null")` | 1 subprocess saved |
| path_pure | `basename_pure(path)` | post_edit.hexa : 182 | `exec("basename '" + file_path + "'")` | 1 subprocess saved |
| path_pure | `dirname_pure(path)` | pre_tool_guard.hexa : 236 | `exec("dirname '" + file_path + "'")` | 1 subprocess saved |
| file_pure | `read_file_pure(path)` | lint.hexa : 2362 | `exec("cat '" + rulefile + "' 2>/dev/null")` | 1 subprocess saved |
| file_pure | `read_file_pure(path)` | lint.hexa : 3250 | `exec("cat '" + ps + "' 2>/dev/null")` | 1 subprocess saved |
| file_pure | `read_file_pure(path)` | lint.hexa : 3310 | `exec("cat '" + tb + "' 2>/dev/null")` | 1 subprocess saved |
| file_pure | `read_file_pure(path)` | lint.hexa : 3359 | `exec("cat '" + tb + "' 2>/dev/null")` | 1 subprocess saved |

**Total Immediate Savings:** 10 subprocesses.

### Medium Priority (With Refactoring)

| pure module | export | gate file : line | replaces | caveat |
|-------------|--------|------------------|----------|--------|
| file_pure | `file_exists_pure(path)` | claude_statusline.hexa : 16 | `exec("test -f '" + active + "' && echo y")` | Change `== "y"` to `if file_exists_pure(active)` |
| file_pure | `file_exists_pure(path)` | post_edit.hexa : 81 | `exec("test -f '" + lint + "' && echo y")` | Change comparison style |
| file_pure | `file_exists_pure(path)` | post_edit.hexa : 115 | `exec("test -f '" + lint + "' && echo y")` | Change comparison style |

**Total Medium Priority:** 3 subprocesses (3 refactoring sites).

### Deferred (No Pure Equivalent Yet)

| pattern | gate files | reason |
|---------|-----------|--------|
| `exec("jq ...")` (complex queries) | claude_statusline.hexa:19, lint.hexa:393,450,469,528,1280 + 12 more | jq requires full JSON parser; json_mini_pure is encoding-only. Hand-rolled parser would be 100+ lines. |
| `exec("grep -c ...")` (counting) | lint.hexa: 15+ uses | string_pure has `str_contains_pure` (bool), not `str_count_matches`. Need new function. |
| `exec("tail -n ... \| jq ...")` (JSONL read + query) | lint.hexa:3328,3345 | Requires JSONL line iteration + jq equivalent. Deferred to phase 2. |
| `exec("git rev-parse --show-toplevel")` | lint.hexa:12, claude_statusline.hexa:11, hexa_url.hexa:164 | No git equivalent in pure hexa. Keep shell call. |
| `exec("nohup ... &")` (background spawn) | hexa_url.hexa:116 | spawn_pure is cooperative (sync), not async. No replacement. Keep shell call. |

**Pattern:** jq, grep -c, and git-root have no pure equivalents. Likely won't change in phase 2/3.

---

## Non-Applicable Modules

| module | reason | gate usage |
|--------|--------|-----------|
| spawn_pure | Cooperative scheduling (sync), not async. Gate needs `exec(...&)`. | None applicable. |
| ffi_path_pure | FFI-specific (library name extraction). | None. |
| url_pure | URL encoding/decoding. Gate has inline `url_decode()` in hexa_url.hexa; no incoming URL-encoded params yet. | Potential future use. |
| json_mini_pure | Encoding-only (no parsing). Gate needs jq for queries. | None applicable (use jq instead). |

---

## Next Steps

### Phase 2 Recommendations

1. **Immediate:** Add `use "runtime/path_pure"` and `use "runtime/file_pure"` to all gate files.
   - Replace 10 `exec("basename")`, `exec("dirname")`, `exec("cat")` calls.
   - Expected time: ~30 min (mechanical, low risk).

2. **Medium term:** Refactor `file_exists_pure` call sites to use bool instead of string comparison.
   - 3 sites affected.
   - Expected time: ~15 min.

3. **Longer term (post-phase 2):**
   - If jq becomes a bottleneck, prototype `json_mini_parser_pure` (JSONL line iteration + simple field extraction).
   - If grep -c counting is high-frequency, add `str_count_matches_pure(haystack, pattern)` to string_pure.
   - Consider git-root alternative (e.g., env var fallback, or `use "self/runtime/git_meta_pure"` if built).

---

## Appendix: grep -c Patterns in gate/

For future `str_count_matches_pure()` support:

```
/Users/ghost/core/hexa-lang/gate/lint.hexa:211:    let py_call = ... exec("grep -c 'python3 -c' ...")
/Users/ghost/core/hexa-lang/gate/lint.hexa:269:    let bad = ... exec("grep -c '@allow-roadmap-ext' ...")
/Users/ghost/core/hexa-lang/gate/lint.hexa:345:    let marker = ... exec("grep -c '@allow-project-hexa' ...")
/Users/ghost/core/hexa-lang/gate/lint.hexa:369:    let bypass = ... exec("grep -c '@allow-cmds-hardcode' ...")
/Users/ghost/core/hexa-lang/gate/lint.hexa:577:    let legacy = ... exec("grep -c '\"_legacy\": true' ...")
/Users/ghost/core/hexa-lang/gate/lint.hexa:832:    let marker = ... exec("grep -c '@allow-complexity-growth' ...")
/Users/ghost/core/hexa-lang/gate/lint.hexa:887:    let has = ... exec("... grep -ciE '(CC[- ]?BY|Creative Commons)' ...")
/Users/ghost/core/hexa-lang/gate/lint.hexa:934:    let marker = ... exec("grep -c '@allow-non-filter' ...")
/Users/ghost/core/hexa-lang/gate/lint.hexa:967:    let marker = ... exec("grep -c '@allow-no-verify' ...")
/Users/ghost/core/hexa-lang/gate/lint.hexa:997:    let m = ... exec("grep -c '@allow-paper-canonical' ...")
/Users/ghost/core/hexa-lang/gate/lint.hexa:2021:    let m = ... exec("grep -c '@allow-generic-verify' ...")
/Users/ghost/core/hexa-lang/gate/lint.hexa:2038:    let m = ... exec("grep -c '@allow-missing-data' ...")
/Users/ghost/core/hexa-lang/gate/lint.hexa:2103:    let m = ... exec("grep -c '@allow-no-runtime' ...")
/Users/ghost/core/hexa-lang/gate/lint.hexa:2225:    let marker = ... exec("grep -c '@allow-local-heavy' ...")
/Users/ghost/core/hexa-lang/gate/lint.hexa:2805:    let has_marker = ... exec("grep -c '@enforce-sandbox' ...")
/Users/ghost/core/hexa-lang/gate/lint.hexa:2834:    let bypass = ... exec("grep -c '@no-sandbox' ...")
... (20+ total occurrences)
```

Most grep -c patterns are looking for **annotations** (strings starting with `@`) or **keywords** in files. Could be replaced with `str_count_lines_containing_pure(content, substring)` if file is read first with `read_file_pure()`.

---

**Audit Complete.** Ready for phase 2 migration planning.
