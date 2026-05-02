---
schema: hexa-lang/gate/hexa_url_modules/ai-native/1
last_updated: 2026-05-02
sister_doc: ~/core/hexa-lang/gate/hexa_url_core.ai.md
catalog_type: per-route reference
total_routes: 13
status: 7 bash + 6 hexa-resolver routes; harness/* — all 4 axes (target/fmt/async/timeout) LIVE; target=mac|ubu1|ubu2|fleet all LIVE end-to-end via browser-harness v0.3.1; ubu1+ubu2 cold-path eliminated (global playwright-core); cmd/list discovery added; recovery/fleet now SSOT-driven
---

# hexa:// modules catalog (AI-native)

Per-route reference. For scheme grammar, dispatcher decision matrix, axis system,
exit codes, and sentinels — see `hexa_url_core.ai.md`.

## TL;DR

- 13 routes total: 7 land on the bash dispatcher, 6 on the hexa-runtime resolver.
- Only `harness/*` exposes the full axis system (target/fmt/async/timeout).
- Routes that ssh out (`kick`, `omega`, `htz/*`, `recovery/fleet`, `harness/?target=ubu*`)
  fan to the host fleet; `claude/login`, `memory/save`, `harness/?target=mac` stay
  local; `gate/*`, `cmd/*`, `active`, `remote/*` run inside the docker hexa runtime.

## Catalog matrix

| Scope/resource          | Dispatcher       | Target  | Status   | Required params      | SSOT                        |
|-------------------------|------------------|---------|----------|----------------------|-----------------------------|
| `kick`                  | bash             | ubu2    | LIVE     | `topic`              | `nexus/cli/run.hexa kick`   |
| `omega`                 | bash             | ubu2    | LIVE     | `seed`               | `nexus/cli/run.hexa omega`  |
| `claude/login`          | bash             | mac     | LIVE     | `slot`               | `osascript` Terminal spawn  |
| `htz/rescue`            | bash             | hetzner | LIVE     | (none)               | `ssh root@157.180.8.154`    |
| `htz/poll`              | bash             | mac     | LIVE     | (none)               | local `nc` poll loop        |
| `recovery/fleet`        | bash             | local   | LIVE (SSOT) | (none); env `RECOVERY_FLEET_HOSTS` overrides | `hive resource list` (live SSOT, falls back to hardcoded `ubu1 ubu2 hetzner`) |
| `harness/<sub>`         | bash             | mac/ubu*/fleet | **LIVE all targets** (browser-harness v0.3.0) | sub-specific | `~/.hx/bin/browser-harness` (CLI internally ssh-pipes to remote when target!=mac) |
| `memory/save`           | bash             | mac     | LIVE     | `slug`, `body`       | `~/.claude-claude6/projects/.../memory/<slug>.md` |
| `gate/drill`            | hexa runtime     | docker  | LIVE     | `seed`               | `gate/drill_bg_spawn.sh`    |
| `gate/entry`            | hexa runtime     | docker  | LIVE     | `mode`, `text`       | `gate/entry.hexa`           |
| `cmd/<name>`            | hexa runtime     | docker  | LIVE     | `seed` (template-defined) | `gate/commands.json`   |
| `cmd/list`              | hexa runtime     | docker  | LIVE (env quirk) | (none); optional `fmt=text\|json` | enumerates `commands.json[".commands"]` keys |
| `active`                | hexa runtime     | docker  | LIVE     | (none)               | `<git-root>/.hook-commands/active.json` |
| `remote/<host>/<inner>` | hexa runtime     | host    | LIVE (host needs hxq+gate) | `<inner>` URL | `gate/hexa_url.hexa:dispatch_remote` |

---

## Bash-dispatcher routes (`~/.hx/bin/hexa-url-handler.sh`)

### `kick`

Dispatch a nexus/cli `kick` job to ubu2 (Mac native hexa is forbidden by Axis D).
Logs go to `~/.hive/state/hexa_url_kick_<ts>.log`.

| Field    | Value                                                              |
|----------|--------------------------------------------------------------------|
| URL      | `hexa://kick?topic=<text>`                                         |
| Params   | `topic` (required, `safe_arg` whitelist)                           |
| Target   | `ubu2` via `hive resource exec ubu2 --command "..."`               |
| Timeout  | 180s (hardcoded)                                                   |
| Output   | (background; nohup → log file)                                     |
| Exit     | 0 (dispatch only); actual job result lives in log file             |

```sh
hxq "hexa://kick?topic=cycle_review"
```

### `omega`

Same shape as `kick`, runs `nexus/cli/run.hexa omega` on ubu2.

| Field    | Value                                            |
|----------|--------------------------------------------------|
| URL      | `hexa://omega?seed=<text>`                       |
| Params   | `seed` (required)                                |
| Target   | `ubu2`                                           |
| Timeout  | 180s                                             |
| Output   | bg log                                           |
| Exit     | 0 (dispatch only)                                |

### `claude/login`

Spawn a `claude --slot <N>` session in macOS Terminal. UI route — interactive.

| Field    | Value                                                      |
|----------|------------------------------------------------------------|
| URL      | `hexa://claude/login?slot=<N>`                             |
| Params   | `slot` (required, digits via `safe_arg`)                   |
| Target   | `mac` (osascript)                                          |
| Output   | new Terminal window                                        |
| Exit     | osascript exit                                             |

```sh
hxq "hexa://claude/login?slot=4"
```

### `htz/rescue`

Trigger fsck + reboot on the Hetzner AX102 server (rescue path).

| Field    | Value                                                      |
|----------|------------------------------------------------------------|
| URL      | `hexa://htz/rescue`                                        |
| Params   | (none)                                                     |
| Target   | `root@157.180.8.154` via SSH key `~/.ssh/id_hetzner_ax102` |
| Output   | bg log at `~/.hive/state/hexa_url_htz_rescue_<ts>.log`     |
| Exit     | 0 (dispatch only)                                          |

Pre-clears known_hosts entry to avoid stale-key blocks.

### `htz/poll`

Watch port 22 of the Hetzner box for up to 30 minutes (60 ticks × 30s); osascript
notification on first OPEN.

| Field    | Value                                                      |
|----------|------------------------------------------------------------|
| URL      | `hexa://htz/poll`                                          |
| Params   | (none)                                                     |
| Target   | `mac` (local `nc` loop)                                    |
| Output   | bg log; macOS notification                                 |
| Exit     | 0 (dispatch only)                                          |

### `recovery/fleet`

Roll-call status across the fleet via `hive resource status`. Host list is now
SSOT-driven (no longer hardcoded).

| Field    | Value                                                      |
|----------|------------------------------------------------------------|
| URL      | `hexa://recovery/fleet`                                    |
| Params   | (none)                                                     |
| Env      | `RECOVERY_FLEET_HOSTS` (override, space-separated; opt-in) |
| Target   | local (loops, ssh per host)                                |
| Output   | aggregated log at `~/.hive/state/hexa_url_fleet_<ts>.log` + audit `{event:"fleet-hosts",hosts:"..."}` |
| Exit     | 0                                                          |

Host discovery strategy (D, top-down):
1. `$RECOVERY_FLEET_HOSTS` env override (if set)
2. `hive resource list` parsed via `grep -vE '^[[:space:]]*(#|__HIVE_RESOURCE__)' | awk '{print $1}'`
3. hardcoded fallback `ubu1 ubu2 hetzner` (defensive, preserves historical behavior)

TODO: drop env override + hardcoded fallback once `hive resource` registers
hetzner (currently SSOT only has `ubu1 ubu2`).

### `harness/<sub>`

Multi-axis dispatcher for `need-singularity/browser-harness` CLI. The only route
with the full axis system (target/fmt/async/timeout). hxq short-circuits this
prefix to bypass the docker hexa runtime.

| Field        | Value                                                                   |
|--------------|-------------------------------------------------------------------------|
| URL          | `hexa://harness/<sub>?<axes>`                                           |
| Subcommands  | `probe` \| `selftest` \| `version` \| `oauth-login`                     |
| Axis `target`| `mac` (default) \| `ubu1` \| `ubu2` \| `fleet` — **all LIVE** (browser-harness v0.3.0 lib/remote.cjs ssh-pipes self-contained payload to remote bare node; remote needs only `node` + `npx playwright`, no harness install) |
| Axis `fmt`   | `text` (default) \| `json`                                              |
| Axis `async` | `0` (default) \| `1`\|`true`\|`false`                                   |
| Axis `timeout` | (none) \| `1`..`3600` seconds (uses `timeout`/`gtimeout`)             |
| oauth-login extra params | `slot=<N>` (required, digits), `headless=1` (optional)      |
| Sub-specific exits | inherits from `~/.hx/bin/browser-harness`; see `core.ai.md` exit map |

Examples:

```sh
hxq hexa://harness/probe                                       # ready, exit 0
hxq "hexa://harness/probe?fmt=json"                            # JSON envelope
hxq "hexa://harness/version?target=mac"                        # 0.2.0
hxq "hexa://harness/selftest?async=1"                          # __HEXA_HARNESS_ASYNC__ pid=N log=...
hxq "hexa://harness/probe?target=ubu2"                         # ready (remote=ubu2 node=v20.18.0 playwright=Version 1.59.1)
hxq "hexa://harness/probe?target=fleet"                        # fan-out across $BROWSER_HARNESS_FLEET_HOSTS (default ["ubu2"])
hxq "hexa://harness/oauth-login?slot=9&target=ubu2&headless=1" # remote oauth (manual-required → exit 51)
hxq "hexa://harness/oauth-login?slot=9&headless=1&timeout=300" # 5min hard cap
hxq "hexa://harness/oauth-login?slot=9&fmt=json&async=1"       # bg + JSON dispatch confirmation
```

`target=ubu1|ubu2|fleet` is fully LIVE as of browser-harness v0.3.0
(`lib/remote.cjs`). Mac CLI builds an inlined JS payload (factory.cjs +
oauth.cjs + bootstrap, ~18.9KB) and pipes it to `ssh <host> 'node -'`. Slot
storageState is SCP'd Mac↔remote with mode 0600. No browser-harness install on
remote — only `node` + `npx playwright` needed.

### `memory/save`

Write a markdown memory file (with YAML frontmatter) for the claude-code memory
indexer.

| Field    | Value                                                      |
|----------|------------------------------------------------------------|
| URL      | `hexa://memory/save?slug=<id>&body=<text>`                 |
| Params   | `slug` (required), `body` (required); both `safe_arg`-validated |
| Target   | `mac` local fs                                             |
| Output   | writes `~/.claude-claude6/projects/-Users-ghost-core-hive/memory/<slug>.md` |
| Exit     | 0                                                          |

Frontmatter is hardcoded (`type: reference`, `description: hexa://memory/save written`).
Project-relative — currently writes to the hive project's memory dir; not generic.

---

## Hexa-resolver routes (`~/core/hexa-lang/gate/hexa_url.hexa`)

These run inside the hexa-runtime docker sandbox. They cannot exec Mac-host
binaries; they can only `hexa run` other `.hexa` scripts and process URL data.

### `gate/drill`

| Field    | Value                                                      |
|----------|------------------------------------------------------------|
| URL      | `hexa://gate/drill?seed=<text>`                            |
| Params   | `seed` (required)                                          |
| Backend  | `gate/drill_bg_spawn.sh <seed>` (bg drill kick)            |
| Sentinel | drill-specific                                             |

### `gate/entry`

| Field    | Value                                                      |
|----------|------------------------------------------------------------|
| URL      | `hexa://gate/entry?mode=<m>&text=<t>`                      |
| Params   | `mode`, `text`                                             |
| Backend  | `hexa run gate/entry.hexa <mode> '<text>'`                 |

### `cmd/<name>`

Run a named command from `gate/commands.json`. Each entry has an `execution.entry`
that is invoked with `seed` substituted in.

| Field    | Value                                                      |
|----------|------------------------------------------------------------|
| URL      | `hexa://cmd/<name>?seed=<text>`                            |
| Params   | `name` (URL path), `seed` (template-dependent)             |
| Backend  | `commands.json[name].execution.entry` after seed substitution |
| Errors   | `CMD-NO-TEMPLATE` if name not found                        |

### `cmd/list`

Enumerate available `cmd/<name>` values without leaking the SSOT path. Reads
`gate/commands.json[".commands"]` keys (or top-level keys as fallback).

| Field    | Value                                                                |
|----------|----------------------------------------------------------------------|
| URL      | `hexa://cmd/list[?fmt=text\|json]`                                   |
| Params   | `fmt` (optional, default `text`)                                     |
| Output   | `text` → one name per line, sorted; `json` → array of `{name[, entry, description]}` |
| Backend  | `gate/commands.json` parsed; iterates `.commands` subtree            |
| Errors   | `CMD-LIST-BAD-FMT` (exit 4), `CMD-LIST-NO-FILE` (exit 1), `CMD-LIST-PARSE-FAIL` (exit 1) |
| Caveat   | hexa-runtime sandbox env `WS` quirk causes `CMDS_JSON` resolution to a relative path — see `hexa_url_core.ai.md` caveat #11. Workaround: hardcode SSOT path or use `__file__`-relative resolution. |

```sh
hxq hexa://cmd/list                        # one name per line
hxq "hexa://cmd/list?fmt=json"             # JSON array
hxq "hexa://cmd/list?fmt=xml"              # exit 4 with bad-fmt
```

Currently 38 commands enumerated from the live SSOT (when env propagation works).

### `active`

Read and print `<git-root>/.hook-commands/active.json` for the current cwd.

| Field    | Value                                                      |
|----------|------------------------------------------------------------|
| URL      | `hexa://active`                                            |
| Params   | (none)                                                     |
| Backend  | `cat $(git rev-parse --show-toplevel)/.hook-commands/active.json` |
| Errors   | `GIT-NO-ROOT` if not inside a git repo                     |

### `remote/<host>/<inner-scope>/<inner-resource>`

Forward an inner `hexa://` URL to another host's `~/.hx/bin/hxq`. Used for
heavy-compute offload from Mac to Linux hosts.

| Field      | Value                                                                   |
|------------|-------------------------------------------------------------------------|
| URL        | `hexa://remote/<host>/<inner-scope>/<inner-resource>?<query>`           |
| Params     | passes through to inner URL                                             |
| Preflight  | 300ms TCP probe via `gate/remote_preflight.hexa`                        |
| Backend    | `ssh -o BatchMode=yes -o ConnectTimeout=5 <host> '$HOME/.hx/bin/hxq <inner>'` |
| Caveat     | **Requires `hxq` + `gate/` on the remote host.** Genuine zero-install remote dispatch is the browser-harness v0.3 lib/remote.cjs path, not this. |

```sh
hxq hexa://remote/ubu2/gate/drill?seed=hello
hxq hexa://remote/ubu1/cmd/build?seed=fast
```

Errors: `REMOTE-BAD-HOST` (no inner scope), `REMOTE-UNREACHABLE` (preflight fail).

---

## Sister consumers

| Consumer                                         | URL routes used                          |
|--------------------------------------------------|------------------------------------------|
| `~/core/anima/tool/browser_harness.hexa`         | calls `~/.hx/bin/browser-harness` directly (not via hexa://) |
| `~/core/hive/tool/browser_harness_invoke.hexa`   | same as above; doesn't use hexa://       |
| `~/.hx/packages/browser-harness/wrappers/browser_harness.hexa` | same |
| macOS LaunchServices (browser/Mail clicks)       | all bash-dispatcher routes               |
| `hxq` CLI                                        | all routes (with harness short-circuit)  |

Note: today no production caller invokes `hexa://harness/*` from inside another
`.hexa` script — direct CLI is preferred. The hexa:// route is for terminal users,
shareable URL bookmarks, and macOS LaunchServices integration.

## Per-scope ownership

| Scope             | Owner module / repo                          |
|-------------------|----------------------------------------------|
| `kick`, `omega`   | `nexus/cli/run.hexa` (need-singularity/nexus) |
| `claude/login`    | claude-code session                          |
| `htz/*`           | hetzner ops (no SSOT module)                 |
| `recovery/fleet`  | `hive resource` CLI (need-singularity/hive-resource) |
| `harness/*`       | `need-singularity/browser-harness`           |
| `memory/save`     | claude-code memory layer (hive project)      |
| `gate/*`, `cmd/*`, `active`, `remote/*` | `~/core/hexa-lang/gate/`        |

When adding a route, register both: `hexa_url_core.ai.md` (axis support if any)
and this file's catalog matrix + per-scope section.

## Verified end-to-end (2026-05-02)

- `harness/probe` (mac, default) → `ready` exit 0
- `harness/version` → `0.3.1`
- `harness/selftest` → F1-F9 PASS via bash dispatcher
- `harness/oauth-login?slot=99&headless=1` → exit 51 with 3-fallback message
- `harness/probe?target=ubu1` → `ready (remote=ubu1 node=v20.18.0 playwright=Version 1.59.1)` exit 0 in **0.88s** (cold-path eliminated)
- `harness/probe?target=ubu2` → same shape, **0.94s**
- `harness/selftest --target ubu2` → F1+F2+F3+F5-remote+F6+F7-remote+F8-remote PASS (browser-harness v0.3.1 symmetric remote selftest)
- `harness/probe?target=fleet` → fan-out stanza per host (ubu2 default; `BROWSER_HARNESS_FLEET_HOSTS=ubu1,ubu2` to expand)
- `harness/oauth-login?slot=99&target=ubu2&headless=1` → exit 51 (state SCP'd, manual click required); exit-52 idempotent path now skips SCP-back when sha256 byte-identical
- `harness/?fmt=json` → JSON envelope
- `harness/?async=1` × 2 same-second → `_${TS}_${SUB}_$$.log` distinct paths
- `harness/?timeout=5` → wraps with `timeout 5s`; exits 124 on timeout
- `recovery/fleet` (default) → SSOT-driven `ubu1 ubu2`; `RECOVERY_FLEET_HOSTS=ubu2` correctly restricts
- `cmd/list?fmt=xml` → exit 4 BAD-FMT (happy path requires WS env propagation fix; see core caveat #11)
- `kick`, `omega` → ubu2 ssh dispatch (audit log shows complete events)
- `claude/login?slot=4` → Terminal window opens
- `recovery/fleet` → log file populated with 3 host stanzas
- `gate/drill?seed=foo` → drill_bg_spawn fired
- `active` → reads `.hook-commands/active.json` from current git root
- `remote/ubu2/gate/drill?seed=foo` → ssh ubu2 hxq inner-URL (preflight gate works)

## Caveats specific to module-set

1. **`memory/save` writes to the HIVE project memory dir, not the cwd's project.**
   If you're in anima and call `hexa://memory/save?slug=foo&body=bar`, the file
   lands in the hive memory store. Hardcoded path; redesign needed for
   cross-project parity.
2. **`htz/rescue` hardcodes IP `157.180.8.154`.** Rebuild the route if the box
   moves or is re-numbered.
3. **`recovery/fleet` host list is hardcoded** to `ubu1 ubu2 hetzner`. Adding a
   new host requires editing the bash handler.
4. **Cold-remote path eliminated for ubu1+ubu2 (2026-05-02).** Both hosts have
   `playwright-core@1.59.1` installed globally; warm probes ~0.9s. Onboarding
   a new host: `ssh <host> 'npm i -g playwright-core --silent --no-audit --no-fund'`.
   Without this, first invocation costs ~30s for one-shot `npm install --prefix
   <tmp>`.
5. **`cmd/<name>` depends on `gate/commands.json`.** Missing template → `CMD-NO-TEMPLATE`.
   No discovery endpoint to enumerate available command names.
6. **`remote/*` requires hxq + gate/ on remote.** True ubu*-no-install path is
   browser-harness's lib/remote.cjs (v0.3), not the URL `remote/` scope.

## File index

| Path                                         | sha256                                                              | LOC |
|----------------------------------------------|---------------------------------------------------------------------|-----|
| `~/.hx/bin/hexa-url-handler.sh`              | `db059a91cd430c030eb1b7d0b73f1439168a1f1f8f2d608762e628a048c8beb6`  |  -  |
| `~/.hx/bin/hxq`                              | `1e0b938c29f68ca5998f5fcc5c2d12899bd4531b1b7e9ce14122ece14ea3f786`  | 115 |
| `~/core/hexa-lang/gate/hexa_url.hexa`        | `9f7a74e708c98660c7e27f096a28587fd6e7d94556353cf85b401283a4293273`  |  -  |
| `~/core/hexa-lang/gate/remote_preflight.hexa`| `9e9555b8aba3e64a02acde73e2f72487ded47245d92abf697b0106dd213b5aa9`  |  -  |
| `~/.hx/packages/browser-harness/lib/remote.cjs` (v0.3.1) | `008cc62abe68c5b43ee76a8ba5ad41f4f7fea3ca6832d6e28db0ee2bc387d613`  |  -  |
| `~/.hx/packages/browser-harness/tests/selftest_remote.cjs` (v0.3.1) | `93ebfb77668b7b03328ba2934fcfc129b7fd48fb58f1057f554d09b7592ebd4e`  | 132 |

After axis additions (fmt/async/timeout) or new scope land, re-pin sha256 and
re-stamp `last_updated` in both this file and `hexa_url_core.ai.md`.
