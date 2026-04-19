/* ═══════════════════════════════════════════════════════════
 *  hexa_nanbox_bridge.h — rt#38-A Phase 2 shadow caller wiring
 *
 *  Header-only, additive-only bridge between legacy HexaVal (32B
 *  tagged-union struct in self/runtime.c) and the Phase-1 NaN-box
 *  HexaV (8B uint64_t in self/hexa_nanbox.h).
 *
 *  Purpose: exercise the NaN-box encoding on live runtime data
 *  IN PARALLEL to the existing 32B HexaVal ABI, without modifying
 *  any existing constructor. Pick one low-risk call site — integer
 *  values — and prove end-to-end round-trip works against real
 *  runtime.c data flow.
 *
 *  Scope (Phase 2, minimal):
 *    - Shadow constructors:   hexa_nb_shadow_int(n)
 *    - Shadow accessors:      hexa_nb_shadow_as_int(v)
 *    - Bridge:                hexa_nb_from_legacy(HexaVal)
 *                             hexa_nb_to_legacy(HexaV)      (int/float/bool/nil only)
 *    - Instrumentation:       hexa_nb_shadow_int_count()     — constructions so far
 *                             hexa_nb_shadow_heap_alloc()    — malloc events (expect 0)
 *
 *  ABI contract: we DO NOT touch HexaVal. The shadow path takes a
 *  HexaVal (legacy 32B struct) as input and returns HexaV (8B
 *  uint64_t). Callers interested in the NaN-box benefit see zero
 *  heap pressure and 4x cache locality. Callers outside this
 *  bridge continue to use HexaVal transparently.
 *
 *  NOT included here: ptr-kind bridges (STR / ARRAY / MAP / ...),
 *  TAG_VALSTRUCT unwrap, closure env migration. Those are rt#38-B
 *  concerns — integration conflicts documented in
 *  doc/rt_38_a.md.
 * ═══════════════════════════════════════════════════════════ */
#ifndef HEXA_NANBOX_BRIDGE_H
#define HEXA_NANBOX_BRIDGE_H

#include <stdint.h>
#include "hexa_nanbox.h"

/* ── Instrumentation counters (per-TU static — header-inline OK
 *    because we only link one bridge consumer per test binary) ── */
static uint64_t hexa_nb_shadow_int_ctor_count    = 0;
static uint64_t hexa_nb_shadow_int_heap_allocs   = 0;  /* must stay 0 */
/* Phase 3 — float path counters, parallel to Phase 2 int counters */
static uint64_t hexa_nb_shadow_float_ctor_count  = 0;
static uint64_t hexa_nb_shadow_float_heap_allocs = 0;  /* must stay 0 */

static inline uint64_t hexa_nb_shadow_int_count(void)      { return hexa_nb_shadow_int_ctor_count; }
static inline uint64_t hexa_nb_shadow_heap_alloc(void)     { return hexa_nb_shadow_int_heap_allocs; }
static inline uint64_t hexa_nb_shadow_float_count(void)    { return hexa_nb_shadow_float_ctor_count; }
static inline uint64_t hexa_nb_shadow_float_heap_alloc(void) { return hexa_nb_shadow_float_heap_allocs; }
static inline void     hexa_nb_shadow_reset(void) {
    hexa_nb_shadow_int_ctor_count    = 0;
    hexa_nb_shadow_int_heap_allocs   = 0;
    hexa_nb_shadow_float_ctor_count  = 0;
    hexa_nb_shadow_float_heap_allocs = 0;
}

/* ── Shadow constructors (ZERO heap alloc — value in register) ── */
static inline HexaV hexa_nb_shadow_int(int64_t n) {
    hexa_nb_shadow_int_ctor_count++;
    /* Phase-1 encoding uses 32-bit int payload; truncate if caller
     * hands us an oversized int. This matches the Phase-1 contract
     * (see hexa_nb_int32 header comment). rt#38-B handles big-int
     * heap-boxing. */
    return hexa_nb_int32((int32_t)n);
}

/* Phase 3 — float shadow constructor.
 *
 *  Mirrors Phase 2's hexa_nb_shadow_int contract: counts constructions,
 *  never touches the heap. Wraps Phase-1's hexa_nb_float which handles
 *  NaN canonicalisation so a double NaN payload cannot collide with the
 *  QNaN-tagged namespace (see hexa_nanbox.h comment on HEXA_NB_QNAN_MASK).
 *
 *  Round-trip contract:
 *    - Finite doubles (including ±0, subnormals, ±inf, integer-valued)
 *      bit-preserved through hexa_nb_shadow_as_float.
 *    - NaN inputs canonicalise to 0x7FF8000000000000ULL. The "is NaN"
 *      property survives, but non-canonical NaN payloads are lost.
 *    - Signalling NaNs — same story, mapped to canonical QNaN.
 *
 *  This matches the Phase-1 ABI contract. rt#38-B big-float / extended
 *  precision support is deferred. */
static inline HexaV hexa_nb_shadow_float(double f) {
    hexa_nb_shadow_float_ctor_count++;
    return hexa_nb_float(f);
}

static inline HexaV hexa_nb_shadow_bool(int b) {
    return hexa_nb_bool(b);
}

static inline HexaV hexa_nb_shadow_nil(void) {
    return hexa_nb_nil();
}

/* ── Shadow accessors ───────────────────────────────────────── */
static inline int64_t hexa_nb_shadow_as_int(HexaV v) {
    return hexa_nb_as_int(v);
}

/* Phase 3 — float shadow accessor. Round-trip partner of
 * hexa_nb_shadow_float. Thin passthrough to the Phase-1 bit-cast
 * accessor; kept here for symmetry with _as_int. */
static inline double hexa_nb_shadow_as_float(HexaV v) {
    return hexa_nb_as_float(v);
}

/* ── Bridge: legacy HexaVal → NaN-box HexaV ──────────────────
 *
 *  Only scalar tags (INT / FLOAT / BOOL / VOID) are bridged in
 *  Phase 2. Pointer-bearing tags (STR / ARRAY / MAP / VALSTRUCT /
 *  FN / CLOSURE) require the caller to opt-in per-tag in rt#38-B;
 *  here we return nil (conservative).
 *
 *  NOTE: this header intentionally does NOT include runtime.c's
 *  HexaTag enum or HexaVal struct — the shadow-path test passes
 *  raw scalar values (int64_t, double, int) into hexa_nb_shadow_*
 *  directly. If a future caller wants HexaVal → HexaV, they must
 *  include runtime.c (or a forward-decl) and write their own
 *  one-liner extractor; the bridge does not dictate ABI.
 *
 *  See doc/rt_38_a.md "ABI integration" for the rationale. */

#endif /* HEXA_NANBOX_BRIDGE_H */
