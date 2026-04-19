# OS-level File Lock (raw#0)

Kernel-enforced immutability for SSOT files. Pre-commit hooks and PreToolUse
hooks are bypassable by any attacker or accidental `--no-verify`. The BSD
`uchg` user-immutable flag is enforced by the kernel — any write, unlink,
or rename attempt returns `EPERM` until the owner explicitly clears it.

## Scope

Required (always locked, must exist):

- `.raw` `.own` `.ext` `.roadmap` `.loop`

Optional grandfather (from raw#12 bans — locked only when present):

- `CLAUDE.md` `AGENTS.md` `CODEX.md`
- `.cursorrules` `.cursor` `.windsurfrules`
- `.aider.conf.yml` `.continuerules`
- `.github/copilot-instructions.md`
- `.claude`

## Commands

```
./hexa tool/os_level_lock.hexa lock     # apply chflags uchg to every target
./hexa tool/os_level_lock.hexa unlock   # remove uchg (owner-only, for edits)
./hexa tool/os_level_lock.hexa status   # print lock state of each target
./hexa tool/os_level_lock.hexa verify   # exit 1 if any required target unlocked
```

## Editing SSOT

```
./hexa tool/os_level_lock.hexa unlock
$EDITOR .raw                             # edit freely
./hexa tool/os_level_lock.hexa lock      # re-apply before committing
```

Forgetting `lock` is caught by `verify` in the pre-commit hook.

## Enforcement Model

`chflags uchg` sets the BSD user-immutable flag. The kernel rejects every
`write(2)` / `unlink(2)` / `rename(2)` / `truncate(2)` syscall on the file
with `EPERM`. No shim, no hook, no sudo escalation bypasses this short of
removing the flag first. Removing the flag requires the file owner running
`chflags nouchg`, i.e. the only bypass path is deliberate and logged.

## Platform

- macOS (Darwin) — supported, uses `chflags uchg`.
- Linux — no-op with warning. Equivalent is `chattr +i`, which requires
  root; not implemented.

## Pre-commit Integration

Call `verify` early in the pre-commit hook:

```
./hexa tool/os_level_lock.hexa verify || exit 1
```

Non-zero exit blocks the commit and prints the unlocked target list.

## Limitations

- `git stash` touching a locked SSOT fails with `EPERM` — unlock before
  stashing.
- `chflags` only affects Darwin inode flags; network filesystems (NFS)
  may silently ignore it.
- Lock applies to the directory inode for `.claude`, not its contents
  (contents are still editable). Raw#12 ban covers the addition of new
  entries at the directory level.
