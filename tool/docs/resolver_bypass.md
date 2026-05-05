# `@resolver-bypass` — Mac→Docker resolver carve-out

**Status:** stable mechanism (promoted from C10 ad-hoc workaround, 2026-05-04).

The `hexa` resolver at `~/.hx/bin/hexa` (raw 44 hard-landing) auto-routes any
`*.hexa` invocation on Darwin into the persistent `hexa-exec` Linux container.
This guarantees CPU/RAM caps that macOS lacks, but breaks any pipeline whose
output is **architecture-bound to the host** (e.g. `cc -O2 -dynamiclib`
produces Mach-O which the Linux container cannot dlopen).

`@resolver-bypass(reason="...")` is the documented opt-out: a header comment
that tells the resolver "exec this script directly on the bare Mac, do not
land in Docker".

## Routing rule (single source of truth)

The resolver scans the **first 100 lines** of every `*.hexa` argv on Darwin
hosts. Bypass triggers if any of these match (see `~/.hx/bin/hexa`
`_darwin_bypass_check` ≈ lines 195–231 + the mirror in
`tool/install_interp_shim.hexa::iss_fallback_shim_content`):

| Marker                                          | Tier               | Use case                                       |
|-------------------------------------------------|--------------------|------------------------------------------------|
| `@resolver-bypass(...)`                         | explicit (highest) | host-bound pipelines (Mach-O, JIT, codesign)   |
| `@resolver-pure(...)`                           | explicit           | pure parse / read-only / single-pass tools    |
| `@omega-saturation-exempt(... chflags ...)`     | keyword            | host-only OS utilities (chflags, launchctl, sandbox-exec, DYLD, SBPL, plutil, codesign) |
| `@omega-saturation-exempt(... pure-text ...)`   | keyword            | pure parse / single-pass / read-only / operational utility |

When **any** of the above triggers on the leading argv `*.hexa`, the resolver
re-execs `$REAL_HEXA` with `HEXA_RESOLVER_NO_REROUTE=1`. The interp shim
(`build/hexa_interp` fallback content) applies the **same** rule one layer
deeper for callers that arrive directly at the interp without going through
`~/.hx/bin/hexa` (e.g. `cmd_run` flatten).

## Container-side environment contract

When a process **does** land in the container, the resolver sets:

```
HEXA_RESOLVER_NO_REROUTE=1     # recursion guard
HEXA_RESOLVER_IN_CONTAINER=1   # advertises container-mode to children
LD_PRELOAD=/opt/hexa/native_gate.so   # set by container image at runtime
```

User code that nests another `hexa` invocation **must scrub these vars** if
the inner call needs the host (Mac) for arch-bound work. The canonical scrub
seen in C10 (`tool/jit_selftest.hexa`):

```hexa
let env_clean = "env -i PATH=/usr/local/bin:/usr/bin:/bin HOME=" + home + " "
exec(env_clean + hexa_real + " run " + ...)
```

Or, equivalently, prefer the new stdlib helper:

```hexa
use "stdlib/resolver"
if resolver_in_container() {
    // re-route to bare Mac, scrubbing routing env
    exec(resolver_scrub_env_prefix() + hexa_real + " run ...")
}
```

## Override env vars (runtime, no source edit needed)

| Env var                              | Effect                                         |
|--------------------------------------|-----------------------------------------------|
| `HEXA_RESOLVER_NO_REROUTE=1`         | bypass once (in-container re-entry guard)     |
| `HEXA_RESOLVER_NO_DARWIN_BYPASS=1`   | disable marker check (force docker)           |
| `HEXA_RESOLVER_NO_DARWIN_MARKER=1`   | disable cheap-subcmd carve-out                |
| `HEXA_RESOLVER_NO_METADATA_BYPASS=1` | disable metadata-only argv carve-out (raw#103)|
| `HEXA_LOCAL=1`                       | emergency Mac-local exec, skip all probes     |
| `HEXA_RESOLVER_IN_CONTAINER=1`       | (container ↔ child contract; do not set on host) |
| `HEXA_SHIM_NO_DARWIN_LANDING=1`      | interp-shim only: skip darwin landing rule    |
| `HEXA_QUIET_RESOLVER=1`              | suppress resolver/shim routing banners (see `docs/runtime_banners.ai.md`) |

## Authoring rule

1. Place the marker in a comment within the first 100 lines (header zone).
2. Form: `@resolver-bypass(reason="<why this script must not land in Docker>")`.
3. Reason should name the host-bound side effect (Mach-O, codesign, chflags,
   launchctl, sandbox-exec, DYLD, plutil, …) **or** state "darwin-native"
   for catch-all subprocess-safe local pipelines.
4. If the script spawns child `hexa` invocations whose argv lacks the marker
   (e.g. pre-flattened tmp file written by `module_loader`), scrub
   `HEXA_RESOLVER_IN_CONTAINER` + `LD_PRELOAD` in the child env (see C10
   pattern above). This is the single most-common bypass-incomplete bug.

## Observability

Every routing decision logs to stderr with a `hexa-resolver:` prefix:

```
hexa-resolver: route=darwin-bypass reason=darwin-native marker present
hexa-resolver: route=docker reason=mac_safe_landing image=hexa-runner:latest …
```

Filter with `2>&1 | grep '^hexa-resolver:'` for routing audit.

To **suppress** banner output for downstream parser hygiene, set
`HEXA_QUIET_RESOLVER=1` or pass `--quiet-resolver` on the `hexa` argv. See
`docs/runtime_banners.ai.md` for the full contract (env propagation, flag
stripping, stderr category prefix, layer-by-layer status).

## See also

- `~/.hx/bin/hexa` (canonical resolver, raw 44)
- `tool/install_interp_shim.hexa::iss_fallback_shim_content` (interp-layer mirror)
- `stdlib/resolver.hexa` (programmatic introspection)
- `stdlib/test/test_resolver.hexa` (selftest verifying the bypass mechanism)
- `docs/resolver_routing_fix_20260427.md` (raw#84 SWR + pure marker design notes)
