# OS-Level Lock Setup (Tier 1 → Tier 3)

This 1-page guide covers the real, user-unbypassable defense for the raw:os-lock SSOT set (`.raw`, `.own`, `.ext`, `.roadmap`, `.loop` + the raw#12 grandfather list).

The previous `chflags uchg` (user-immutable) is trivially bypassable — the same account that set it can clear it. Real protection needs three cooperating tiers. All error/ok output from these tools follows the ai-native format in `self/core/ai_err.hexa` so agents can parse results.

## Threat model

- **Attacker**: a long-running agent (Claude Code, a shell session) acting as the same Unix user as the developer.
- **Goal**: prevent that agent from mutating raw:os-lock files without explicit human consent at each edit.
- **Out of scope**: a rogue agent that obtains root via unrelated privilege escalation.

## Tier 1 — schg (system-immutable, sudo-gated)

`chflags schg` on macOS (BSD system-immutable flag) can only be cleared by root, and only when the system is in single-user mode OR by `sudo chflags noschg` with current creds. The agent cannot bypass this without invoking sudo — which is where Tier 2 intercepts.

Activate:

```
./hexa tool/os_level_lock.hexa harden
```

This applies `chflags schg` to `_critical_targets()` = `{.raw, .own}`. The other SSOT (`.ext`, `.roadmap`, `.loop`) stays at Tier 0 (`uchg`) because legitimate edit frequency is high enough that the friction isn't worth the marginal protection.

Linux: `chattr +i` behaves like schg (root-only set/clear). `harden` routes through the same path.

Reverse with `./hexa tool/os_level_lock.hexa soften`.

Verify per-file state:

```
./hexa tool/os_level_lock.hexa status --ai
```

(`--ai` emits the ai-native format; plain `status` remains human-readable.)

## Tier 2 — pam_tid.so (TouchID gate on sudo)

Tier 1 alone still lets a single `sudo -v` (password-cached) session persist for 5 minutes — a window an agent can exploit. Adding `pam_tid.so` to `/etc/pam.d/sudo` routes every sudo invocation through TouchID, which the agent physically cannot satisfy.

Check current state:

```
./hexa tool/pam_tid_check.hexa
```

If it returns `err pam-tid/not-configured` or `err pam-tid/disabled`, edit the pam file. On macOS 14+ the durable location is `/etc/pam.d/sudo_local` (survives softwareupdate); on older releases, `/etc/pam.d/sudo` directly.

Manual activation:

```
sudo vi /etc/pam.d/sudo_local
```

Add this line as the **first** non-comment auth entry:

```
auth       sufficient     pam_tid.so
```

Re-check:

```
./hexa tool/pam_tid_check.hexa
# expect: ok pam-tid/configured
```

## Tier 3 — the `hx_edit` ceremony

The per-edit "ceremony" is what forces a fresh human authentication for every mutation of a raw:os-lock file. With Tier 1 + Tier 2 in place:

```
./hexa tool/hx_edit.hexa .raw
```

Flow:

1. verify `.raw` is in scope (else ai-native err)
2. `sudo -k` — invalidates the sudo cache (forces re-auth even if a ticket is warm)
3. `sudo chflags noschg .raw` — triggers TouchID prompt via pam_tid
4. launches `$EDITOR` (or `$VISUAL`, else `vi`) on `.raw`
5. runs `./hexa self/raw_cli.hexa verify` — if it fails, the file is **left unlocked** so you can re-run `--retry` without losing work
6. `sudo -n chflags schg .raw` — relock (uses the cached ticket from step 3)
7. emits `ok hx-edit/done` with a `git diff --stat` snippet

Flags:

- `--retry` — skip step 2 (reuse a fresh sudo ticket when retrying after a verify fail).
- `--list-scope` — dump the allowed-file list (agent introspection).
- `--dry-run` — print the plan; do not touch sudo or the editor.

Every error path emits through `self/core/ai_err.hexa`. A representative failure:

```
err hx-edit/sudo-aborted
  file: .raw
  cause: sudo prompt aborted or TouchID canceled (exit 1)
  remedy:
    - ./hexa tool/hx_edit.hexa .raw --retry
    - sudo -v   # refresh sudo cache first
  ref: raw:os-lock
```

## Emergency bypass

If TouchID breaks (finger unrecognized, sensor dead) or the editor session corrupts state:

```
sudo chflags noschg .raw                 # manual unlock (will prompt for password if pam_tid fails)
$EDITOR .raw
./hexa self/raw_cli.hexa verify
sudo chflags schg .raw                   # manual relock
```

No silent-failure fallback exists — the ai-native error emitter refuses to emit exit codes without a preceding `err …` payload.

## Tier 4 (deferred) — agent isolation account

The strongest layer is to run the agent (`claude`, background `./hexa` processes, etc.) as a non-admin Unix user that is not in `/etc/sudoers`. Then even a compromised agent literally cannot invoke sudo — Tier 1 becomes unbypassable without a second human login.

Claude Code currently runs as the developer's primary account, so this tier is deferred. When ready:

1. create a second macOS user (`claude-agent`) without admin rights
2. give it read access to the repo + write access only to `.claude/worktrees/`
3. switch Claude Code to run under that account via launchd plist
4. revoke the primary user's raw:os-lock write perms so ALL edits go through hx_edit

Until then, Tier 1 + Tier 2 + Tier 3 are the real defense surface.

## Sanity

Run all three checks:

```
./hexa tool/os_level_lock.hexa status --ai
./hexa tool/pam_tid_check.hexa
./hexa tool/hx_edit.hexa --list-scope
```

Agents parse the stdout/stderr of each of these with the `self/core/ai_err` grammar. A `remedy:` block is always actionable; a missing `remedy:` in an `err` payload is itself a bug in the emitter.
