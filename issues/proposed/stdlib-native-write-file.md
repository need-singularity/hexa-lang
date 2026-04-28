# stdlib: native `write_file` / `read_file` API to retire ARG_MAX heredoc-via-cmdline pattern

## Summary

Provide a native stdlib I/O pair so callers do not have to route file content
through `exec("cat <<EOF ... EOF > path")` or `exec("printf '%s' ... > path")`,
both of which silently fail when the body crosses the OS `ARG_MAX` cliff.

**Anchor**: hive raw 144 (`exec-cmd-length-guard`, NEW 2026-04-28, severity warn).
**Workaround pattern label**: `exec-wrap-native` (raw 159 NARROW lint matrix row 5).
**Signal kind**: stdlib-gap.
**Severity to request**: blocker — eliminates the entire ARG_MAX cliff failure
class for content I/O once landed.

## Motivation

The hexa stdlib currently has no native file I/O for arbitrary-size content.
Every cycle that wants to write a file >~64KB has to push the body through a
shell command line:

```hexa
let cmd = "cat > '" + path + "' <<'__HXC_EOF__'\n" + body + "\n__HXC_EOF__"
let _r = exec(cmd)   // raw 12 silent-error-ban violation: rc discarded
```

This pattern is everywhere in the hive/ + nexus/ code base because there is no
alternative. It is wrong in two ways:

1. **Hard cliff at `ARG_MAX − envp_overhead`.** On Darwin 25.4.0 the empirical
   cliff measured by sub-agent `adc2e734` (2026-04-28) is **1,044,361 bytes**.
   Once the rendered command crosses that boundary, `popen` returns
   `pclose=32512` (`E2BIG`, `127<<8`) and the file is never written. The
   call site has no idea.
2. **Return code discarded.** Idiomatic call-sites use `let _r = exec(cmd)`
   because the only way to surface the error is to read `_r` and parse a
   shell exit code, which everyone forgets. This makes the cliff *silent*
   from hexa's perspective even though the OS reported it loudly.

A native I/O pair pushes the I/O down into the runtime where it can use a real
`write(2)` loop — no `ARG_MAX`, no `popen`, no shell quoting.

## Repro

Failing call site lives at:

```
/Users/ghost/core/hive/tool/hxc_convert.hexa:191-192
```

The function is `_write_file(path, body)`. It builds a heredoc command and
pipes it to `exec`. For any input where `body` exceeds the cliff, the file is
silently empty / missing and downstream cycles fail at the consumer with a
"file not found" or "unexpected EOF" error far from the actual cause.

Empirical reproduction (recorded in sub-agent task
`/private/tmp/claude-501/-Users-ghost-core-anima/e8d30a3e-9b9f-4aec-b681-348a3ba457ae/tasks/adc2e734f2badf624.output`):

1. Fill `body` to 1,044,362 bytes (1 byte over the Darwin 25.4.0 cliff).
2. Call `_write_file("/tmp/x", body)`.
3. Observe `pclose=32512` and an empty / non-existent `/tmp/x`.

## Proposed signature

```hexa
// Returns 0 on success, non-zero rc on failure (errno-shaped).
fn stdlib_write_file(path: string, body: string) -> int

// Returns the file body. Caller checks `len(...)` and `last_io_error()`
// (or the equivalent runtime hook) to disambiguate empty-file vs error.
fn stdlib_read_file(path: string) -> string
```

Backed by a `write(2)` / `read(2)` loop in `self/runtime.c`. No shell hop. No
quoting concerns. Honors `umask` like every other stdlib I/O primitive.

## Migration

Once this lands:

- `_write_file` heredoc helpers across hive/ + nexus/ collapse to a one-line
  call to `stdlib_write_file`.
- The `exec-wrap-native` row of the raw 159 lint matrix (raw159_upstream_lint
  P5 detector) will start firing on every remaining call site, gating cleanup.
- Raw 144's `exec-cmd-length-guard` becomes vacuously satisfied for the I/O
  case (the only safe write path is the native one).

## Out of scope (follow-ups)

- Streaming I/O (`open`/`read`/`close`) for files larger than RAM — separate
  proposal; the bulk-buffer signature above covers the workaround retirement.
- Atomic `write-temp + rename` — useful but orthogonal; can be layered on top.

## Evidence anchors

- hive `.raw` L4727-4783 — raw 144 registration.
- `/Users/ghost/core/hive/tool/hxc_convert.hexa:191-192` — surviving heredoc
  call site (Bug 1 audit residual).
- Sub-agent `adc2e734` Darwin 25.4.0 cliff measurement
  (`/private/tmp/claude-501/...adc2e734f2badf624.output`).
- `/Users/ghost/core/hive/tool/raw159_upstream_lint_minimal.hexa` — P5
  `_detect_exec_wrap_native` detector that will retire when this lands.
