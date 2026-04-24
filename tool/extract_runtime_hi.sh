#!/usr/bin/env bash
# tool/extract_runtime_hi.sh — reproducible extraction:
#   self/runtime_hi.hexa -> self/runtime_hi_gen.c
#
# Pipeline:
#   1) self/native/hexa_v2 transpiles the .hexa SSOT to raw .c
#   2) awk extracts [static HexaVal __hexa_sl_0 .. rt_str_center closing]
#   3) sed renames __hexa_sl_* -> __hexa_rt_sl_* and __hexa_strlit_init ->
#      __hexa_rt_strlit_init (avoid collision with user gen.c in same TU)
#   4) sed marks rt_str_* definitions `static` (internal linkage; shims in
#      runtime.c still export hexa_str_* with external linkage)
#   5) appends __attribute__((constructor)) so statics are populated before
#      main() runs — runtime.c has no entry of its own.
#
# This file is included by self/runtime.c AFTER the definition of
# hexa_str_join (runtime.c ~L3872) because rt_str_pad_left/pad_right/
# repeat/center/lines depend on hexa_str_join (and rt_str_lines on
# rt_str_split which is defined inside this file).
#
# hxa-20260423-003 M1-lite Step 4.

set -euo pipefail
HX="${HX_ROOT:-$(cd "$(dirname "$0")/.."; pwd)}"
HV2="${HEXA_V2:-$HX/self/native/hexa_v2}"
SRC="$HX/self/runtime_hi.hexa"
TMP=$(mktemp -d)
OUT="$HX/self/runtime_hi_gen.c"

if [[ ! -x "$HV2" ]]; then
    echo "[extract_runtime_hi] ERROR: hexa_v2 not found at $HV2" >&2
    exit 1
fi
if [[ ! -f "$SRC" ]]; then
    echo "[extract_runtime_hi] ERROR: SSOT missing: $SRC" >&2
    exit 1
fi

"$HV2" "$SRC" "$TMP/raw.c" >/dev/null

{
  cat <<'HDR'
// GENERATED FROM self/runtime_hi.hexa — do not edit manually.
// Source of truth: self/runtime_hi.hexa (M1-lite hi-layer SSOT).
// Reproduce: tool/extract_runtime_hi.sh (runs self/native/hexa_v2 then
// strips main/selftest, renames __hexa_sl_* -> __hexa_rt_sl_*, and marks
// rt_str_* as static).
// Included from self/runtime.c AFTER hexa_str_join is defined.
// (hxa-20260423-003 Step 4 — extraction replaces hand-port rt_str_*)

HDR

  awk '
    /^static HexaVal __hexa_sl_0;/ { in_block=1 }
    in_block && /^HexaVal runtime_hi_selftest/ { in_block=0 }
    in_block { print }
  ' "$TMP/raw.c" \
  | sed -E '
      s/__hexa_sl_/__hexa_rt_sl_/g;
      s/__hexa_strlit_init/__hexa_rt_strlit_init/g;
      s/^HexaVal (rt_str_[a-z_]+)\(/static HexaVal \1(/g;
    '

  cat <<'FTR'

__attribute__((constructor))
static void __hexa_rt_strlit_init_ctor(void) { __hexa_rt_strlit_init(); }
FTR
} > "$OUT"

echo "[extract_runtime_hi] wrote $OUT"
wc -l "$OUT"
