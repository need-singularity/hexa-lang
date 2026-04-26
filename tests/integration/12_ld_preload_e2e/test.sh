#!/usr/bin/env bash
# 12_ld_preload_e2e — Phase 11 LD_PRELOAD enforce smoke-test (raw#7 F5/F6 + raw#8/#9).
#
# Verifies that hexa-runner image (production Dockerfile docker/runner/Dockerfile)
# carries native_gate.so as LD_PRELOAD and the shim returns EPERM at the VFS
# boundary for banned write paths.  Builds a tiny diag program that calls open()
# directly (skipping any shell or hexa indirection) so the test is independent
# of busybox-static / hexa-cli static-musl bypass surfaces.
#
# Test matrix (5 cases):
#   T1  raw#9/raw#8  /work/banned.py            → expect EPERM (errno=1)
#   T2  raw#7 F5     /work/core/hexa-lang/foo/loner.txt    → expect EPERM
#   T3  raw#7 F6     /work/core/hexa-lang/foo/foo.txt      → expect EPERM
#   T4  allowlist    /work/legitimate.txt       → expect OK
#   T5  opt-out      CLAUDX_NO_SANDBOX=1 + .py  → expect OK
#
# Skip rules:
#   - rc=77 if docker unavailable on this host
#   - rc=77 if no hexa-runner:* image with LD_PRELOAD env (Phase 11 image)
#   - rc=77 if compiler (debian:12-slim arm64) cannot pull (offline)
#
# This test exists as forward documentation of the actual enforce surface; it
# is the integration-level companion to self/native/native_gate.c unit logic.

set -u
TAG="${HEXA_RUNNER_TAG:-}"

# Find a hexa-runner image with LD_PRELOAD set (Phase 11+).
if [ -z "$TAG" ]; then
    if ! command -v docker >/dev/null 2>&1; then
        echo "SKIP: docker not on PATH"
        exit 77
    fi
    for candidate in hexa-runner:phase11-test hexa-runner:latest hexa-runner-distroless:test; do
        env_json="$(docker inspect --format='{{json .Config.Env}}' "$candidate" 2>/dev/null || true)"
        case "$env_json" in
            *LD_PRELOAD*native_gate*) TAG="$candidate"; break ;;
        esac
    done
fi
if [ -z "$TAG" ]; then
    echo "SKIP: no hexa-runner image with LD_PRELOAD=/opt/hexa/native_gate.so found."
    echo "      Build with:  docker buildx build --platform linux/arm64 \\"
    echo "                       -f docker/runner/Dockerfile -t hexa-runner:phase11-test --load ."
    exit 77
fi
echo "[12_ld_preload_e2e] using image: $TAG"

# Working dir for diag program + test outputs (host tmpdir, mounted as /work).
WORK="$(mktemp -d -t hexa_smoke_e2e.XXXXXX)"
trap 'rm -rf "$WORK"' EXIT

# Compile diag.c via debian:12-slim (matches runner libc, glibc-aarch64).
cat > "$WORK/diag.c" <<'EOF'
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s PATH\n", argv[0]); return 2; }
    int fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        fprintf(stderr, "open(%s) FAIL errno=%d (%s)\n", argv[1], errno, strerror(errno));
        return 1;
    }
    write(fd, "hi\n", 3);
    close(fd);
    fprintf(stderr, "open(%s) OK\n", argv[1]);
    return 0;
}
EOF

# Detect arch — we test on whatever the host docker engine runs.
ARCH="$(uname -m)"
case "$ARCH" in
    arm64|aarch64) PLATFORM="linux/arm64" ;;
    x86_64|amd64)  PLATFORM="linux/amd64" ;;
    *) echo "SKIP: unsupported arch $ARCH"; exit 77 ;;
esac

echo "[12_ld_preload_e2e] compiling diag.c in debian:12-slim ($PLATFORM)..."
if ! docker run --rm --platform "$PLATFORM" -v "$WORK:/work" -w /work \
        debian:12-slim sh -c \
        'apt-get update >/dev/null 2>&1 && apt-get install -y -q gcc libc6-dev >/dev/null 2>&1 && gcc -O2 -o diag diag.c' >/dev/null 2>&1; then
    echo "SKIP: cannot compile diag.c (offline? debian:12-slim pull failed?)"
    exit 77
fi
[ -x "$WORK/diag" ] || { echo "FAIL: diag binary not produced"; exit 1; }

mkdir -p "$WORK/core/hexa-lang/foo"

PASS=0
FAIL=0
results=""

run_case() {
    local label="$1" expect="$2" path="$3" extra_env="${4:-}"
    rm -f "$path" 2>/dev/null
    local docker_args="--rm -v $WORK:/work --entrypoint /work/diag $extra_env $TAG"
    local out rc
    out="$(docker run $docker_args "$path" 2>&1)"; rc=$?
    local got_eperm=0
    case "$out" in *"FAIL errno=1"*) got_eperm=1 ;; esac
    local got_ok=0
    case "$out" in *"OK"*) got_ok=1 ;; esac

    if [ "$expect" = "EPERM" ]; then
        if [ $got_eperm -eq 1 ] && [ $rc -ne 0 ]; then
            echo "  PASS $label  (EPERM as expected)"
            PASS=$((PASS+1)); results="$results,$label=PASS"
        else
            echo "  FAIL $label  (expected EPERM, got rc=$rc out=$out)"
            FAIL=$((FAIL+1)); results="$results,$label=FAIL"
        fi
    else  # OK
        if [ $got_ok -eq 1 ] && [ $rc -eq 0 ]; then
            echo "  PASS $label  (OK as expected)"
            PASS=$((PASS+1)); results="$results,$label=PASS"
        else
            echo "  FAIL $label  (expected OK, got rc=$rc out=$out)"
            FAIL=$((FAIL+1)); results="$results,$label=FAIL"
        fi
    fi
}

echo "[12_ld_preload_e2e] running 5-case matrix"
run_case "T1_raw9_py"      EPERM "/work/banned.py"
run_case "T2_raw7_F5"      EPERM "/work/core/hexa-lang/foo/loner.txt"
run_case "T3_raw7_F6"      EPERM "/work/core/hexa-lang/foo/foo.txt"
run_case "T4_allowlist"    OK    "/work/legitimate.txt"
run_case "T5_optout"       OK    "/work/optout.py" "-e CLAUDX_NO_SANDBOX=1"

echo "[12_ld_preload_e2e] results: PASS=$PASS FAIL=$FAIL ($results)"
if [ $FAIL -ne 0 ]; then
    exit 1
fi
echo "PASS: 5/5 LD_PRELOAD enforce surfaces verified"
exit 0
