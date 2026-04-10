#!/bin/bash
# build_hexa.sh — hexa_v2-only pipeline PoC (cargo-free self-host)
#
# Goal: 100% absorption — replace cargo build with
#   hexa_v2 (native arm64)  →  C source  →  clang  →  hexa_v3 binary
#
# ⚠ This script is PARALLEL to build.sh. It does NOT touch Cargo.toml
#   or build.sh. Other agents are still porting src/ → self/.
#
# Usage:
#   ./build_hexa.sh                 # full pipeline (stage1→2→3)
#   ./build_hexa.sh stage1          # transpile only
#   ./build_hexa.sh stage2          # clang compile only
#   ./build_hexa.sh stage3          # self-test only
#   STAGE1_INPUT=self/foo.hexa ./build_hexa.sh

set -e
cd "$(dirname "$0")"

ROOT="$(pwd)"
HEXA_V2="${ROOT}/self/native/hexa_v2"
# NOTE: spec said self/main_compiler.hexa — that file does NOT exist.
# Closest candidate in tree is self/compiler.hexa (BLOCKER B1 below).
STAGE1_INPUT="${STAGE1_INPUT:-${ROOT}/self/compiler.hexa}"
STAGE1_OUT="${STAGE1_OUT:-/tmp/hexa_v3.c}"
STAGE2_BIN="${STAGE2_BIN:-${ROOT}/hexa_v3}"
STAGE3_TEST="${STAGE3_TEST:-${ROOT}/examples/calc_min.hexa}"
CLANG="${CLANG:-clang}"

log() { printf "\033[1;36m[build_hexa]\033[0m %s\n" "$*"; }
err() { printf "\033[1;31m[build_hexa:ERR]\033[0m %s\n" "$*" >&2; }

stage1() {
    log "Stage 1: hexa_v2  ${STAGE1_INPUT}  →  ${STAGE1_OUT}"
    [ -x "$HEXA_V2" ] || { err "hexa_v2 not found or not executable: $HEXA_V2"; exit 10; }
    [ -f "$STAGE1_INPUT" ] || { err "stage1 input missing: $STAGE1_INPUT (BLOCKER B1)"; exit 11; }
    "$HEXA_V2" "$STAGE1_INPUT" "$STAGE1_OUT"
    [ -s "$STAGE1_OUT" ] || { err "stage1 produced empty $STAGE1_OUT"; exit 12; }
    log "Stage 1 OK ($(wc -c <"$STAGE1_OUT") bytes)"
}

stage2() {
    log "Stage 2: $CLANG -O2 ${STAGE1_OUT}  →  ${STAGE2_BIN}"
    command -v "$CLANG" >/dev/null || { err "$CLANG not in PATH"; exit 20; }
    [ -f "$STAGE1_OUT" ] || { err "no C source to compile: $STAGE1_OUT (run stage1 first)"; exit 21; }
    "$CLANG" -O2 -std=c11 -o "$STAGE2_BIN" "$STAGE1_OUT"
    log "Stage 2 OK → $STAGE2_BIN"
}

stage3() {
    log "Stage 3: self-test  ${STAGE2_BIN}  ${STAGE3_TEST}"
    [ -x "$STAGE2_BIN" ] || { err "stage2 binary missing: $STAGE2_BIN"; exit 30; }
    [ -f "$STAGE3_TEST" ] || { err "test input missing: $STAGE3_TEST"; exit 31; }
    "$STAGE2_BIN" "$STAGE3_TEST" /tmp/hexa_v3_selftest.c || {
        err "stage3 self-test failed — hexa_v3 did not accept $STAGE3_TEST"
        exit 32
    }
    log "Stage 3 OK (self-test transpiled → /tmp/hexa_v3_selftest.c)"
}

case "${1:-all}" in
    stage1) stage1 ;;
    stage2) stage2 ;;
    stage3) stage3 ;;
    all)    stage1; stage2; stage3 ;;
    *)      err "unknown target: $1 (use stage1|stage2|stage3|all)"; exit 1 ;;
esac

cat <<'BLOCKERS'

Known blockers (must resolve before full cargo removal):
  B1  self/main_compiler.hexa (spec) does NOT exist — fallback: self/compiler.hexa
      TODO: port/rename a canonical top-level driver to self/main_compiler.hexa
  B2  alloc/ — Rust allocator (GC/RC) not yet ported to self/
  B3  ir/   — HEXA-IR Mk.I+ pass pipeline (σ=12) still Rust-only
  B4  wasm  — src/wasm.rs (cdylib, WASM playground) has no hexa_v2 path
  B5  cargo test (1349 tests) has no hexa_v2 equivalent harness
  B6  CI    — .github/workflows/ does not exist; nothing pins cargo today,
              so CI migration is deferred until stage1..3 is green locally.
BLOCKERS
