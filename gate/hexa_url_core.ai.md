---
schema: hexa-lang/gate/hexa_url_core/ai-native/1
last_updated: 2026-05-02
ssot:
  bash_dispatcher:   ~/.hx/bin/hexa-url-handler.sh
  hexa_resolver:     ~/core/hexa-lang/gate/hexa_url.hexa
  cli_shortcut:      ~/.hx/bin/hxq
  preflight:         ~/core/hexa-lang/gate/remote_preflight.hexa
sister_doc:          ~/core/hexa-lang/gate/hexa_url_modules.ai.md
audit_trails:
  invocation_log:    ~/.hx/log/hexa-url-handler.log
  jsonl_audit:       ~/.hive/state/fix_audit/mech_A_hexa_url.jsonl
mac_url_handler:     LaunchServices URL scheme (hexa://) → bash dispatcher
status: dispatcher landed; 7 bash routes + 6 hexa routes; harness all 4 axes (target/fmt/async/timeout) LIVE; target=mac|ubu1|ubu2|fleet all LIVE end-to-end with cold-path eliminated (browser-harness v0.3.1 + global playwright-core on ubu1/ubu2); cmd/list discovery added; recovery/fleet now reads hive-resource SSOT
---

# hexa:// dispatcher (AI-native core)

`hexa://<scope>/<resource>?<axes>` is a URL-scheme invocation surface for triggering work
across the local machine and the host fleet. Two physical dispatchers share the scheme
namespace; `hxq` is the CLI shortcut that picks the right one.

## TL;DR for an agent reading this cold

- One scheme, two dispatchers. **Bash** (`~/.hx/bin/hexa-url-handler.sh`, native, no sandbox)
  for ops/integration routes. **Hexa runtime** (`gate/hexa_url.hexa`, runs in docker
  `route=docker mac_safe_landing`) for compute/dispatch logic.
- Decision rule: routes that exec Mac-host binaries (`browser-harness`, `osascript`,
  `ssh`, `nc`, `hive`) MUST land on bash dispatcher — hexa runtime cannot exec host
  binaries from inside its sandbox.
- `hxq <url>` is the canonical CLI. It short-circuits `hexa://harness/*` to the bash
  dispatcher; everything else goes through `gate/hexa_url.hexa` via `hexa run`.
- macOS LaunchServices clicks (browser/Mail) hit the bash dispatcher directly.
- All invocations append a JSON line to `~/.hx/log/hexa-url-handler.log` and
  `~/.hive/state/fix_audit/mech_A_hexa_url.jsonl` — full audit trail.

## URL grammar

```
hexa://<scope>[/<resource>][?<axes>]
  scope     ::= [a-z]+
  resource  ::= <ident>("/"<ident>)*           # may be empty for some scopes (e.g. active)
  axes      ::= <pair> ("&" <pair>)*
  pair      ::= <key> "=" <urlencoded-value>
```

Examples:

```
hexa://active                                       # scope=active, no resource, no axes
hexa://gate/drill?seed=hello                        # scope=gate, resource=drill
hexa://harness/oauth-login?slot=9&headless=1        # nested resource = none, harness/<sub>
hexa://remote/ubu2/gate/drill?seed=foo              # nested remote dispatch
```

`PATH_PART = "<scope>/<resource>"` after stripping `hexa://` and the `?<axes>` tail.
Bash dispatcher uses `case "$PATH_PART"` matching; hexa resolver uses
`parse_url(url) → [scope, resource, query_raw]`.

## Dispatcher decision matrix

| Scope/resource          | Dispatcher       | Reason                                         |
|-------------------------|------------------|------------------------------------------------|
| `kick`                  | bash → ubu2 ssh  | nexus/cli compute on remote, Axis D forbids local hexa |
| `omega`                 | bash → ubu2 ssh  | same                                           |
| `claude/login`          | bash             | osascript Terminal spawn (Mac UI)              |
| `htz/rescue`            | bash             | hetzner ssh + reboot                           |
| `htz/poll`              | bash             | nc loop + osascript notification               |
| `recovery/fleet`        | bash             | iterates `hive resource status` over hosts     |
| `harness/<sub>`         | bash (hxq short-circuits) | exec `~/.hx/bin/browser-harness`         |
| `memory/save`           | bash             | local fs write                                 |
| `gate/drill`            | hexa runtime     | drill_bg_spawn launcher                        |
| `gate/entry`            | hexa runtime     | entry.hexa exec                                |
| `cmd/<name>`            | hexa runtime     | commands.json template invoke                  |
| `active`                | hexa runtime     | reads `<git-root>/.hook-commands/active.json`  |
| `remote/<host>/<inner>` | hexa runtime     | ssh `<host> hxq <inner>` (requires hxq+gate/ on remote) |

## hxq dispatch decision

```
$ hxq <url>
  │
  ├─ url matches ^hexa://harness/  → exec ~/.hx/bin/hexa-url-handler.sh <url>
  │                                  (bypass docker; need to exec Mac binary)
  │
  └─ otherwise                     → exec hexa run gate/hexa_url.hexa <url>
                                     (in-docker semantic resolver)
```

LaunchServices invocation (browser click on a `hexa://` link) is registered to the
bash dispatcher directly; no shortcut routing happens. hxq exists for terminal use.

## Axis system (orthogonal query params)

Currently axes are implemented for the `harness/*` scope only. Future scopes may opt
in by replicating the same axis-parse boilerplate.

| Axis      | Default | Values                  | Effect                                                |
|-----------|---------|-------------------------|-------------------------------------------------------|
| `target`  | `mac`   | `mac` \| `ubu1` \| `ubu2` \| `fleet` | WHERE invocation runs (all LIVE — browser-harness v0.3 lib/remote.cjs ssh-pipes payload to remote bare node, no remote install) |
| `fmt`     | `text`  | `text` \| `json`        | Output shape; json wraps in envelope                  |
| `async`   | `0`     | `0` \| `1` \| `true` \| `false` | nohup fork, return sentinel + exit 0 immediately |
| `timeout` | (none)  | `1`..`3600` (seconds)   | wraps invocation with `timeout Ns` (or `gtimeout`)    |

**Precedence** (when multiple axes set): `target` selects host → `timeout` wraps the
invocation → `async` forks the (timeout-wrapped) command → `fmt` controls the FINAL
output (sync result OR async dispatch confirmation envelope).

### `fmt=json` envelope shape

Sync result:
```json
{
  "ts": <unix-seconds>,
  "url": "<original hexa:// URL>",
  "sub": "<probe|selftest|version|oauth-login>",
  "target": "mac|ubu1|ubu2|fleet",
  "exit": <int>,
  "stdout": "<captured>",
  "stderr": "<captured>",
  "duration_ms": <int>,
  "timeout_fired": <bool>
}
```

Async dispatch confirmation:
```json
{
  "ts": <unix-seconds>,
  "url": "<...>",
  "async": true,
  "pid": <int>,
  "log": "<absolute path to log file>"
}
```

## Exit code map (canonical)

Inherited from POSIX + scope-specific assignments. Bash dispatcher re-emits the
underlying command's exit; hexa resolver returns its own.

| Code | Meaning                                                     |
|------|-------------------------------------------------------------|
| 0    | success                                                     |
| 1    | rejected (non-hexa scheme, harness absent, OAuth failed, etc.) |
| 2    | unsafe argument (failed `safe_arg` whitelist regex)         |
| 3    | required query param missing (`?slot=`, `?topic=`, `?seed=`, `?body=`, `?slug=`) |
| 4    | usage / unknown subcmd / bad axis value (target/fmt/async/timeout) |
| 5    | selftest fail (browser-harness)                             |
| 50   | not-implemented stub (historical; all `harness/*` paths LIVE as of bp/v0.3.0 + handler 60a4562) |
| 51   | oauth manual-required-but-headless                          |
| 52   | oauth idempotent (already logged in, no action)             |
| 124  | timeout fired (when `?timeout=Ns` set)                      |

## Sentinel patterns (machine-readable stdout markers)

Conventions: `__<COMPONENT>_<EVENT>__ key=value [key=value...]` on its own line.

| Sentinel                             | Producer                | Meaning                                  |
|--------------------------------------|-------------------------|------------------------------------------|
| `__BROWSER_HARNESS_PROBE__`          | browser-harness wrapper | install path resolution result           |
| `__BROWSER_HARNESS_SELFTEST__`       | browser-harness         | F1-F8 PASS/FAIL summary                  |
| `__BROWSER_HARNESS_INVOKE__`         | hive wrapper            | wrapper-side selftest verdict            |
| `__ANIMA_BROWSER_HARNESS__`          | anima wrapper           | anima-side selftest verdict              |
| `__ANIMA_BROWSER_HARNESS_PROBE__`    | anima wrapper           | chain probe through harness              |
| `__HEXA_HARNESS_ASYNC__`             | bash dispatcher         | `pid=N log=<path>` for async dispatch    |
| `__HIVE_RESOURCE__`                  | hive resource exec      | fleet route success/failure              |
| `__PRE_RC=<n>`                       | remote_preflight        | TCP probe rc (0=reachable)               |
| `__BROWSER_HARNESS_PKG_WRAPPER__`    | shipped pkg wrapper     | wrapper-self selftest                    |

## Audit trail

Every invocation writes two JSON lines to two files:

```
~/.hx/log/hexa-url-handler.log               (rotating local log; 1 line per event)
~/.hive/state/fix_audit/mech_A_hexa_url.jsonl (jsonl audit; preserved long-term)
```

Events: `invoke`, `dispatch`, `complete`, `reject`. Each carries `ts`, `url`,
`event`-specific keys (e.g. `reason` for reject, `rc` for complete).

Bash dispatcher writes; hexa resolver does not (yet — gap noted; if instrumentation
needed, mirror the bash pattern via `to_string(exec("echo ... >> $LOG"))`).

## Adding a new scope (template)

For an OPS route (exec host binary, ssh, osascript) → bash dispatcher:

1. Add a `<scope>/<resource>)` clause inside `case "$PATH_PART"` in
   `~/.hx/bin/hexa-url-handler.sh`.
2. Extract required query params via `qv <key>`. Validate with `safe_arg`.
3. Emit dispatch JSON line to `$LOG`.
4. Run the work (foreground exec OR `nohup ... &; disown` for fire-and-forget).
5. Capture exit code, emit complete JSON line, `exit $RC`.
6. If the route should be hxq-callable without docker overhead, optionally extend
   `~/.hx/bin/hxq` short-circuit (currently only `harness/*` is short-circuited).

For a COMPUTE route (parse URL, dispatch hexa script, transform) → hexa resolver:

1. Add an `else if sc == "<scope>"` branch in `main()` of
   `gate/hexa_url.hexa`, optionally with a `dispatch_<scope>(res, query)` fn.
2. Use `get_param(query, "<key>")` to extract axes.
3. exec downstream hexa scripts via `hexa run <script> <args>`.
4. Update the usage message in both `main()` `ai_err("USAGE-EMPTY"...)` and the
   final `ai_err("SCOPE-UNKNOWN"...)`.

For ANY new scope: add a row to `hexa_url_modules.ai.md` documenting input/output/
exit/sentinel/example.

## raw#10 caveats

1. **No docker → host bridge.** hexa-resolver scripts inside the docker sandbox
   cannot exec `~/.hx/bin/<anything>` or `/bin/echo` etc. Always check the
   "Dispatcher decision matrix" before adding a route — host-binary routes belong
   on bash.
2. **Output duplication on docker path.** `route=docker` mode appears to print
   stdout twice in some cases. Workaround: prefer bash dispatcher for any route
   where exact stdout matters (e.g. `fmt=json` envelopes).
3. **`safe_arg` whitelist is permissive but bash-3.2 limited.** Char class
   `^[a-zA-Z0-9 _.,:/=!?<>()·-]+$`. Quotes, semicolons, dollar signs all rejected.
   Don't pass shell-active characters in query values.
4. **`urldecode` requires python3.** Falls back to identity if absent — encoded
   sequences like `%20` won't decode. Air-gapped python-less environments break.
5. **macOS LaunchServices registration is manual.** This doc doesn't ship the
   `LSRegisterURL` step (raw-9-exemption per handler header). Confirm registration
   with `mdls -name kMDItemCFBundleIdentifier $(lsregister -dump | grep -i hexa)`
   if browser clicks fail to land.
6. **`hexa://remote/<host>/...` requires hxq+gate on the remote.** Comment in
   `dispatch_remote` line 187. Genuine zero-install remote (Mac→bare-node) is the
   browser-harness v0.3 lib/remote.cjs path, NOT this remote scope.
7. **Audit log can grow unbounded.** No rotation today. `~/.hive/state/fix_audit/
   mech_A_hexa_url.jsonl` grows ~1 line per invocation. Consider logrotate when
   it crosses ~10MB.
8. **Async log naming includes PID suffix** (handler db059a9). Pattern is
   `~/.hive/state/harness_async_${TS}_${SUB}_$$.log` — two same-second
   dispatches no longer collide. Verified via parallel kick.
9. **Async dispatch latency 120-300ms** (not the 100ms target). Bottleneck is
   bash interpreter startup + `jq` invocation (json case ~2× text). Hard floor
   on macOS without a compiled-binary rewrite.
10. **Remote target latency: ~0.9s warm.** `target=ubu1|ubu2` paths now ~0.88-0.94s
    after pre-installing `playwright-core@1.59.1` globally on both hosts (2026-05-02).
    Cold path (host without global playwright-core) costs ~30s for one-shot
    `npm install --prefix <tmp>`. To onboard a new host, run
    `ssh <host> 'npm i -g playwright-core --silent --no-audit --no-fund'`.
11. **hexa-runtime sandbox env vars not propagated.** `env("WS")` returns empty
    inside `hexa run gate/hexa_url.hexa` even when WS is exported in the calling
    shell. Affects all resolver-side routes that build paths from $WS (cmd/list,
    cmd/<name>, gate/drill, gate/entry). Workaround: hardcode SSOT path or use
    `__file__`-relative resolution. Discovered 2026-05-02 by cmd/list smoke.

## Verified end-to-end (2026-05-02)

- Bash dispatcher native exec: `harness/probe` → `ready` exit 0 (Mac).
- hxq short-circuit: `hxq hexa://harness/version` → `0.2.0` exit 0 (no docker pass).
- Hexa resolver: `hxq hexa://active` → reads `.hook-commands/active.json` (or
  GIT-NO-ROOT in non-repo cwd).
- Remote preflight: 300ms TCP probe gate guards `dispatch_remote`.
- All four axes (target/fmt/async/timeout) on `harness/*` validated 14/14 PASS.
- `hexa://harness/probe?target=ubu2` → `ready (remote=ubu2 node=v20.18.0
  playwright=Version 1.59.1)` exit 0 in **0.94s wall-time** (warm cache,
  global playwright-core).
- `hexa://harness/probe?target=ubu1` → 0.88s (same).
- `hexa://harness/selftest --target ubu2` → F1+F2+F3+F5-remote+F6+F7-remote+
  F8-remote PASS (browser-harness v0.3.1 symmetric remote selftest).
- `hexa://harness/probe?target=fleet` → fan-out across hosts; per-host stanza.
- `hexa://harness/probe?target=ubu2&fmt=json` → JSON envelope.
- `hexa://harness/version?async=1` × 2 same-second → distinct PID-suffixed log
  files (`_75480.log` + `_75479.log`).
- `hexa://recovery/fleet` (default) → reads `hive resource list` SSOT, audit
  log records `{"event":"fleet-hosts","hosts":"ubu1 ubu2"}`.
- `hexa://cmd/list` → enumerates 38 commands from `gate/commands.json`
  (`.commands` subtree); BAD-FMT branch fires correctly (exit 4); happy-path
  blocked by sandbox env quirk (caveat #11).
- LaunchServices click → bash handler verified historically via raw audit trail.

## File index

| Path                                         | sha256                                                              | LOC |
|----------------------------------------------|---------------------------------------------------------------------|-----|
| `~/.hx/bin/hexa-url-handler.sh`              | `db059a91cd430c030eb1b7d0b73f1439168a1f1f8f2d608762e628a048c8beb6`  |  -  |
| `~/.hx/bin/hxq`                              | `1e0b938c29f68ca5998f5fcc5c2d12899bd4531b1b7e9ce14122ece14ea3f786`  | 115 |
| `~/core/hexa-lang/gate/hexa_url.hexa`        | `9f7a74e708c98660c7e27f096a28587fd6e7d94556353cf85b401283a4293273`  |  -  |
| `~/core/hexa-lang/gate/remote_preflight.hexa`| `9e9555b8aba3e64a02acde73e2f72487ded47245d92abf697b0106dd213b5aa9`  |  -  |
| `~/.hx/packages/browser-harness/lib/remote.cjs` (v0.3.1) | `008cc62abe68c5b43ee76a8ba5ad41f4f7fea3ca6832d6e28db0ee2bc387d613`  |  -  |
| `~/.hx/packages/browser-harness/tests/selftest_remote.cjs` (v0.3.1) | `93ebfb77668b7b03328ba2934fcfc129b7fd48fb58f1057f554d09b7592ebd4e`  | 132 |

Re-pin via `shasum -a 256` after each edit. Sister: `hexa_url_modules.ai.md`
catalogs every scope/resource exposed by these files.
