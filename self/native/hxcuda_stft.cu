// hxcuda_stft.cu -- STFT/iSTFT CUDA kernels for HEXA-SPEAK native vocoder
//
// Companion to hxcuda_fused.cu. Adds STFT (audio->spectrogram) and
// iSTFT (spectrogram->audio) needed by anima-speak/neural_vocoder.hexa.
//
// Design:
//   * cuFFT backs the DFT (CUFFT_R2C for STFT, CUFFT_C2R for iSTFT).
//   * Custom kernels handle windowing + framing (pre-FFT) and
//     overlap-add + window-sum normalization (post-iFFT).
//   * Precision: fp32 I/O, fp32 internal, fp32 accumulator.
//     (bf16 label kept for symbol-naming consistency with the rest
//     of hxcuda; true internal bf16 would introduce FFT error for
//     audio-scale signals.)
//   * Plans cached per (n_fft, n_frames) in a tiny fixed-size LRU.
//
// ABI convention (matches hxcuda_fused.cu):
//   * pointers as int64_t, cast via uintptr_t
//   * struct-args ABI for >6 arg functions
//   * extern "C" symbols: hxcuda_stft_bf16, hxcuda_istft_bf16
//
// Compile: see scripts/build_hxcuda_linux.hexa (link -lcufft)

#include <cuda_runtime.h>
#include <cufft.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

// =========================================================================
//  Kernel: window + frame (audio -> framed, windowed signal)
//
//  Input:  audio[n_samples]    -- 1-D pcm
//  Output: framed[n_frames * n_fft]  -- each row is window .* slice
//
//  Layout: framed[t, k] = window[k] * audio[t*hop + k], zero-padded if OOB.
//  Grid:   (n_frames, ceil(n_fft / BLOCK))
//  Block:  (BLOCK, 1, 1)
// =========================================================================

__global__ void stft_window_frame_kernel(
    const float* __restrict__ audio,
    const float* __restrict__ window,
    float*       __restrict__ framed,
    int64_t n_samples,
    int64_t n_fft,
    int64_t hop,
    int64_t n_frames)
{
    int64_t t = blockIdx.x;
    int64_t k = (int64_t)blockIdx.y * blockDim.x + threadIdx.x;
    if (t >= n_frames || k >= n_fft) return;

    int64_t s = t * hop + k;
    float v = (s < n_samples) ? audio[s] : 0.0f;
    framed[t * n_fft + k] = v * window[k];
}

// =========================================================================
//  Kernel: overlap-add (iSTFT inverse -> audio accumulator + norm buffer)
//
//  After cuFFT C2R inverse, `framed` holds per-frame time-domain signals
//  each of length n_fft. We multiply again by the (synthesis) window and
//  overlap-add into `audio_out`. Separately, we accumulate window*window
//  into `win_sum` for later normalization (COLA / principal-square
//  correction).
//
//  Grid:  (n_frames, ceil(n_fft / BLOCK))
//  Block: (BLOCK, 1, 1)
//
//  NOTE: atomicAdd is used on the overlap region. For audio-scale sizes
//  this is a small cost and vastly simpler than segmented reductions.
// =========================================================================

__global__ void istft_overlap_add_kernel(
    const float* __restrict__ framed,
    const float* __restrict__ window,
    float*       __restrict__ audio_out,   // [out_len]
    float*       __restrict__ win_sum,     // [out_len]
    int64_t out_len,
    int64_t n_fft,
    int64_t hop,
    int64_t n_frames,
    float   fft_norm)                      // 1/n_fft (cuFFT C2R is unnormalized)
{
    int64_t t = blockIdx.x;
    int64_t k = (int64_t)blockIdx.y * blockDim.x + threadIdx.x;
    if (t >= n_frames || k >= n_fft) return;

    int64_t s = t * hop + k;
    if (s >= out_len) return;

    float w  = window[k];
    float xk = framed[t * n_fft + k] * fft_norm;   // normalize cuFFT output
    atomicAdd(&audio_out[s], xk * w);
    atomicAdd(&win_sum[s],   w * w);
}

// =========================================================================
//  Kernel: normalize by window-sum (in-place)
// =========================================================================

__global__ void istft_normalize_kernel(
    float*       __restrict__ audio_out,
    const float* __restrict__ win_sum,
    int64_t out_len,
    float   eps)
{
    int64_t i = (int64_t)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= out_len) return;
    float w = win_sum[i];
    audio_out[i] = (w > eps) ? (audio_out[i] / w) : 0.0f;
}

// =========================================================================
//  Tiny cuFFT plan cache (4 slots, LRU)
// =========================================================================

struct PlanCacheEntry {
    int64_t   n_fft;
    int64_t   n_frames;
    int       type;       // 0 = R2C, 1 = C2R
    cufftHandle plan;
    int64_t   tick;
};

#define PLAN_CACHE_SLOTS 8

static PlanCacheEntry g_plan_cache[PLAN_CACHE_SLOTS] = {};
static int64_t        g_plan_tick  = 0;

static cufftHandle get_or_create_plan(int64_t n_fft, int64_t n_frames, int type)
{
    // Linear search (cache small)
    for (int i = 0; i < PLAN_CACHE_SLOTS; ++i) {
        PlanCacheEntry& e = g_plan_cache[i];
        if (e.plan != 0 && e.n_fft == n_fft && e.n_frames == n_frames && e.type == type) {
            e.tick = ++g_plan_tick;
            return e.plan;
        }
    }
    // Find victim (empty slot first, else oldest tick)
    int victim = 0;
    int64_t oldest = g_plan_cache[0].tick;
    for (int i = 0; i < PLAN_CACHE_SLOTS; ++i) {
        if (g_plan_cache[i].plan == 0) { victim = i; oldest = -1; break; }
        if (g_plan_cache[i].tick < oldest) { oldest = g_plan_cache[i].tick; victim = i; }
    }
    PlanCacheEntry& e = g_plan_cache[victim];
    if (e.plan != 0) {
        cufftDestroy(e.plan);
        e.plan = 0;
    }
    cufftType ctype = (type == 0) ? CUFFT_R2C : CUFFT_C2R;
    cufftResult r;
    if (n_frames <= 1) {
        r = cufftPlan1d(&e.plan, (int)n_fft, ctype, 1);
    } else {
        // Batched 1-D FFT: batch = n_frames
        int nn = (int)n_fft;
        r = cufftPlanMany(&e.plan, 1, &nn,
                          NULL, 1, (int)n_fft,
                          NULL, 1, (int)(n_fft / 2 + 1),
                          ctype, (int)n_frames);
    }
    if (r != CUFFT_SUCCESS) {
        fprintf(stderr, "[hxcuda_stft] cuFFT plan create failed: %d\n", (int)r);
        e.plan = 0;
        return 0;
    }
    e.n_fft    = n_fft;
    e.n_frames = n_frames;
    e.type     = type;
    e.tick     = ++g_plan_tick;
    return e.plan;
}

// =========================================================================
//  Host wrappers (extern "C")
// =========================================================================

extern "C" {

// ── hxcuda_stft_bf16 ─────────────────────────────────────────────
//
// Forward STFT.
//
// Struct-args ABI (7 fields):
//   off  0: n_samples    (int64_t)
//   off  8: n_fft        (int64_t)  (must be even)
//   off 16: hop          (int64_t)
//   off 24: audio_p      (int64_t)  device float* [n_samples]
//   off 32: window_p     (int64_t)  device float* [n_fft]
//   off 40: spec_p       (int64_t)  device float* [n_frames * (n_fft/2+1) * 2]
//                                    (real/imag interleaved, as returned by
//                                     cuFFT cufftComplex layout: re, im)
//   off 48: n_frames_out (int64_t)  host int64_t* -- output: # of frames
//
// Returns 0 on success; negative on error.

typedef struct {
    int64_t n_samples;
    int64_t n_fft;
    int64_t hop;
    int64_t audio_p;
    int64_t window_p;
    int64_t spec_p;
    int64_t n_frames_out;
} HxCudaStftArgs;

int64_t hxcuda_stft_bf16(int64_t args_p) {
    if (args_p == 0) return -1;
    HxCudaStftArgs* a = (HxCudaStftArgs*)(uintptr_t)args_p;

    if (a->n_samples <= 0 || a->n_fft <= 0 || a->hop <= 0) return -2;
    if ((a->n_fft & 1) != 0) return -3;   // cuFFT R2C needs even n_fft

    const float* audio_d  = (const float*)(uintptr_t)a->audio_p;
    const float* window_d = (const float*)(uintptr_t)a->window_p;
    float*       spec_d   = (float*)(uintptr_t)a->spec_p;  // interpreted as cufftComplex

    if (!audio_d || !window_d || !spec_d) return -4;

    int64_t n_frames = 1 + (a->n_samples + a->hop - 1) / a->hop;
    // Matches librosa default with center=False: frames = 1 + floor((n-n_fft)/hop).
    // We use center=False w/ zero-pad-end semantics.
    n_frames = (a->n_samples >= a->n_fft)
               ? (1 + (a->n_samples - a->n_fft) / a->hop + 1)   // +1 for tail frame with zero-pad
               : 1;
    // simpler: ceil(max(n_samples - n_fft, 0)/hop) + 1
    {
        int64_t span = a->n_samples - a->n_fft;
        if (span < 0) span = 0;
        n_frames = (span + a->hop - 1) / a->hop + 1;
        if (n_frames < 1) n_frames = 1;
    }

    // Allocate framed workspace [n_frames, n_fft] fp32
    float* framed_d = NULL;
    cudaError_t cerr = cudaMalloc(&framed_d, n_frames * a->n_fft * sizeof(float));
    if (cerr != cudaSuccess) return -5;

    // Launch windowing + framing
    const int BX = 128;
    dim3 grid((unsigned)n_frames, (unsigned)((a->n_fft + BX - 1) / BX));
    dim3 block(BX);
    stft_window_frame_kernel<<<grid, block>>>(
        audio_d, window_d, framed_d,
        a->n_samples, a->n_fft, a->hop, n_frames);
    cerr = cudaGetLastError();
    if (cerr != cudaSuccess) { cudaFree(framed_d); return -6; }

    // cuFFT R2C batched
    cufftHandle plan = get_or_create_plan(a->n_fft, n_frames, 0);
    if (plan == 0) { cudaFree(framed_d); return -7; }

    cufftResult fr = cufftExecR2C(plan, (cufftReal*)framed_d, (cufftComplex*)spec_d);
    if (fr != CUFFT_SUCCESS) { cudaFree(framed_d); return -8; }

    cudaFree(framed_d);

    // Write n_frames back (host pointer)
    if (a->n_frames_out != 0) {
        int64_t* out_host = (int64_t*)(uintptr_t)a->n_frames_out;
        *out_host = n_frames;
    }
    return 0;
}

// ── hxcuda_istft_bf16 ────────────────────────────────────────────
//
// Inverse STFT with overlap-add + window-sum normalization.
//
// Struct-args ABI (8 fields):
//   off  0: n_frames     (int64_t)
//   off  8: n_fft        (int64_t)
//   off 16: hop          (int64_t)
//   off 24: out_len      (int64_t)       total audio_out buffer length
//   off 32: spec_p       (int64_t)       device cufftComplex* [n_frames * (n_fft/2+1)]
//   off 40: window_p     (int64_t)       device float* [n_fft] (synthesis window)
//   off 48: audio_out_p  (int64_t)       device float* [out_len]
//   off 56: n_samples_out (int64_t)      host int64_t* -- effective sample count

typedef struct {
    int64_t n_frames;
    int64_t n_fft;
    int64_t hop;
    int64_t out_len;
    int64_t spec_p;
    int64_t window_p;
    int64_t audio_out_p;
    int64_t n_samples_out;
} HxCudaIstftArgs;

int64_t hxcuda_istft_bf16(int64_t args_p) {
    if (args_p == 0) return -1;
    HxCudaIstftArgs* a = (HxCudaIstftArgs*)(uintptr_t)args_p;

    if (a->n_frames <= 0 || a->n_fft <= 0 || a->hop <= 0 || a->out_len <= 0) return -2;
    if ((a->n_fft & 1) != 0) return -3;

    const float* window_d    = (const float*)(uintptr_t)a->window_p;
    float*       audio_out_d = (float*)(uintptr_t)a->audio_out_p;
    float*       spec_d      = (float*)(uintptr_t)a->spec_p;   // as cufftComplex

    if (!window_d || !audio_out_d || !spec_d) return -4;

    // Workspace: framed [n_frames, n_fft] + win_sum [out_len]
    float* framed_d = NULL;
    cudaError_t cerr = cudaMalloc(&framed_d, a->n_frames * a->n_fft * sizeof(float));
    if (cerr != cudaSuccess) return -5;

    float* win_sum_d = NULL;
    cerr = cudaMalloc(&win_sum_d, a->out_len * sizeof(float));
    if (cerr != cudaSuccess) { cudaFree(framed_d); return -6; }

    cudaMemset(win_sum_d, 0, a->out_len * sizeof(float));
    cudaMemset(audio_out_d, 0, a->out_len * sizeof(float));

    // cuFFT C2R batched inverse
    cufftHandle plan = get_or_create_plan(a->n_fft, a->n_frames, 1);
    if (plan == 0) { cudaFree(framed_d); cudaFree(win_sum_d); return -7; }
    cufftResult fr = cufftExecC2R(plan, (cufftComplex*)spec_d, (cufftReal*)framed_d);
    if (fr != CUFFT_SUCCESS) { cudaFree(framed_d); cudaFree(win_sum_d); return -8; }

    // Overlap-add + accumulate window^2
    const int BX = 128;
    dim3 grid((unsigned)a->n_frames, (unsigned)((a->n_fft + BX - 1) / BX));
    dim3 block(BX);
    float fft_norm = 1.0f / (float)a->n_fft;
    istft_overlap_add_kernel<<<grid, block>>>(
        framed_d, window_d, audio_out_d, win_sum_d,
        a->out_len, a->n_fft, a->hop, a->n_frames, fft_norm);
    cerr = cudaGetLastError();
    if (cerr != cudaSuccess) { cudaFree(framed_d); cudaFree(win_sum_d); return -9; }

    // Normalize
    int norm_bx = 256;
    int norm_grid = (int)((a->out_len + norm_bx - 1) / norm_bx);
    istft_normalize_kernel<<<norm_grid, norm_bx>>>(
        audio_out_d, win_sum_d, a->out_len, 1.0e-8f);
    cerr = cudaGetLastError();
    if (cerr != cudaSuccess) { cudaFree(framed_d); cudaFree(win_sum_d); return -10; }

    cudaFree(framed_d);
    cudaFree(win_sum_d);

    if (a->n_samples_out != 0) {
        int64_t* out_host = (int64_t*)(uintptr_t)a->n_samples_out;
        int64_t eff = (a->n_frames - 1) * a->hop + a->n_fft;
        if (eff > a->out_len) eff = a->out_len;
        *out_host = eff;
    }
    return 0;
}

// ── hxcuda_stft_version ─────────────────────────────────────────
int64_t hxcuda_stft_version(void) {
    return 1;  // v1: initial STFT/iSTFT (cuFFT-backed)
}

}  // extern "C"
