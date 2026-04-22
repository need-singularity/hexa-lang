# lint.hexa OOM reproduction — Docker sandbox

Experimental isolated environment to reproduce and debug the lint.hexa
out-of-memory growth that has repeatedly frozen the host Mac (24 GB RAM).

## Incident summary

On 2026-04-22 the machine froze while running normal Claude Code work.
CPU-resource diagnostics showed three `hexa_stage0.real` interpreter
processes each grown to 40–53 GB resident — ~146 GB combined, with
only 84 MB free pages and 11.3 GB in the compressor. The interpreter
runs after every file edit via `$HEXA_LANG/gate/lint.hexa` (4379 lines,
168 fns) and multiple instances had stacked up.

The runtime already has a hardcoded `call_user_fn` depth cap of 1000
(`self/hexa_full.hexa:9174`), so the 40+ GB cannot be explained by deep
recursion alone. The bug is either **shallow-nested recursion with
heavy per-frame allocation** or **unbounded loop accumulation** inside
lint.hexa (or one of the stdlib files it uses).

A `@watchdog(rss_mb=2048, time_s=120, on_breach="kill")` attribute was
added to `ai_lint_main` in commit `031e18f3` as a safety net on the
host. This directory adds a second, stronger layer: run the workload
in a Linux container with a 2 GB memory cgroup, so the kernel kills
the container on OOM and the host is never at risk.

## Why Docker (not just ulimit on macOS)

macOS does not reliably enforce `RLIMIT_AS` / `RLIMIT_RSS`, so a
`ulimit -v 2000000` on the host is not a real cap. Linux cgroups
(via `docker run --memory=2g --memory-swap=2g`) are enforced by the
kernel and guarantee containment.

## Architecture

```
host (macOS arm64)
 └── docker (Linux arm64 VM, hard-capped)
      ├── /usr/local/bin/hexa_bootstrap    (built once in image)
      ├── /repo  (read-only bind of hexa-lang source tree — live edits)
      └── /work  (writable tmpfs, 512 MB, build artifacts)
```

The image contains only the C bootstrap compiler. Editing `lint.hexa`
on the host is immediately visible inside the container through the
read-only bind mount — no image rebuild needed between experiments.

## Bootstrap chain

`hexa_stage0.real` on the host is a Mach-O binary — it does not run in
Linux. The image therefore compiles `self/bootstrap_compiler.c`
(1493 lines of C, self-contained, only needs `self/runtime.c` and
`<ctype.h>`) with `gcc -O2`. That gives a working hexa-to-C
transpiler inside the container.

Full bootstrap to a Linux hexa interpreter capable of running lint.hexa
end-to-end may require additional stages (build `hexa_full.hexa` from
the bootstrap, then compile `hexa_full.c` with gcc). Whether
`bootstrap_compiler.c` covers every feature `lint.hexa` uses is an
open question — this setup is the scaffold, the iteration is part of
the experiment.

## Usage

Prereq: Docker daemon running (Docker Desktop / Colima / OrbStack).

```
cd experimental/lint_oom_docker
make build          # build image with hexa_bootstrap (~30 s first time)
make smoke          # end-to-end "hello world" — validates the image itself
make shell          # drop into a 2 GB container, /repo mounted read-only
make compile-lint   # try transpiling lint.hexa (expected to expose gaps
                    # in bootstrap_compiler.c coverage on the first run)
make clean          # remove the image
```

Sanity check on the host (outside Docker) has already confirmed
`bootstrap_compiler.c` compiles cleanly with `clang -O2` and the
resulting binary successfully tokenises, parses, and emits C for a
trivial `fn main() { println("hello") }` input. The Linux gcc build
is expected to behave the same way.

Inside the container:

```
# Examples — adapt as bootstrap coverage allows
hexa_bootstrap /repo/gate/lint.hexa -o /work/lint.c
gcc /work/lint.c -o /work/lint
/usr/bin/time -v /work/lint              # observe peak RSS under 2 GB cap
```

## What to look for

- `Killed` + exit code `137` from docker → kernel OOM kill at 2 GB (bug reproduced).
- `/usr/bin/time -v` peak RSS trend across different inputs → isolates the offending file type.
- `gcc -fsanitize=address` (add to a debug variant of the Dockerfile) → where the allocation actually happens in the transpiled C.

## State

Experimental scaffolding. Not wired into CI. No guarantee
`bootstrap_compiler.c` transpiles `lint.hexa` successfully on the first
attempt — that discovery is part of the experiment.

Related commit on host: `031e18f3 fix(gate): @watchdog cap on lint main`.
