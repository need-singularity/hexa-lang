# .raw-exemptions/

Per-rule grandfather registry for `raw#0` P1b. Each file exempts specific
file:line locations from a single rule, with a **mandatory** expiry date.

## Directory layout

```
.raw-exemptions/
  raw_<id>.list     # exemptions for .raw rule <id>
  own_<id>.list     # exemptions for .own rule <id>
  README.md         # this file
```

## File format

One entry per line. Comments (`#`) and blank lines are allowed.

```
<file_path> | <line_range> | <reason> | <expiry YYYY-MM-DD>
```

| Field        | Notes                                                            |
|--------------|------------------------------------------------------------------|
| `file_path`  | repo-relative path (e.g. `self/native/hexa_v2.bak_20260418`)     |
| `line_range` | single `42`, range `10-20`, or `*` for whole-file                |
| `reason`     | short human justification — visible in `list` output             |
| `expiry`     | `YYYY-MM-DD` UTC — **required**, indefinite grandfather forbidden |

## Example

```
# .raw-exemptions/raw_7.list
# format: <file_path> | <line_range> | <reason> | <expiry YYYY-MM-DD>
# expiry REQUIRED — indefinite grandfather forbidden.

self/native/hexa_v2.bak_20260418 | * | seed-freeze in effect | 2026-06-30
self/native/hexa_v2.bak_20260419 | * | seed-freeze            | 2026-06-30
```

## CLI

```
./hexa tool/raw_exemptions.hexa list                     # all entries
./hexa tool/raw_exemptions.hexa list <rule_id>           # one rule
./hexa tool/raw_exemptions.hexa add <rule_id> <file> <range> <reason> <expiry>
./hexa tool/raw_exemptions.hexa check                    # exit 1 if any expired
```

`raw_all` calls `check` at gate start — any expired entry blocks the unlock gate.
Entries within 7 days of expiry are printed as `WARN` but do not block.

## Why "no indefinite grandfather"

Under `raw#0` every exemption is a debt that must be paid back. An open-ended
grandfather list is indistinguishable from never enforcing the rule at all.
Forcing an expiry date makes the debt visible and surfaces stale exemptions
via `check`, so they either get re-justified (extended) or fixed (removed).
