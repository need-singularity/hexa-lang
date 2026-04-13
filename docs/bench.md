# `hexa bench` — Benchmark Harness (bt#81)

Self-hosted micro-benchmark runner integrated into the `hexa` CLI.
Wraps target `.hexa` scripts with `/usr/bin/time` + `HEXA_ALLOC_STATS=1`
to capture wall-clock, RSS, and runtime allocation counters across N
repetitions, then reports median / min / max.

## Usage

```
hexa bench <file> [--runs N] [--json] [--compare <other_bin>]
```

Options:

| flag                     | default | effect                                                 |
| ------------------------ | ------- | ------------------------------------------------------ |
| `--runs N`               | 3       | Repetitions; median across N is the headline number.   |
| `--json`                 | off     | Emit single-line JSON instead of the human table.      |
| `--compare <other_bin>`  | —       | Run the same file under a second binary; print delta%. |

Examples:

```bash
hexa bench bench/fib_rec.hexa --runs 5
hexa bench bench/probe.hexa --json
hexa bench bench/alloc_heavy.hexa --compare /Users/ghost/Dev/hexa-lang/build/hexa_stage0_prev
```

## Sample Output

Table mode:

```
=== hexa bench ===
  binary:    /Users/ghost/Dev/hexa-lang/hexa
  file:      bench/alloc_heavy.hexa
  runs:      3

  metric                median        min           max
  ────────────────────  ────────────  ────────────  ────────────
  wall (s)              0.16          0.16          0.18
  rss  (KB)             27584         27376         27648

  HEXA_ALLOC_STATS (median)
    array_new:  40484
    array_push: 91420
    array_grow: 20139
    map_new:    126
    map_set:    0
```

JSON mode:

```json
{"file":"bench/probe.hexa","runs":3,"median_wall_s":0.00,"min_wall_s":0.00,"max_wall_s":0.00,"median_rss_kb":1760,"min_rss_kb":1760,"max_rss_kb":1888,"alloc_stats":{"array_new":409,"array_push":559,"array_grow":38,"map_new":13,"map_set":0}}
```

Compare mode (positive delta% = B is slower / heavier than A):

```
=== hexa bench --compare ===
  file:      bench/alloc_heavy.hexa
  runs:      2
  A:         /Users/ghost/Dev/hexa-lang/.claude/worktrees/bt-81/hexa
  B:         /Users/ghost/Dev/hexa-lang/hexa

  metric         A (median)    B (median)    delta%
  ─────────────  ────────────  ────────────  ────────
  wall (s)       0.10          0.11          +10.00%
  rss  (KB)      27952         27520         -1.55%

  alloc-stats (median: A → B)
    array_new:  40484 → 40484
    array_push: 91420 → 91420
    array_grow: 20139 → 20139
    map_new:    126 → 126
    map_set:    0 → 0
```

## How the numbers are gathered

1. The harness invokes the target binary inside `/usr/bin/time -l` (macOS,
   BSD format) or `/usr/bin/time -v` (Linux, GNU format) with
   `HEXA_ALLOC_STATS=1` exported in the environment.
2. The wrapper command sends *target stdout* to `/dev/null` (we do not care
   about workload output) and *time + alloc-stats stderr* to a per-PID
   tempfile under `/tmp/hexa_bench_stats.<PID>.<RAND>.txt`.
3. `awk` extracts:
   - wall: `0.07 real` (BSD) or `Elapsed (wall clock) time ... 0:00.07`
     (GNU; converted to seconds).
   - RSS: `maximum resident set size` bytes (BSD; divided by 1024) or
     `Maximum resident set size (kbytes)` KB (GNU).
4. `grep '\[HEXA_ALLOC_STATS\]' | tail -1 | awk` pulls the
   `array_new=N push=N grow=N map_new=N map_set=N` counters from the
   single atexit dump line. `tail -1` is necessary because both the
   parent `./hexa` dispatcher and the stage0 child can print stats; we
   take the last one (= the workload).
5. Each metric's run-to-run array is sorted via shell `sort -g`; the
   middle index gives the median. min/max via `head -1` / `tail -1`.

## Routing rule (which interpreter actually runs the file)

The harness picks the *interpreter* based on `bin`:

| `bin` basename matches | what runs                            | why                                              |
| ---------------------- | ------------------------------------ | ------------------------------------------------ |
| `hexa_stage0*`         | `bin <file>` directly                | bin is already an interpreter                    |
| anything, but `<dir>/build/hexa_stage0` exists | `<dir>/build/hexa_stage0 <file>`    | bin is the dispatcher (`./hexa run <file>`); we bypass to avoid double-fork that drops the child's `HEXA_ALLOC_STATS` dump |
| neither                | `bin run <file>`                     | last-resort dispatcher path                      |

The dispatcher bypass is mandatory: otherwise the alloc counters reflect
only the `./hexa` parent process (a few hundred allocations) and not the
workload itself (tens of thousands).

## Standard suite (`bench/`)

| file                | what it stresses                                                         |
| ------------------- | ------------------------------------------------------------------------ |
| `probe.hexa`        | Cold-start: lex + parse + interpret a single `println("ok")`.            |
| `fib_rec.hexa`      | Function call dispatch — `fib(30)` ≈ 2.7M recursive calls.               |
| `alloc_heavy.hexa`  | Array allocation pressure — 4-element array × 10K iterations.            |
| `import_chain.hexa` | Module loader — `import` of self/lexer.hexa + self/parser.hexa.          |
| `simple_loop.hexa`  | `BinOp + Assign + while` interpreter dispatch — 100K arithmetic ops.     |

## Adding a new bench

1. Create `bench/<name>.hexa`.
2. Make it self-contained (no external state, deterministic output).
3. Document at the top in a comment what the bench *isolates*. Pure
   dispatch? Allocation? IO? Module loading?
4. Verify `hexa run bench/<name>.hexa` succeeds on its own.
5. Verify `hexa bench bench/<name>.hexa --runs 3` produces sensible
   numbers (wall > 0, alloc counters non-zero if you expected allocs).

## Interpretation tips

- **wall_s on the order of `0.00`** means the bench is shorter than the
  10ms `/usr/bin/time` resolution. Increase the workload (loop count) or
  switch to an in-process micro-bench using `clock()` (see
  `self/bench_env_get.hexa`).
- **RSS jitter of a few hundred KB** between runs is normal on macOS
  due to copy-on-write fork overhead. Use median, not min/max, as the
  headline number.
- **alloc counters constant across runs** is expected — the workload is
  deterministic. If they vary, the bench has nondeterminism (RNG,
  iteration over a map, etc.) and should be fixed.
- **`--compare` delta% > +5%** with allocation counters identical points
  to a hot-path slowdown (interpreter dispatch, codegen). delta% with
  alloc counters changed reflects allocator behavior, not necessarily
  speed.

## Known limitations

- Wall resolution is 10 ms (`/usr/bin/time` granularity). For
  ns/op-level work, use the in-process `clock()`-based benches in
  `self/bench_*.hexa`.
- The harness assumes `/usr/bin/time` is present (true on macOS and most
  Linux distros). BusyBox `time` is *not* compatible.
- `--compare` always benches the same `<file>` under two binaries; for
  multi-file matrices, script the harness in the shell.
- `HEXA_ALLOC_STATS` requires the runtime built with the counter hooks
  in `self/runtime.c` (rt 32 family). Older binaries print no stats →
  counters report `0` across the board.
