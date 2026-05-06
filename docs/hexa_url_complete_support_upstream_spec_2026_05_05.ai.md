---
schema: hexa-lang/docs/ai-native/1
last_updated: 2026-05-06
status: catalog (re-framed) — categorical hexa-lang upstream support items for the hexa:// + invisible-sync ecosystem
purpose: SSOT catalog of hexa-lang capabilities that the hexa:// dispatcher and the cross-host invisible-sync design depend on; per-item closed/open status with on-disk evidence
sister_docs:
  - hexa-lang/gate/hexa_url_core.ai.md
  - hexa-lang/gate/hexa_url_modules.ai.md
related_ssot:
  - hive/docs/hexa_sync_invisible_design_2026_05_04.ai.md
  - hive/docs/lsregister_url_handler_plan_2026_05_05.ai.md
  - hive/spec/sync_registry.spec.yaml
  - hive/spec/mk2_ecosystem_catalog.spec.yaml § component_18 (invisible_sync_mk2)
  - hive/spec/mk2_apex.spec.yaml § inventory.12_invisible_sync
authoring_note: |
  An earlier draft was cited from hive specs and the lsregister plan as a numbered
  10-gap list ("Closes 9/10 hexa-lang upstream gaps"); the source enumeration was
  not preserved end-to-end. This document re-frames the surface as a categorical
  catalog (no false-precision numbering) and is the canonical SSOT going forward.
  Two items still carry the legacy gap-1 and gap-10 numbering because the
  lsregister plan's subtask split (S-22.1..S-22.4) references them by number;
  those identifiers are preserved as anchors only, not as an authoritative count.
---

# hexa-lang upstream support catalog — for hexa:// and invisible-sync

This document is the single SSOT for hexa-lang capabilities the hexa:// dispatcher
and the invisible-sync design rely on. Each item is either CLOSED (capability
present on disk; cite supplied) or OPEN (capability absent; honest cite of the
workaround or deferral).

The previously cited numbered gap list does not survive in any source document.
Items below are grouped by category. The two items already carrying numeric
anchors in the lsregister plan (gap 1 LSRegisterURL, gap 10 .app bundler) keep
their numbers as reference identifiers; all other items carry no number.

## TL;DR for an agent reading this cold

- Five categories: file primitives, exec/network primitives, runtime/sandbox,
  macOS integration, ergonomics.
- Two structural items remain OPEN: LSRegisterURL primitive (gap-1 anchor) and
  .app bundler stub (gap-10 anchor). Both are tracked under the lsregister plan
  as a dedicated S-22.1..S-22.4 weeks-scale cycle and are intentionally deferred.
- Three runtime items remain OPEN as honest workarounds: hexa-runtime sandbox
  cannot exec host binaries, sandbox does not propagate env vars, docker route
  duplicates stdout in some cases.
- Module-import / typed `use` resolution for cross-script wiring is OPEN; the
  atlas dispatcher uses a subprocess + `__RC=N` stdout trailer as the canonical
  workaround.
- Everything else needed for the invisible-sync design's day-1 kind handlers is
  CLOSED on disk.

## Category 1 — File primitives

### 1.1 yaml read+frontmatter parse

- Status: **CLOSED**
- Surface: `hexa-lang/stdlib/yaml.hexa` (+ companion `yaml.ai.md`)
- Capability: frontmatter extraction, flat-key parse. Nested-yaml validation is
  best-effort — `hive/spec/schema/spec_v1.schema.yaml` documents this caveat at
  raw10_caveats C1.
- Consumed by: `hive/tool/host_pool_mk2_loader.hexa`, `hive/tool/kick_mk2_loader.hexa`,
  `hive/tool/kick_roi_mk2_loader.hexa`, `hive/tool/mk2_catalog_lint.hexa`,
  bootstrap reads of `spec/sync_registry.spec.yaml` rows.

### 1.2 atomic value/cell operations

- Status: **CLOSED**
- Surface: `hexa-lang/self/atomic_ops.hexa` (atomic_new, atomic_load, atomic_store,
  atomic_cas, atomic_add, atomic_sub on int/bool cells)
- Note: this is the value-cell primitive set, not a typed atomic-file API.
  Atomic-file write semantics for plist/state writes (write-tmp + rename) are
  composed in `stdlib/portable_fs.hexa` + `self/stdlib/fs.hexa`; that
  composition is sufficient for the launchctl plist write path in
  `hive/handlers/launchctl.hexa` (see lsregister plan S-22.1).

### 1.3 portable filesystem stdlib

- Status: **CLOSED**
- Surface: `hexa-lang/stdlib/portable_fs.hexa`, `hexa-lang/self/stdlib/fs.hexa`,
  `hexa-lang/self/stdlib/path.hexa`
- Capability: read, write, rename, mkdir, exists, stat — sufficient for the
  invisible-sync handler set (repo / hook / gate / deploy-key / path-patch /
  env-export / npm-global / launchctl).

### 1.4 json parse + emit

- Status: **CLOSED**
- Surface: `hexa-lang/stdlib/json.hexa`, `hexa-lang/stdlib/json_object.hexa`,
  `hexa-lang/self/stdlib/json.hexa`
- Consumed by: every `__HEXA_<EVENT>__` sentinel emitter, every audit-jsonl
  writer (e.g., `~/.hive/state/fix_audit/mech_A_hexa_url.jsonl`).

## Category 2 — Exec / network primitives

### 2.1 process exec + popen with status

- Status: **CLOSED**
- Surface: `hexa-lang/self/stdlib/proc.hexa` — `popen_lines`,
  `popen_lines_with_status`, `proc_run_with_stdin`, `proc_run_json_bridge`
- Used as the canonical wrapper for ssh/git/lsregister shell-outs. Typed
  per-binary stdlib wrappers (a hypothetical `ssh.hexa` / `git.hexa`) are NOT
  present and not planned for this cycle — exec-wrapper is the canonical idiom.

### 2.2 ssh shell-out wrapper (composed via 2.1)

- Status: **CLOSED (composed, not typed primitive)**
- Surface: callers compose ssh invocations through `proc::popen_lines("ssh ...")`
  (e.g., `hexa-lang/gate/hexa_url.hexa::dispatch_remote`,
  `hexa-lang/gate/remote_preflight.hexa`, `hive/sync/rsync.hexa`).
- Caveat: no typed stdlib wrapper. Argument quoting is the caller's
  responsibility, validated by the bash dispatcher's `safe_arg` whitelist for
  hexa:// routes. Typed wrapper would be a future ergonomics item.

### 2.3 git tooling (lock retry, hook ban watcher)

- Status: **CLOSED**
- Surface: `hexa-lang/tool/git_lock_retry.hexa`,
  `hexa-lang/tool/git_hook_ban_watcher.hexa`
- These are tools rather than a stdlib `git.hexa` wrapper. The invisible-sync
  push/pull paths invoke git via `proc::popen_lines("git ...")` directly, which
  is sufficient for the day-1 handler set.

### 2.4 http / http2 / websocket

- Status: **CLOSED**
- Surface: `hexa-lang/stdlib/http.hexa`, `http2.hexa`, `websocket.hexa`
- Not load-bearing for invisible-sync today; cited because the broader hexa://
  ecosystem (`hexa://harness/oauth-login`) consumes them.

## Category 3 — Runtime / sandbox

### 3.1 module-load gate (Layer 2)

- Status: **CLOSED (gate-only)**
- Surface: `hexa-lang/self/stdlib/module_gate.hexa` — load-time admissibility
  check on `use "foo"` imports, fail-closed on IO fault, mirrors law_io
  contract.
- This is the **gate**, not a runtime resolver for cross-script function calls.
  See item 3.2 for the resolver-side gap.

### 3.2 cross-script module-import / typed `use` for dispatcher fan-out

- Status: **OPEN — workaround in place**
- Workaround: subprocess fan-out via `hexa run <subcmd>.hexa` with a `__RC=N`
  stdout trailer to propagate the child exit code back to the parent
  dispatcher.
- Canonical example: `nexus/mk2_hexa/mk2/src/atlas/mod.hexa` dispatches to
  `lookup.hexa` / `recall.hexa` / `hypothesis.hexa` / `distribution.hexa` via
  subprocess, parses `__RC=N` from the trailer.
- Why not closed: typed cross-`.hexa`-script function calls would require
  resolver-side support that is not present. Subprocess workaround is correct
  (gives process isolation + clear exit propagation); the cost is fork latency
  (~tens of ms per dispatch) and a one-line stdout protocol.
- Promotion path: a typed `use "atlas/lookup"` import surface would be a
  separate weeks-scale upstream effort and is intentionally NOT in the
  invisible-sync scope.

### 3.3 hexa-runtime → host binary exec (docker sandbox limit)

- Status: **OPEN — by design**
- Limitation: scripts running inside the hexa-runtime docker sandbox
  (`route=docker mac_safe_landing`) cannot exec Mac-host binaries.
- Workaround: routes that need host binaries (`browser-harness`, `osascript`,
  `ssh`, `nc`, `hive`, `lsregister`) land on the bash dispatcher
  (`~/.hx/bin/hexa-url-handler.sh`) instead of the hexa runtime resolver.
- Cite: `hexa-lang/gate/hexa_url_core.ai.md` § "Dispatcher decision matrix" +
  caveat #1.
- Why not closed: this is a security/isolation property of the docker sandbox,
  not an oversight. Closing it would weaken the sandbox.

### 3.4 sandbox env-var propagation

- Status: **OPEN — workaround in place**
- Limitation: `env("WS")` returns empty inside `hexa run gate/hexa_url.hexa`
  even when WS is exported in the calling shell.
- Affects: every resolver-side route that builds paths from `$WS` (cmd/list,
  cmd/<name>, gate/drill, gate/entry).
- Workaround: hardcode SSOT path or use `__file__`-relative resolution.
- Cite: `hexa-lang/gate/hexa_url_core.ai.md` raw#10 caveat #11 (discovered
  2026-05-02 via cmd/list smoke).

### 3.5 docker route stdout duplication

- Status: **OPEN — workaround in place**
- Limitation: `route=docker` mode appears to print stdout twice in some cases.
- Workaround: prefer bash dispatcher for routes where exact stdout matters
  (e.g., `fmt=json` envelopes).
- Cite: `hexa-lang/gate/hexa_url_core.ai.md` raw#10 caveat #2.

## Category 4 — macOS integration

### 4.1 LSRegisterURL primitive (gap-1 anchor — preserved from lsregister plan)

- Status: **OPEN — deferred to lsregister plan S-22.3**
- Required surface (not yet present):
  `hexa-lang/self/stdlib/macos_url_handler.hexa` exposing
  `ls_register_url_scheme`, `ls_get_default_handler`, `ls_set_default_handler`.
- Implementation strategies (per plan § S-22.3):
  - (a) Foundation/AppKit binding via dlopen + objc_msgSend — pure hexa-lang,
    requires Objective-C runtime knowledge, 1-2 weeks.
  - (b) Shell out to `lsregister` CLI — bash-thin, 2-3 days, recommended for
    first iteration.
  - (c) Swift FFI — write a tiny Swift helper, link via dlopen.
- Effort: 2-3 days (option b) to 1-2 weeks (option a).
- Cite: `hive/docs/lsregister_url_handler_plan_2026_05_05.ai.md` § S-22.3.

### 4.2 .app bundler (gap-10 anchor — preserved from lsregister plan)

- Status: **OPEN — deferred to lsregister plan S-22.2**
- Required surface (not yet present):
  `hive/sync/bundle_app.hexa` (lives in hive, not hexa-lang upstream — included
  here because the plan doc cites it as part of the upstream-support thread).
  Builds a minimal `.app` directory with `Info.plist` declaring
  `CFBundleURLSchemes: ["hexa"]` and a tiny bash trampoline.
- Effort: 3-4 days.
- Cite: `hive/docs/lsregister_url_handler_plan_2026_05_05.ai.md` § S-22.2.

### 4.3 LaunchServices URL-handler activation (composes 4.1 + 4.2)

- Status: **OPEN — composes 4.1 + 4.2**
- Plan: `hive/sync/verify_url_registration.hexa` (lsregister plan § S-22.4).
- Until 4.1 + 4.2 land, the `~/.hx/bin/hexa-url-handler.sh` bash dispatcher
  carries the **raw-9-exemption** carve-out as the macOS LaunchServices entry
  point. Cite: `hexa-lang/gate/hexa_url_core.ai.md` raw#10 caveat #5 ("macOS
  LaunchServices registration is manual").

## Category 5 — Ergonomics

### 5.1 resolver banner suppression

- Status: **CLOSED (CLI-flag form)**
- Mechanism: hexa-runtime resolver prints no startup banner under normal
  invocation; verbose-mode and dev-mode flags exist but are opt-in. Sufficient
  for `lsregister` stderr cleanliness in the S-22.3 path.
- Note: no standalone `banner_suppress.hexa` module — this is a CLI flag
  property, not a stdlib primitive.

### 5.2 audit log rotation

- Status: **OPEN — operational only**
- Limitation: `~/.hx/log/hexa-url-handler.log` and
  `~/.hive/state/fix_audit/mech_A_hexa_url.jsonl` grow unbounded (~1 line per
  invocation).
- Workaround: monitor and run `logrotate` manually when files cross ~10MB.
- Cite: `hexa-lang/gate/hexa_url_core.ai.md` raw#10 caveat #7.

## Status summary

| Category | Item | Status |
|---|---|---|
| 1 File | yaml read+frontmatter | CLOSED |
| 1 File | atomic value cells | CLOSED |
| 1 File | portable fs stdlib | CLOSED |
| 1 File | json parse+emit | CLOSED |
| 2 Exec | process exec / popen | CLOSED |
| 2 Exec | ssh shell-out (composed) | CLOSED |
| 2 Exec | git tooling | CLOSED |
| 2 Exec | http/http2/websocket | CLOSED |
| 3 Runtime | module-load gate | CLOSED (gate-only) |
| 3 Runtime | cross-script module-import | **OPEN** (subprocess + `__RC=N` workaround) |
| 3 Runtime | host-binary exec from sandbox | **OPEN** (by design — bash dispatcher carve-out) |
| 3 Runtime | sandbox env-var propagation | **OPEN** (workaround: hardcode / `__file__`-relative) |
| 3 Runtime | docker route stdout dup | **OPEN** (workaround: prefer bash for `fmt=json`) |
| 4 macOS | LSRegisterURL (gap-1 anchor) | **OPEN** — S-22.3 deferred |
| 4 macOS | .app bundler (gap-10 anchor) | **OPEN** — S-22.2 deferred |
| 4 macOS | LaunchServices URL handler | **OPEN** — S-22.4 composes 4.1+4.2 |
| 5 Ergo | resolver banner suppression | CLOSED (CLI-flag) |
| 5 Ergo | audit log rotation | **OPEN** (operational, no rotation) |

Closed: 9. Open: 9 (3 by design, 4 with workarounds, 2 deferred to S-22.x).

## Cross-link

- Sister docs (current state of hexa://):
  `hexa-lang/gate/hexa_url_core.ai.md`,
  `hexa-lang/gate/hexa_url_modules.ai.md`.
- Implementation plan for S-22.1..S-22.4 (the deferred OPEN items in
  Category 4): `hive/docs/lsregister_url_handler_plan_2026_05_05.ai.md`.
- Invisible-sync design (consumer of this catalog):
  `hive/docs/hexa_sync_invisible_design_2026_05_04.ai.md`.
- mk2 ecosystem catalog component pointing here:
  `hive/spec/mk2_ecosystem_catalog.spec.yaml § component_18 (invisible_sync_mk2)`.
- mk2 apex inventory entry:
  `hive/spec/mk2_apex.spec.yaml § inventory.12_invisible_sync`.

## raw#10 caveats

1. The previously cited "9/10 hexa-lang upstream gaps" carryover phrasing has
   no source enumeration. This catalog supersedes it. Any consumer that still
   reads "9/10" should be amended to point at this categorical surface.
2. The two items carrying numeric anchors (gap-1 LSRegisterURL, gap-10 .app
   bundler) preserve those numbers solely because the lsregister plan's
   subtask split references them. The numbers are reference identifiers, not
   an authoritative count.
3. ssh / git capabilities are CLOSED in the composed sense (callers wrap
   `proc::popen_lines`). Typed `ssh.hexa` / `git.hexa` stdlib wrappers do not
   exist and are not on the roadmap. If a future cycle needs typed wrappers,
   a separate proposal under `hexa-lang/proposals/` is the right surface.
4. Category 3 OPEN items are honest workaround records, not unfinished work:
   3.3 is a sandbox isolation property (closing it would weaken the sandbox);
   3.4 / 3.5 are docker-runtime quirks tracked in raw#10 caveats and worked
   around by the dispatcher decision matrix.
5. This document is authored in English per
   `hive/spec/mk2_ecosystem_catalog.spec.yaml § BR-MK2-AI-NATIVE-ENGLISH-ONLY`
   (since 2026-05-06). Future amends keep the same convention.
