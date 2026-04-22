/* ═══════════════════════════════════════════════════════════════════
 * self/native/fp_init.c — B20 / roadmap 55 Phase 1
 *
 * Per-thread FP control-word normalization for IEEE 754 strict mode.
 *
 * Called unconditionally at main() entry (codegen_c2.hexa emits
 * `hexa_fp_init()` right after hexa_set_args). Idempotent — safe to
 * invoke multiple times or from any thread entry trampoline.
 *
 * Why per-thread: MXCSR (x86_64) and FPCR (aarch64) are thread-local
 * registers. OS/runtime may seed them with FTZ/DAZ/non-IEEE rounding
 * bits enabled (e.g. Intel MKL sets DAZ, Accelerate sets FZ on some
 * paths). Bit-exact cross-substrate FP requires explicit reset in
 * every thread that runs @strict_fp code.
 *
 * Phase 2 will wire hexa_fp_init() into a pthread_create() wrapper
 * (self/native/signal_flock.c pattern) so worker-pool threads inherit
 * the normalized state.
 *
 * Semantics:
 *   x86_64: clear FTZ (bit 15) + DAZ (bit 6) in MXCSR.
 *           Round-to-nearest-even is the default (RC=00), untouched.
 *   aarch64: zero FPCR — clears FZ (bit 24), FZ16 (bit 19), AH (bit 1),
 *            sets round-to-nearest-even (RMode=00), no trapping.
 *   other: no-op (the two-rounding FMA gate in codegen still holds).
 * ═══════════════════════════════════════════════════════════════════ */

#include <stdint.h>

#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#include <pmmintrin.h>
#endif

static int _hexa_fp_init_done = 0;

void hexa_fp_init(void) {
#if defined(__x86_64__) || defined(_M_X64)
    /* Clear FTZ (0x8000) and DAZ (0x0040) while preserving RC/other. */
    unsigned int csr = _mm_getcsr();
    csr &= ~0x8040u;
    _mm_setcsr(csr);
#elif defined(__aarch64__)
    /* FPCR ← 0: FZ=0, AH=0, RMode=00 (round-to-nearest-even). */
    uint64_t zero = 0;
    __asm__ volatile ("msr fpcr, %0" :: "r"(zero));
#else
    /* Other substrates (ppc64le, riscv64, …): no per-thread FP CSR to
     * normalize here. The codegen-side FMA gate still applies; strict
     * transcendental libm (Phase 2 crlibm vendoring) will carry the
     * remaining portability burden. */
#endif
    _hexa_fp_init_done = 1;
}

/* Observable for self-test / introspection. */
int hexa_fp_init_was_called(void) {
    return _hexa_fp_init_done;
}
