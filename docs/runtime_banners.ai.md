---
schema: hexa-lang/docs/ai-native/1
last_updated: 2026-05-05
status: spec — partial implementation landed in hexa_interp shim; ~/.hx/bin/hexa side pending next deploy
purpose: env var + CLI flag contract for suppressing resolver/shim routing banners that pollute downstream parser stdout/stderr
sister_docs:
  - hexa-lang/tool/docs/resolver_bypass.md
  - hexa-lang/docs/hexa_url_complete_support_upstream_spec_2026_05_05.ai.md  (gap #4)
  - hexa-lang/tool/install_interp_shim.hexa
---

# Runtime banner suppression — `HEXA_QUIET_RESOLVER` + `--quiet-resolver`

## Problem

Every `hexa` invocation potentially emits 1–3 lines of routing metadata before
the user's program runs. The known emitters and shapes are:

| Layer                        | Banner shape (example)                                                | Channel | Source                                                             |
|------------------------------|-----------------------------------------------------------------------|---------|--------------------------------------------------------------------|
| `~/.hx/bin/hexa` (resolver)  | `hexa-resolver: route=darwin-bypass reason=darwin-native marker present` | stderr  | `~/.hx/bin/hexa` (release-distributed; upstream-deployed)         |
| `~/.hx/bin/hexa` (resolver)  | `hexa-resolver: route=docker reason=mac_safe_landing image=…`           | stderr  | `~/.hx/bin/hexa`                                                  |
| `~/.hx/bin/hexa` (NO_REROUTE)| `# resolver: NO_REROUTE honored, executing $REAL_HEXA directly`        | stderr  | `~/.hx/bin/hexa`                                                  |
| `build/hexa_interp` shim     | `# shim: NO_REROUTE honored, executing $REAL directly`                 | stderr  | `tool/install_interp_shim.hexa::iss_fallback_shim_content` (this repo) |

These leak into pipelines that consume `hexa run …` output. Sentinel-line
parsers (see `hive/sync/bootstrap.hexa::_apply_kind`) and JSON-line consumers
all carry per-call `grep -v '^hexa-resolver:'` boilerplate; the contamination is
called out in `docs/hexa_url_complete_support_upstream_spec_2026_05_05.ai.md`
section 2.3 as upstream gap #4.

## Contract (this doc is the SSOT)

### Env var: `HEXA_QUIET_RESOLVER`

When set to `1`, every banner-emitting layer MUST suppress its routing/shim
notes. Set values other than `1` (including unset) preserve current default
behavior.

The variable is **inherited across one hop of re-exec** (the resolver and
shim layers re-exec `$REAL_HEXA` / `$REAL`, and `HEXA_QUIET_RESOLVER` flows
through unchanged so the inner layer is also quiet). It is NOT cleared on
re-exec (unlike `HEXA_RESOLVER_NO_REROUTE`, which is one-hop-scoped to avoid
silent recursion bypass).

### CLI flag: `--quiet-resolver`

Any `hexa` invocation MAY pass `--quiet-resolver` as an argv token (anywhere
in the argv list). The banner-emitting wrapper layers MUST:

1. Detect the flag.
2. Set `HEXA_QUIET_RESOLVER=1` for itself and all child processes (export).
3. Strip the flag from `$@` before exec'ing the real binary (so the user's
   program does not see it).

### Stderr-only with category prefix

Even when banners DO emit (default behavior, `HEXA_QUIET_RESOLVER` unset),
they MUST go to **stderr only**, never stdout, and SHOULD carry a category
prefix `[resolver-banner] ` so downstream callers can filter via either:

```
2>/dev/null                                 # drop all stderr (lossy)
2> >(grep -v '^\[resolver-banner\] ' >&2)   # drop only resolver banners
```

The legacy emitters in `~/.hx/bin/hexa` use `hexa-resolver:` and `# resolver:`
as de-facto category prefixes — the new contract is to add `[resolver-banner] `
in front of all such lines so a single grep pattern works across both layers.
During the transition, callers may need to grep both `^hexa-resolver:` and
`^\[resolver-banner\]`.

## Implementation status

| Layer                         | Status                                       |
|-------------------------------|----------------------------------------------|
| `tool/install_interp_shim.hexa` (interp shim) | **landed 2026-05-05** — `HEXA_QUIET_RESOLVER` + `--quiet-resolver` flag honored; banner emits `[resolver-banner] shim: …` when not quiet. Re-run `hexa tool/install_interp_shim.hexa` to regenerate `build/hexa_interp` with the new logic. |
| `~/.hx/bin/hexa` (resolver)   | **pending** — release-distributed; next upstream deploy must adopt this contract. The resolver currently emits `hexa-resolver: route=…` lines unconditionally. Until the deploy, callers must continue to pre-strip those lines. |

## Verification

Once both layers honor the contract:

```bash
# stdout MUST be free of resolver/shim banner lines (but legitimate program
# output remains).
HEXA_QUIET_RESOLVER=1 hexa run script.hexa --selftest 2>/dev/null

# Equivalent CLI form.
hexa run --quiet-resolver script.hexa --selftest 2>/dev/null

# Default behavior unchanged — banners still emit on stderr.
hexa run script.hexa --selftest        # banners visible on stderr
```

In the interim (resolver-side not yet updated), callers SHOULD use:

```bash
HEXA_QUIET_RESOLVER=1 hexa run script.hexa --selftest 2>/dev/null
```

`HEXA_QUIET_RESOLVER=1` silences the shim layer; `2>/dev/null` strips the
remaining `hexa-resolver:` banners from `~/.hx/bin/hexa`. This loses
legitimate stderr from the user program — a known transitional cost
documented under upstream gap #4.

**`--quiet-resolver` flag note (transition).** The CLI flag form requires
flag-stripping at every layer that consumes argv. Until `~/.hx/bin/hexa`
adopts the contract, the flag will pass through as positional argv and the
user program will see it (likely causing argparse failure). Until then,
prefer the env-var form `HEXA_QUIET_RESOLVER=1`.

## Related work

- `tool/docs/resolver_bypass.md` — routing markers + override env var matrix.
- `stdlib/resolver.hexa` — programmatic resolver introspection (route label,
  in-container probe, scrub-prefix). Does not emit banners.
- `docs/hexa_url_complete_support_upstream_spec_2026_05_05.ai.md` § 2.3 — gap
  enumeration, motivating context.
