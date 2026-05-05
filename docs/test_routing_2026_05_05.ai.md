---
schema: hexa-lang/docs/test_routing/ai-native/1
last_updated: 2026-05-05
phase: documentation
status: stable_mechanism_documented
direction: B (document; A unification deferred)
predecessors:
  - tool/docs/resolver_bypass.md@2026-05-04
  - docs/resolver_routing_fix_20260427.md
  - docs/stdlib_yaml_rfc_2026_05_05.md (sister-test convention)
  - tool/install_interp_shim.hexa::iss_fallback_shim_content
  - tool/resolver_bypass_check.sh.in (marker SSOT)
gap_id: hexa-lang/runtime/test-surface-routing-disparity
discovered_by: Agent L (file_atomic stdlib work, 2026-05-05)
backward_compat: required
destructive: 0
sub_gaps_surfaced:
  - SG-1 streaming-popen sentinel-strip on no-trailing-newline child stdout (independent)
exemplar_fixture: self/test/_routing_exemplar_canonical.hexa
canonical_pattern: dual-surface (stdlib --selftest + self/test structural sister)
---

# hexa-lang Test Routing — the two-surface contract

When a test author drops a `.hexa` file under `self/test/` they get a different
runtime than when the same content sits at `self/stdlib/<mod>.hexa --selftest`
or at `/tmp/<probe>.hexa`. The divergence is not path-determined at the
resolver layer; it is **marker-determined**, but path-correlated by convention
because the bare-Mac pipelines tend to carry markers and the structural tests
under `self/test/` deliberately do not. This doc names the two surfaces, gives
a canonical dual-surface authoring pattern, and surfaces an unrelated sub-gap
in the streaming-popen sentinel logic.

## §1 The routing decision (single source of truth)

The decision lives in `~/.hx/bin/hexa::_darwin_bypass_check` (off-limits canonical)
and is mirrored at the interp-shim layer in `tool/install_interp_shim.hexa::iss_fallback_shim_content`.
The marker scan body is the SSOT shared by both consumers:
`tool/resolver_bypass_check.sh.in`. The lint at `tool/lint_resolver_bypass.hexa`
fails if the two consumers drift.

**There is no `self/test/`-specific routing rule.** The resolver's decision is:

1. `HEXA_RESOLVER_NO_REROUTE=1` → real bare-Mac exec (one-shot bypass; recursion guard).
2. `HEXA_LOCAL=1` / `HEXA_LOCAL_NO_CAP=1` / `HEXA_STAGE0_NO_CAP=1` → real bare-Mac exec.
3. Inside a container (`/.dockerenv` or `HEXA_IN_CONTAINER=1` or `HEXA_RESOLVER_IN_CONTAINER=1`) → real exec inside the container.
4. cheap-subcmd carve-out (`--version`, `--help`, `lsp`, `format`, …) → real bare-Mac exec.
5. metadata-only argv carve-out (raw#103) → real bare-Mac exec.
6. `_cwd_real` sync-root short-circuit (cwd under host/linux mirror root) → real bare-Mac exec.
7. **Marker scan on leading `*.hexa` argv** — first 100 lines of the file searched for:
   - `@resolver-bypass(reason="…")` → real bare-Mac exec
   - `@resolver-pure(reason="…")` → real bare-Mac exec
   - `@omega-saturation-exempt(… chflags|launchctl|sandbox-exec|DYLD|SBPL|plutil|codesign …)` → real bare-Mac exec
   - `@omega-saturation-exempt(… operational utility|pure-text|single-pass|pure parse|read-only …)` → real bare-Mac exec
8. anima ssh probe (4 hosts, neg-cache, force-up/down env vars) → external host on success.
9. **Fall-through** → docker-landing into the persistent `hexa-exec` container.

So the path string `self/test/foo.hexa` does **not** itself drive routing. What
correlates with the path is authorial intent: structural tests written under
`self/test/` typically carry no marker (they are not "darwin-native" pipelines)
and so they fall through to the docker landing every time. That is the surface
Agent L (and Agent K, see §3) bumped into.

## §2 The two surfaces — what each one provides

| Property                               | Surface A: docker landing             | Surface B: bare-Mac bypass            |
|----------------------------------------|---------------------------------------|---------------------------------------|
| Where files typically land             | `self/test/*.hexa` (no marker)        | `self/stdlib/<mod>.hexa --selftest`, marker-bearing tools |
| `/tmp/<probe>.hexa` lands here when…   | no marker present                     | marker present, or `HEXA_LOCAL=1`     |
| Host                                   | `gcr.io/distroless/cc-debian12` + busybox-static | bare macOS host                  |
| Filesystem                             | container ext4 (mounts vary)          | host APFS                             |
| Available coreutils                    | busybox-static applets only           | full BSD/coreutils as installed       |
| `shasum`                               | absent                                | present (`/usr/bin/shasum`)           |
| `mktemp -d`                            | busybox `mktemp -d` only              | BSD mktemp (`-d` works, `-t` template differs) |
| `stat`                                 | busybox `stat` (Linux flags)          | BSD `stat -f` (Mac flags)             |
| `HEXA_STDLIB_ROOT`                     | `/usr/local/bin/self/stdlib` (baked in image) | unset by default; resolver reads `HEXA_LANG` |
| `HEXA_LANG`                            | unset                                 | `~/core/hexa-lang` if exported        |
| `LD_PRELOAD=/opt/hexa/native_gate.so`  | YES (raw#7 OS enforce)                | NO (advisory only on macOS due to SIP) |
| `HEXA_RESOLVER_IN_CONTAINER=1`         | YES                                   | NO                                    |
| `HEXA_RESOLVER_NO_REROUTE=1`           | YES                                   | YES (set by resolver before re-exec)  |
| Determinism for CI                     | high (image-pinned)                   | host-dependent                        |
| `use "self/stdlib/<mod>"` resolves     | via `HEXA_STDLIB_ROOT` (baked stdlib) | via `HEXA_LANG`                       |

### §2.1 Why imports look like they "get silently stripped"

After raw#92 (`self/module_loader.hexa:447`) the loader fails loud (`exit(2)`)
on any unresolved `use "…"`. The historical "silent strip" was the pre-raw#92
warn-only path. If a current test under `self/test/` reports its imports
"missing", the cause is one of:

- The container has the stdlib at `HEXA_STDLIB_ROOT=/usr/local/bin/self/stdlib`
  but the test imported `use "stdlib/<mod>"` against an outdated baked stdlib
  (image rebuild needed).
- The test imported a relative path like `use "self/test/helper"` which the
  container cannot see (only `self/module_loader.hexa` and `self/stdlib/` are
  bind-baked into the runtime image — see `docker/runner/Dockerfile:225-228`).
- The test imported a project-rooted path (`use "tool/foo"` outside stdlib)
  which the container's filesystem layout does not provide.

The mac surface always sees the full `~/core/hexa-lang/` checkout via
`HEXA_LANG`. The container only sees what was baked in. This is the real
content-of-the-disparity — not "the resolver eats imports".

## §3 The canonical dual-surface authoring pattern (Direction B)

We pick **Direction B (document the disparity)** over Direction A (unify by
routing `self/test/` through bare-Mac). Rationale:

- Direction A would change runtime semantics for ~250+ existing files under
  `self/test_*.hexa` and `self/test/`, breaking the docker-image-pinned CI
  determinism that today's test fleet implicitly relies on.
- Direction A would also weaken the LD_PRELOAD enforcement layer (raw#7
  F5/F6 + raw#6 F4 + raw#8 + raw#13) for the test fleet — markers exist
  precisely so darwin-native pipelines can opt out; flipping the default
  would make every test an opt-out.
- Direction B costs only documentation + an exemplar fixture; the
  per-author-decision overhead is a single-line marker choice or the
  dual-surface pattern below.

### §3.1 Pattern: dual-surface stdlib test

```text
self/stdlib/<mod>.hexa            ← stdlib body
                                    has a built-in `--selftest` argv branch
                                    (canonical runtime conformance)

self/test/<mod>_*.hexa            ← structural sister tests
                                    parse/emit/byte-eq verification only
                                    container-safe (no host coreutils)
```

Authoring steps:

1. **Stdlib selftest** — embed an argv branch in `self/stdlib/<mod>.hexa`:

   ```hexa
   if len(args()) >= 3 && args()[2] == "--selftest" {
       _selftest_main()
       exit(0)
   }
   ```

   Run with: `hexa run self/stdlib/<mod>.hexa --selftest`.
   Add `@resolver-bypass(reason="darwin-native — stdlib selftest must see host fs/clock for fixture round-trip")` only if you actually need host coreutils. For pure parse/round-trip tests, omit the marker (the container has busybox + the baked stdlib + no `LD_PRELOAD` veto on read syscalls).

2. **Structural sister test** — under `self/test/<mod>_<aspect>.hexa`:

   - Use only `use "stdlib/<mod>"` or `use "self/stdlib/<mod>"` — both forms
     resolve via `HEXA_STDLIB_ROOT` in-container and via `HEXA_LANG` on host.
   - Avoid host-only commands (`shasum`, `stat -f`, BSD-only mktemp templates).
     If hashing is needed, prefer the in-language pure helpers
     (`stdlib/<crypto>.hexa` family) or accept that the test is structural
     not byte-exact.
   - Print a unique sentinel on PASS (e.g. `__<MOD>_TEST__ PASS`) so the
     parent runner can grep stdout.

3. **Document the disclosure** in the stdlib's RFC / ai.md sister:
   ```
   - Test pattern: `self/test/<mod>_<aspect>.hexa` (structural sister; runtime
     conformance lives in `self/stdlib/<mod>.hexa --selftest`).
   ```

### §3.2 When to deviate

- **Force bare-Mac for a structural test**: add the marker explicitly.
  Example: `@resolver-bypass(reason="darwin-native — exercises BSD stat -f")`.
- **Force docker for a marker-bearing tool's selftest**: invoke with
  `HEXA_RESOLVER_NO_DARWIN_BYPASS=1 hexa run self/stdlib/<mod>.hexa --selftest`.
- **Run identically on both surfaces**: write the test to the busybox
  intersection (no host-only commands). The exemplar fixture in §4 verifies
  this contract.

## §4 Exemplar fixture

`self/test/_routing_exemplar_canonical.hexa` (created alongside this doc) is
a minimal test that:

- Imports `use "stdlib/io"` (resolves on both surfaces).
- Reads its own routing-mode signature from environment (`HEXA_RESOLVER_IN_CONTAINER`,
  `HEXA_LANG`, `HEXA_STDLIB_ROOT`, `uname -s`) and prints a one-line summary.
- Performs a content-test that uses only the busybox intersection (no `shasum`,
  no BSD `stat -f`, no `mktemp -t`).
- Exits 0 on PASS with sentinel `__ROUTING_EXEMPLAR__ PASS surface=<A|B>`.

Verification (both should print PASS, only `surface=` differs):

```
$ hexa run self/test/_routing_exemplar_canonical.hexa
__ROUTING_EXEMPLAR__ PASS surface=A   # docker-landing (default for marker-less file)

$ HEXA_LOCAL=1 hexa run self/test/_routing_exemplar_canonical.hexa
__ROUTING_EXEMPLAR__ PASS surface=B   # bare-Mac (forced via env var)
```

Adding `@resolver-bypass(reason="…")` to the file's header would also flip it
to surface B without the env var.

## §5 Sub-gaps surfaced (independent of routing)

### SG-1 Streaming-popen stdout swallow on no-trailing-newline child

Observed by Agent K via the git stdlib. Independent of the routing disparity.

**Mechanism** (see `self/main.hexa::run_stream_with_sentinel` lines 1582-1602
and the raw#159 commentary at 1605-1629):

The streaming forwarder appends each `fgets()` line to stdout, then on EOF
inspects `__hexa_run_stream_buf` (the unflushed tail). If that tail
`starts_with("__HEXA_RC=")`, it is consumed for rc parsing. **If the child's
final stdout emit ends without `\n`** (e.g. `print("hello")` rather than
`println("hello")`), `fgets()` concatenates `hello__HEXA_RC=0\n` into a single
trailing line. `starts_with("__HEXA_RC=")` then returns `false`, the legacy
fallback (`if len(last) > 0 { print(last) }`) prints the entire merged line —
including the sentinel — to the user's stdout, breaking `JSON.parse(stdout)`
callers.

The opposite asymmetry: a child that emits empty stdout terminating exactly
at the sentinel can have its rc parsed correctly but the final pre-sentinel
content swallowed if it ended without `\n`. raw#159's `--no-sentinel` mode
(landed) is the documented escape hatch. Recommendation: surface this in
`stdlib/proc.hexa` callers' docs and consider promoting `--no-sentinel` /
`HEXA_NO_SENTINEL=1` to the default for `popen_lines()`-style helpers when
they ship in RFC-001's full landing.

**Tracking**: file as a separate gap rather than blocking this routing doc.

## §6 Future Direction A unification cycle (deferred)

Direction A (route `self/test/` through bare-Mac unconditionally) remains the
correct long-term move once the following preconditions are met:

1. The test fleet runs through a CI harness that explicitly opts test files
   into the docker image when image-pinned determinism matters (i.e., the
   mac-bypass becomes the default and docker becomes the opt-in for tests
   that exercise LD_PRELOAD enforcement or distroless-binary parity).
2. The `LD_PRELOAD=/opt/hexa/native_gate.so` enforcement gains a host-side
   equivalent (`DYLD_INSERT_LIBRARIES` once SIP is solved on developer macs,
   or an in-language gate enforced by the parser/loader).
3. A `@test-surface(docker|host|either)` marker (or extension of
   `@resolver-bypass`) lets the test author state their intent explicitly,
   making the routing decision auditable rather than path-coincidental.

Until then, Direction B (this doc) is the user-visible contract.

## §7 Cross-link

- `tool/docs/resolver_bypass.md` — the marker mechanism canonical
- `docs/resolver_routing_fix_20260427.md` — neg-cache + escape-hatch design
- `docs/stdlib_yaml_rfc_2026_05_05.md` §2.3 / §7 — sister-test convention exemplar
- `tool/resolver_bypass_check.sh.in` — marker-scan SSOT
- `tool/install_interp_shim.hexa::iss_fallback_shim_content` — interp-layer mirror
- `tool/lint_resolver_bypass.hexa` — drift detector
- `docker/runner/Dockerfile:225-228` — what the container bakes from the host repo
- `self/module_loader.hexa:140-198` (resolution rules) and `:447-478` (raw#92 fail-loud)
- `stdlib/resolver.hexa` — programmatic introspection (`resolver_in_container()`, `resolver_scrub_env_prefix()`)
