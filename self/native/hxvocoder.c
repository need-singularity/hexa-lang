// hxvocoder.c — hexa FFI ↔ HEXA-SPEAK Mk.III neural vocoder shim
//
// Goal: replace the pure-hexa hot loops in
// anima/anima-speak/neural_vocoder.hexa with a flat C C-ABI shim so the
// 24kHz × 1s additive synthesis (n_partials × n_samples = 6 × 24000 =
// 144,000 sin/exp/clip ops) drops from ~12s/sec interpreter time into
// the libm scalar floor. Same shape as hxqwen14b.c / hxlayer.c — int64_t
// pointer marshalling so HX FFI's int-only positional convention holds.
//
// v1 STATUS: REAL — six functions, no stubs. All hot paths port
// directly from neural_vocoder.hexa with bit-faithful semantics
// (within fp32 vs fp64 rounding). Designed to be parity-checked
// against the pure-hexa reference at 1e-5 abs tolerance.
//
// ABI surface (six symbols — all int64_t IN, all float* via int64_t):
//
//   int64_t hxvocoder_version(void)
//     ABI version. Returns 1.
//
//   void hxvocoder_vec_zeros(int64_t out_p, int64_t n)
//     Zero `n` consecutive float32 slots starting at `out_p`. Pure
//     memset — exists so callers can avoid the heap alloc churn that
//     dominates pure-hexa vec_zeros.
//
//   void hxvocoder_linear_proj(
//       int64_t y_p, int64_t x_p, int64_t W_p, int64_t b_p,
//       int64_t in_dim, int64_t out_dim, int64_t has_bias)
//     y[out_dim] = W[out_dim x in_dim] · x[in_dim] (+ b[out_dim] if has_bias!=0)
//     Scalar GEMV. has_bias is an int64_t (0/1) since FFI carries no
//     bool. b_p may be 0 when has_bias == 0.
//     Note: BLAS integration is intentionally deferred to a sibling
//     hxblas wiring task — this shim stays libm-only.
//
//   void hxvocoder_tanh_vec(int64_t y_p, int64_t x_p, int64_t n)
//     y[i] = tanhf(x[i])  for i in [0, n)
//
//   void hxvocoder_synth_additive(
//       int64_t out_p,        // float* [n_samples] OUT
//       int64_t ctrl_p,       // float* [n_frames * ctrl_dim] flat
//       int64_t phase_p,      // float* [n_partials] IN/OUT (carried across calls)
//       int64_t n_frames,
//       int64_t n_samples,
//       int64_t n_partials,
//       int64_t ctrl_dim,     // = n_partials * 2
//       int64_t hop,
//       double  sample_rate,
//       double  base_freq_hz,
//       double  max_gain)
//     24kHz additive synthesis hot loop (n_samples × n_partials sin/exp).
//     ctrl is row-major [n_frames][ctrl_dim] with field layout
//       ctrl[f][p*2]   = log-frequency offset for partial p (in [-1, +1])
//       ctrl[f][p*2+1] = gain                              (in [-1, +1])
//     phase[] is the per-partial phase accumulator (radians) — caller
//     passes mutable scratch pre-zeroed; final phase persists in-place
//     so chunked calls can continue without phase discontinuity.
//
//   void hxvocoder_decode_wave(
//       int64_t out_p,        // float* [n_frames*hop] OUT
//       int64_t latent_p,     // float* [n_frames * latent_dim] flat
//       int64_t W_ctrl_p,     // float* [ctrl_dim * latent_dim] row-major
//       int64_t scratch_ctrl_p,   // float* [n_frames * ctrl_dim]
//       int64_t scratch_phase_p,  // float* [n_partials] zeroed by callee
//       int64_t n_frames,
//       int64_t latent_dim,
//       int64_t ctrl_dim,
//       int64_t n_partials,
//       int64_t hop,
//       double  sample_rate,
//       double  base_freq_hz,
//       double  max_gain)
//     Wraps Stage A (latent→ctrl via W_ctrl projection + tanh) AND Stage
//     B (additive synth) in one dispatch. Ten args is over the FFI 6-arg
//     ceiling — wrapper must pack into a struct or call the smaller
//     primitives individually. Kept here for parity tests that prefer
//     a single fused entry, and for non-Hexa C consumers.
//
// Build (Linux x86_64):
//   bash scripts/build_hxvocoder_linux.sh
// Cross-verify (Mac):
//   bash scripts/build_hxvocoder_linux.sh --mac-xverify
// Mac-native live FFI:
//   bash scripts/build_hxvocoder_linux.sh --mac-native
//
// .hexanoport marker convention:
//   This .c is a native shim (extern FFI boundary), not a compilation
//   target for the Hexa-to-C codegen.

#include <math.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

// ─────────────────────────────────────────────────────────────
// hxvocoder_version — bump on any breaking ABI change.
// v1 = real implementation (no stubs).
// ─────────────────────────────────────────────────────────────
int64_t hxvocoder_version(void) {
    return 1;
}

// ─────────────────────────────────────────────────────────────
// hxvocoder_vec_zeros — fast bulk-zero. Replaces the pure-hexa
// vec_zeros which builds a list via repeated push (O(n) heap allocs).
// ─────────────────────────────────────────────────────────────
void hxvocoder_vec_zeros(int64_t out_p, int64_t n) {
    if (out_p == 0 || n <= 0) return;
    float* o = (float*)(uintptr_t)out_p;
    memset(o, 0, (size_t)n * sizeof(float));
}

// ─────────────────────────────────────────────────────────────
// hxvocoder_linear_proj — scalar GEMV.
//   y[i] = sum_j W[i*in_dim + j] * x[j]  (+ b[i] if has_bias)
// Pure scalar O3 auto-vec is sufficient for the (CTRL_DIM=12,
// LATENT_DIM=384) hot path used by the vocoder. BLAS sgemv via hxblas
// is the obvious upgrade (G agent territory).
// ─────────────────────────────────────────────────────────────
void hxvocoder_linear_proj(int64_t y_p, int64_t x_p, int64_t W_p,
                           int64_t b_p, int64_t in_dim, int64_t out_dim,
                           int64_t has_bias) {
    if (y_p == 0 || x_p == 0 || W_p == 0) return;
    if (in_dim <= 0 || out_dim <= 0) return;

    float* y = (float*)(uintptr_t)y_p;
    const float* x = (const float*)(uintptr_t)x_p;
    const float* W = (const float*)(uintptr_t)W_p;
    const float* b = (has_bias != 0 && b_p != 0)
        ? (const float*)(uintptr_t)b_p : NULL;

    for (int64_t i = 0; i < out_dim; i++) {
        float s = (b != NULL) ? b[i] : 0.0f;
        const float* Wi = W + (size_t)i * (size_t)in_dim;
        for (int64_t j = 0; j < in_dim; j++) {
            s += Wi[j] * x[j];
        }
        y[i] = s;
    }
}

// ─────────────────────────────────────────────────────────────
// hxvocoder_tanh_vec — elementwise tanh. Real tanhf, no Padé hack:
// the audible-amplitude smoke-check needs proper saturation behaviour
// at |x| > 2 (a fast tanh approximation can introduce DC drift).
// ─────────────────────────────────────────────────────────────
void hxvocoder_tanh_vec(int64_t y_p, int64_t x_p, int64_t n) {
    if (y_p == 0 || x_p == 0 || n <= 0) return;
    float* y = (float*)(uintptr_t)y_p;
    const float* x = (const float*)(uintptr_t)x_p;
    for (int64_t i = 0; i < n; i++) {
        y[i] = tanhf(x[i]);
    }
}

// ─────────────────────────────────────────────────────────────
// hxvocoder_synth_additive — 24kHz additive synth hot loop.
//
// Mirrors neural_vocoder.hexa::synth_additive bit-faithfully (modulo
// fp32 rounding):
//   * locate surrounding frames f0, f1
//   * linearly interp gain & freq-offset across [f0, f1]
//   * convert offset to actual frequency: nominal * 2^offset
//   * advance per-partial phase accumulator
//   * accumulate s += gain * MAX_GAIN * sin(phase)
//
// Phase wrap-around: keep phase bounded in [-2π, 2π] each iteration so
// sinf precision does not degrade on long runs.
//
// TWO_PI baked at fp64 precision, cast to fp32 for the hot loop.
// LN2 likewise — exp(offset * LN2) === 2^offset.
// ─────────────────────────────────────────────────────────────
void hxvocoder_synth_additive(int64_t out_p, int64_t ctrl_p,
                              int64_t phase_p,
                              int64_t n_frames, int64_t n_samples,
                              int64_t n_partials, int64_t ctrl_dim,
                              int64_t hop, double sample_rate,
                              double base_freq_hz, double max_gain) {
    if (out_p == 0 || ctrl_p == 0 || phase_p == 0) return;
    if (n_samples <= 0 || n_partials <= 0 || ctrl_dim <= 0) return;
    if (n_frames <= 0 || hop <= 0 || sample_rate <= 0.0) return;

    float* out = (float*)(uintptr_t)out_p;
    const float* ctrl = (const float*)(uintptr_t)ctrl_p;
    float* phase = (float*)(uintptr_t)phase_p;

    const float TWO_PI    = (float)6.283185307179586;
    const float LN2       = (float)0.6931471805599453;
    const float inv_sr    = (float)(1.0 / sample_rate);
    const float base_freq = (float)base_freq_hz;
    const float gain_max  = (float)max_gain;
    const float inv_hop   = 1.0f / (float)hop;

    for (int64_t t = 0; t < n_samples; t++) {
        // Locate surrounding frames (matches hexa: frame_pos = t/HOP).
        const float frame_pos = (float)t * inv_hop;
        int64_t f0 = (int64_t)frame_pos;
        if (f0 < 0) f0 = 0;
        if (f0 >= n_frames) f0 = n_frames - 1;
        int64_t f1 = f0 + 1;
        if (f1 >= n_frames) f1 = n_frames - 1;
        const float alpha = frame_pos - (float)f0;
        const float one_minus_alpha = 1.0f - alpha;

        const float* row0 = ctrl + (size_t)f0 * (size_t)ctrl_dim;
        const float* row1 = ctrl + (size_t)f1 * (size_t)ctrl_dim;

        float s = 0.0f;
        for (int64_t p = 0; p < n_partials; p++) {
            const int64_t off_idx  = p * 2;
            const int64_t gain_idx = p * 2 + 1;

            const float off_a = row0[off_idx];
            const float off_b = row1[off_idx];
            const float off   = off_a * one_minus_alpha + off_b * alpha;

            const float g_a = row0[gain_idx];
            const float g_b = row1[gain_idx];
            const float g   = g_a * one_minus_alpha + g_b * alpha;

            // freq = BASE * (p+1) * 2^offset
            const float nominal = base_freq * (float)(p + 1);
            const float scale   = expf(off * LN2);
            const float freq    = nominal * scale;

            // phase advance + bounded wrap
            float ph = phase[p] + TWO_PI * freq * inv_sr;
            if (ph > TWO_PI) ph -= TWO_PI;
            if (ph < 0.0f)   ph += TWO_PI;
            phase[p] = ph;

            s += g * gain_max * sinf(ph);
        }
        out[t] = s;
    }
}

// ─────────────────────────────────────────────────────────────
// hxvocoder_decode_wave — Stage A + Stage B fused.
//
// One dispatch walks:
//   for f in 0..n_frames:
//       ctrl[f] = tanh(W_ctrl @ latent[f])     (Stage A)
//   synth_additive(out, ctrl, ...)             (Stage B)
//
// scratch_ctrl_p must be sized [n_frames * ctrl_dim] floats.
// scratch_phase_p must be sized [n_partials] floats — zeroed by callee.
//
// Note: 13 args overflow the standard hexa FFI 6-arg ceiling. The
// Hexa-side wrapper either packs into a struct (see hxlmhead pattern)
// or calls hxvocoder_linear_proj + hxvocoder_tanh_vec + hxvocoder_synth_additive
// directly. This entry exists for the parity test and for native
// consumers that can drive 13 args fine (e.g. a Python ctypes test).
// ─────────────────────────────────────────────────────────────
void hxvocoder_decode_wave(int64_t out_p, int64_t latent_p, int64_t W_ctrl_p,
                           int64_t scratch_ctrl_p, int64_t scratch_phase_p,
                           int64_t n_frames, int64_t latent_dim,
                           int64_t ctrl_dim, int64_t n_partials,
                           int64_t hop, double sample_rate,
                           double base_freq_hz, double max_gain) {
    if (out_p == 0 || latent_p == 0 || W_ctrl_p == 0) return;
    if (scratch_ctrl_p == 0 || scratch_phase_p == 0) return;
    if (n_frames <= 0 || latent_dim <= 0 || ctrl_dim <= 0) return;
    if (n_partials <= 0 || hop <= 0) return;

    const float* latent  = (const float*)(uintptr_t)latent_p;
    const float* W_ctrl  = (const float*)(uintptr_t)W_ctrl_p;
    float* ctrl_scratch  = (float*)(uintptr_t)scratch_ctrl_p;
    float* phase         = (float*)(uintptr_t)scratch_phase_p;

    // Zero phase accumulator (caller-allocated scratch).
    for (int64_t i = 0; i < n_partials; i++) phase[i] = 0.0f;

    // ── Stage A: per-frame linear_proj + tanh ──
    for (int64_t f = 0; f < n_frames; f++) {
        const float* x_f = latent + (size_t)f * (size_t)latent_dim;
        float* y_f       = ctrl_scratch + (size_t)f * (size_t)ctrl_dim;
        for (int64_t i = 0; i < ctrl_dim; i++) {
            const float* Wi = W_ctrl + (size_t)i * (size_t)latent_dim;
            float acc = 0.0f;
            for (int64_t j = 0; j < latent_dim; j++) acc += Wi[j] * x_f[j];
            y_f[i] = tanhf(acc);
        }
    }

    // ── Stage B: additive synthesis ──
    const int64_t n_samples = n_frames * hop;
    hxvocoder_synth_additive(out_p, scratch_ctrl_p, scratch_phase_p,
                             n_frames, n_samples, n_partials, ctrl_dim,
                             hop, sample_rate, base_freq_hz, max_gain);
}

// ─────────────────────────────────────────────────────────────
// hxvocoder_decode_nv — full Stage A + Stage B, constants baked.
//
// Designed for the Hexa FFI 6-arg ceiling: only 4 args needed.
//   out_p        : float* [n_frames * NV_HOP] output PCM samples
//   latent_flat_p: float* [n_frames * NV_LATENT_DIM] row-major
//   W_ctrl_p     : float* [NV_CTRL_DIM * NV_LATENT_DIM] row-major
//   n_frames     : int64_t
//
// Baked constants match neural_vocoder.hexa Section 1:
//   SR=24000  HOP=240  LATENT=384  PARTIALS=6  CTRL=12
//   BASE_FREQ=200.0  MAX_GAIN=0.18
//
// All scratch is stack/heap inside this function — caller only
// allocates the three persistent buffers.
// ─────────────────────────────────────────────────────────────
#define NV_SR          24000.0
#define NV_HOP         240
#define NV_PARTIALS    6
#define NV_CTRL_DIM    12
#define NV_LATENT_DIM  384
#define NV_BASE_FREQ   200.0
#define NV_MAX_GAIN    0.18

void hxvocoder_decode_nv(int64_t out_p, int64_t latent_flat_p,
                         int64_t W_ctrl_p, int64_t n_frames) {
    if (out_p == 0 || latent_flat_p == 0 || W_ctrl_p == 0) return;
    if (n_frames <= 0) return;

    float* out          = (float*)(uintptr_t)out_p;
    const float* latent = (const float*)(uintptr_t)latent_flat_p;
    const float* W_ctrl = (const float*)(uintptr_t)W_ctrl_p;

    const int64_t n_samples = n_frames * NV_HOP;

    // ── scratch: ctrl [n_frames × CTRL_DIM] + phase [PARTIALS] ──
    float* ctrl = (float*)calloc((size_t)n_frames * NV_CTRL_DIM, sizeof(float));
    float phase[NV_PARTIALS];
    for (int i = 0; i < NV_PARTIALS; i++) phase[i] = 0.0f;

    // ── Stage A: per-frame linear_proj + tanh ──
    for (int64_t f = 0; f < n_frames; f++) {
        const float* x_f = latent + f * NV_LATENT_DIM;
        float* y_f       = ctrl   + f * NV_CTRL_DIM;
        for (int i = 0; i < NV_CTRL_DIM; i++) {
            const float* Wi = W_ctrl + (size_t)i * NV_LATENT_DIM;
            float acc = 0.0f;
            for (int j = 0; j < NV_LATENT_DIM; j++) acc += Wi[j] * x_f[j];
            y_f[i] = tanhf(acc);
        }
    }

    // ── Stage B: additive synthesis (inlined, same as synth_additive) ──
    const float TWO_PI  = 6.283185307179586f;
    const float LN2     = 0.6931471805599453f;
    const float inv_sr  = 1.0f / (float)NV_SR;
    const float inv_hop = 1.0f / (float)NV_HOP;

    for (int64_t t = 0; t < n_samples; t++) {
        const float frame_pos = (float)t * inv_hop;
        int64_t f0 = (int64_t)frame_pos;
        if (f0 >= n_frames) f0 = n_frames - 1;
        int64_t f1 = f0 + 1;
        if (f1 >= n_frames) f1 = n_frames - 1;
        const float alpha = frame_pos - (float)f0;
        const float oma   = 1.0f - alpha;

        const float* row0 = ctrl + f0 * NV_CTRL_DIM;
        const float* row1 = ctrl + f1 * NV_CTRL_DIM;

        float s = 0.0f;
        for (int p = 0; p < NV_PARTIALS; p++) {
            const float off  = row0[p*2]   * oma + row1[p*2]   * alpha;
            const float gain = row0[p*2+1] * oma + row1[p*2+1] * alpha;
            const float nominal = (float)NV_BASE_FREQ * (float)(p + 1);
            const float freq = nominal * expf(off * LN2);
            float ph = phase[p] + TWO_PI * freq * inv_sr;
            if (ph > TWO_PI) ph -= TWO_PI;
            if (ph < 0.0f)   ph += TWO_PI;
            phase[p] = ph;
            s += gain * (float)NV_MAX_GAIN * sinf(ph);
        }
        out[t] = s;
    }

    free(ctrl);
}
