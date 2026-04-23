# hexa_v2 — Linux x86_64 pre-built binary

Cross-compiled from `self/native/hexa_cc.c` on macOS ARM64 via zig cc.
Static musl link — no glibc dependency, runs on Linux kernels ≥ 2.6.32.

## Artifact

- **Path**: `dist/linux-x86_64/hexa_v2`
- **Format**: ELF 64-bit LSB, x86-64, statically linked, stripped=no
- **SHA-256**: `3ff995fc8b68e3a5b9e46a803a269e03204ff0b439a668a6dfadc58acc01d496`
- **Built**: 2026-04-23
- **Source commit**: see `git log -1 dist/linux-x86_64/hexa_v2`

## Rebuild recipe

Requires zig on host (macOS or Linux ARM64/x86_64):

```
zig cc -target x86_64-linux-musl -O2 -std=gnu11 -D_GNU_SOURCE \
       -Wno-trigraphs -I self \
       self/native/hexa_cc.c \
       -o dist/linux-x86_64/hexa_v2 -lm
```

Alternative (host must be x86_64 Linux or have x86_64-linux-musl-gcc):

```
x86_64-linux-musl-gcc -O2 -std=gnu11 -D_GNU_SOURCE -Wno-trigraphs \
       -I self self/native/hexa_cc.c \
       -o dist/linux-x86_64/hexa_v2 -static -lm
```

## Usage on Linux pod

```
./dist/linux-x86_64/hexa_v2 path/to/source.hexa path/to/out.c
```

This is the **stage0 transpiler** — takes `.hexa` input, emits `.c` for clang.
Not to be confused with the `./hexa` wrapper which is a shell script driver.

## Unblocks

- `nxs-20260422-006` (nexus) — Linux pod 'Exec format error'
- `agm-20260422-003` (airgenome)
- `agm-20260422-006` (airgenome) — resource_gap prio 95
- `anima-20260422-003` (anima)

## Rebuild automation

See `tool/build_linux_x86_64.hexa` for scripted rebuild + sha verify.
