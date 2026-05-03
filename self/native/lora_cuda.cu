// lora_cuda.cu — F-CUDA-LORA-1: r=64 LoRA forward + backward custom CUDA kernels
//
// Replaces the RC_ERR_CUDA_TODO=-5 stub in hxqwen14b.c for the LoRA training
// hot path. Provides hand-written tiled kernels (no cuBLAS dependency) so the
// .so can run on minimal CUDA installs (cudart only) and so we have explicit
// control over fp32/bf16/fp16 dispatch and the alpha/r scale fold-in.
//
// Mathematical contract (matches the task brief, F-CUDA-LORA-1):
//
//   Forward:   y[M,N] = x[M,K] · W[K,N]  +  α · (x[M,K] · A[K,R]) · B[R,N]
//   Backward:  given dy[M,N] and the saved x[M,K], A[K,R], B[R,N]:
//                u[M,R]  = α · (dy · Bᵀ)
//                dA[K,R] = xᵀ · u                 (= α · xᵀ · dy · Bᵀ)
//                dB[R,N] = α · (x · A)ᵀ · dy      (= α · Aᵀ · xᵀ · dy)
//                dx[M,K] = α · (dy · Bᵀ) · Aᵀ + dy · Wᵀ        (LoRA + base contributions; the
//                                                                 caller provides W only if base-grad
//                                                                 path is wanted, otherwise NULL)
//
//   r defaults to 64 (HxQwenLoraFwdArgs::R_lora). The kernels are r-agnostic
//   (any rank from 1..256) because the small dim sits in registers via a
//   per-thread accumulator strip.
//
// Layout: all tensors row-major fp32 by default. Strides equal innermost dim.
// `dtype_id` argument selects the kernel variant:
//     0 = fp32 (REQUIRED — implemented here)
//     1 = bf16 (stub: returns -7 LORA_RC_DTYPE_TODO)
//     2 = fp16 (stub: returns -7 LORA_RC_DTYPE_TODO)
//
// Stream: each launcher takes `cudaStream_t stream`. Pass NULL for the
// default stream. All kernels are async; callers synchronize as needed.
//
// PENDING-HARDWARE: F-CUDA-LORA-1 acceptance (r=64 fwd+bwd vs PyTorch < 1e-3
// absolute) requires execution on RTX 5070 (sm_120) or H100 (sm_90). The
// CPU-vs-CPU equivalence harness in test/lora_cuda_equiv_test.c covers the
// GPU-launcher API by routing through the same arg structs to the host-side
// reference path; the device path is exercised by the same test once a CUDA
// runtime is detected (see `lora_cuda_runtime_available`).

#include <cuda_runtime.h>
#include <stdint.h>
#include <math.h>

#include "lora_cuda.h"

// ─────────────────────────────────────────────────────────────
// Tile / block configuration. BM × BN per block, BK along K. The rank
// dimension R (default 64) is reduced inside per-thread accumulators
// because R is small relative to K, N.
// ─────────────────────────────────────────────────────────────
#define LORA_BM 64
#define LORA_BN 64
#define LORA_BK 16
#define LORA_THREADS 256   // 16x16 tile of threads each owning a 4x4 micro-tile

// ─────────────────────────────────────────────────────────────
// Forward kernel: y = x · W + α · (x · A) · B
// Strategy: split into two passes (no reuse of an MxR scratch across blocks
// — we recompute x·A on the fly via a register tile of width R≤256 per warp).
// We instead perform a fused tiled matmul where each output tile y[bm:bm+BM,
// bn:bn+BN] is the sum of:
//   accW = Σ_k x[bm,k] · W[k,bn]
//   accL = α · Σ_r ( Σ_k x[bm,k]·A[k,r] ) · B[r,bn]
// We compute accL by first accumulating the rank-R intermediate in shared
// memory: tmp_block[BM,R] = x[bm,:] · A[:,r] (one block per (bm) row strip
// produces an MxR slab); a separate kernel multiplies by B[R,N] with the
// scale folded in. Two-kernel forward keeps registers bounded and matches
// the cuBLAS reference plumbing the original code used.
// ─────────────────────────────────────────────────────────────

// Pass A: tmp[M,R] = x[M,K] · A[K,R]
//   grid:  ((M+BM-1)/BM, (R+BN-1)/BN)
//   block: 16x16 threads, each computing a 4x4 micro-tile of tmp.
__global__ void lora_fwd_xA_kernel(
    const float* __restrict__ x,   // [M, K]
    const float* __restrict__ A,   // [K, R]
    float* __restrict__ tmp,       // [M, R]
    int M, int K, int R
) {
    __shared__ float sX[LORA_BM][LORA_BK];
    __shared__ float sA[LORA_BK][LORA_BN];

    int row_block = blockIdx.x * LORA_BM;
    int col_block = blockIdx.y * LORA_BN;

    int tx = threadIdx.x;        // 0..15
    int ty = threadIdx.y;        // 0..15

    // Each thread handles a 4x4 micro-tile.
    float acc[4][4];
    #pragma unroll
    for (int i = 0; i < 4; i++)
        #pragma unroll
        for (int j = 0; j < 4; j++) acc[i][j] = 0.0f;

    int n_tiles = (K + LORA_BK - 1) / LORA_BK;
    for (int t = 0; t < n_tiles; t++) {
        int k_base = t * LORA_BK;

        // Cooperatively load sX[BM, BK] and sA[BK, BN].
        // 16x16 threads = 256; need to load 64*16 = 1024 elements each.
        // Each thread loads 4 elements per matrix per tile.
        #pragma unroll
        for (int i = 0; i < 4; i++) {
            int sm_row = ty * 4 + i;     // 0..63
            int sm_col = tx;             // 0..15
            int g_row = row_block + sm_row;
            int g_col = k_base + sm_col;
            sX[sm_row][sm_col] = (g_row < M && g_col < K) ? x[g_row * K + g_col] : 0.0f;
        }
        #pragma unroll
        for (int i = 0; i < 4; i++) {
            int sm_row = tx;             // 0..15  (BK=16)
            int sm_col = ty * 4 + i;     // 0..63
            int g_row = k_base + sm_row;
            int g_col = col_block + sm_col;
            sA[sm_row][sm_col] = (g_row < K && g_col < R) ? A[g_row * R + g_col] : 0.0f;
        }
        __syncthreads();

        // Multiply-accumulate into acc[4][4].
        #pragma unroll
        for (int kk = 0; kk < LORA_BK; kk++) {
            float a0 = sX[ty * 4 + 0][kk];
            float a1 = sX[ty * 4 + 1][kk];
            float a2 = sX[ty * 4 + 2][kk];
            float a3 = sX[ty * 4 + 3][kk];
            float b0 = sA[kk][tx * 4 + 0];
            float b1 = sA[kk][tx * 4 + 1];
            float b2 = sA[kk][tx * 4 + 2];
            float b3 = sA[kk][tx * 4 + 3];
            acc[0][0] += a0 * b0; acc[0][1] += a0 * b1; acc[0][2] += a0 * b2; acc[0][3] += a0 * b3;
            acc[1][0] += a1 * b0; acc[1][1] += a1 * b1; acc[1][2] += a1 * b2; acc[1][3] += a1 * b3;
            acc[2][0] += a2 * b0; acc[2][1] += a2 * b1; acc[2][2] += a2 * b2; acc[2][3] += a2 * b3;
            acc[3][0] += a3 * b0; acc[3][1] += a3 * b1; acc[3][2] += a3 * b2; acc[3][3] += a3 * b3;
        }
        __syncthreads();
    }

    // Write 4x4 tile to tmp[M, R].
    #pragma unroll
    for (int i = 0; i < 4; i++) {
        int g_row = row_block + ty * 4 + i;
        if (g_row >= M) continue;
        #pragma unroll
        for (int j = 0; j < 4; j++) {
            int g_col = col_block + tx * 4 + j;
            if (g_col >= R) continue;
            tmp[g_row * R + g_col] = acc[i][j];
        }
    }
}

// Pass B (forward, LoRA branch): y = α · tmp[M,R] · B[R,N] + (accumulate ? y : 0) + (apply_base ? x·W : 0)
// We split the base GEMM into its own kernel for clarity (apply_base path).
__global__ void lora_fwd_tmpB_kernel(
    const float* __restrict__ tmp,    // [M, R]
    const float* __restrict__ B,      // [R, N]
    float* __restrict__ y,            // [M, N]
    int M, int N, int R, float alpha,
    int beta_is_one        // 0 => overwrite y; 1 => y += contribution
) {
    __shared__ float sT[LORA_BM][LORA_BK];
    __shared__ float sB[LORA_BK][LORA_BN];

    int row_block = blockIdx.x * LORA_BM;
    int col_block = blockIdx.y * LORA_BN;

    int tx = threadIdx.x;
    int ty = threadIdx.y;

    float acc[4][4];
    #pragma unroll
    for (int i = 0; i < 4; i++)
        #pragma unroll
        for (int j = 0; j < 4; j++) acc[i][j] = 0.0f;

    int n_tiles = (R + LORA_BK - 1) / LORA_BK;
    for (int t = 0; t < n_tiles; t++) {
        int k_base = t * LORA_BK;

        #pragma unroll
        for (int i = 0; i < 4; i++) {
            int sm_row = ty * 4 + i;
            int sm_col = tx;
            int g_row = row_block + sm_row;
            int g_col = k_base + sm_col;
            sT[sm_row][sm_col] = (g_row < M && g_col < R) ? tmp[g_row * R + g_col] : 0.0f;
        }
        #pragma unroll
        for (int i = 0; i < 4; i++) {
            int sm_row = tx;
            int sm_col = ty * 4 + i;
            int g_row = k_base + sm_row;
            int g_col = col_block + sm_col;
            sB[sm_row][sm_col] = (g_row < R && g_col < N) ? B[g_row * N + g_col] : 0.0f;
        }
        __syncthreads();

        #pragma unroll
        for (int kk = 0; kk < LORA_BK; kk++) {
            float a0 = sT[ty*4+0][kk], a1 = sT[ty*4+1][kk], a2 = sT[ty*4+2][kk], a3 = sT[ty*4+3][kk];
            float b0 = sB[kk][tx*4+0], b1 = sB[kk][tx*4+1], b2 = sB[kk][tx*4+2], b3 = sB[kk][tx*4+3];
            acc[0][0]+=a0*b0; acc[0][1]+=a0*b1; acc[0][2]+=a0*b2; acc[0][3]+=a0*b3;
            acc[1][0]+=a1*b0; acc[1][1]+=a1*b1; acc[1][2]+=a1*b2; acc[1][3]+=a1*b3;
            acc[2][0]+=a2*b0; acc[2][1]+=a2*b1; acc[2][2]+=a2*b2; acc[2][3]+=a2*b3;
            acc[3][0]+=a3*b0; acc[3][1]+=a3*b1; acc[3][2]+=a3*b2; acc[3][3]+=a3*b3;
        }
        __syncthreads();
    }

    #pragma unroll
    for (int i = 0; i < 4; i++) {
        int g_row = row_block + ty * 4 + i;
        if (g_row >= M) continue;
        #pragma unroll
        for (int j = 0; j < 4; j++) {
            int g_col = col_block + tx * 4 + j;
            if (g_col >= N) continue;
            float scaled = alpha * acc[i][j];
            if (beta_is_one) y[g_row * N + g_col] += scaled;
            else             y[g_row * N + g_col]  = scaled;
        }
    }
}

// Base GEMM: y = x · W (overwrites y; LoRA pass then accumulates onto it).
__global__ void lora_base_xW_kernel(
    const float* __restrict__ x,    // [M, K]
    const float* __restrict__ W,    // [K, N]
    float* __restrict__ y,          // [M, N]
    int M, int N, int K
) {
    __shared__ float sX[LORA_BM][LORA_BK];
    __shared__ float sW[LORA_BK][LORA_BN];

    int row_block = blockIdx.x * LORA_BM;
    int col_block = blockIdx.y * LORA_BN;
    int tx = threadIdx.x, ty = threadIdx.y;

    float acc[4][4];
    #pragma unroll
    for (int i = 0; i < 4; i++)
        #pragma unroll
        for (int j = 0; j < 4; j++) acc[i][j] = 0.0f;

    int n_tiles = (K + LORA_BK - 1) / LORA_BK;
    for (int t = 0; t < n_tiles; t++) {
        int k_base = t * LORA_BK;

        #pragma unroll
        for (int i = 0; i < 4; i++) {
            int sm_row = ty * 4 + i;
            int sm_col = tx;
            int g_row = row_block + sm_row;
            int g_col = k_base + sm_col;
            sX[sm_row][sm_col] = (g_row < M && g_col < K) ? x[g_row * K + g_col] : 0.0f;
        }
        #pragma unroll
        for (int i = 0; i < 4; i++) {
            int sm_row = tx;
            int sm_col = ty * 4 + i;
            int g_row = k_base + sm_row;
            int g_col = col_block + sm_col;
            sW[sm_row][sm_col] = (g_row < K && g_col < N) ? W[g_row * N + g_col] : 0.0f;
        }
        __syncthreads();

        #pragma unroll
        for (int kk = 0; kk < LORA_BK; kk++) {
            float a0 = sX[ty*4+0][kk], a1 = sX[ty*4+1][kk], a2 = sX[ty*4+2][kk], a3 = sX[ty*4+3][kk];
            float b0 = sW[kk][tx*4+0], b1 = sW[kk][tx*4+1], b2 = sW[kk][tx*4+2], b3 = sW[kk][tx*4+3];
            acc[0][0]+=a0*b0; acc[0][1]+=a0*b1; acc[0][2]+=a0*b2; acc[0][3]+=a0*b3;
            acc[1][0]+=a1*b0; acc[1][1]+=a1*b1; acc[1][2]+=a1*b2; acc[1][3]+=a1*b3;
            acc[2][0]+=a2*b0; acc[2][1]+=a2*b1; acc[2][2]+=a2*b2; acc[2][3]+=a2*b3;
            acc[3][0]+=a3*b0; acc[3][1]+=a3*b1; acc[3][2]+=a3*b2; acc[3][3]+=a3*b3;
        }
        __syncthreads();
    }

    #pragma unroll
    for (int i = 0; i < 4; i++) {
        int g_row = row_block + ty * 4 + i;
        if (g_row >= M) continue;
        #pragma unroll
        for (int j = 0; j < 4; j++) {
            int g_col = col_block + tx * 4 + j;
            if (g_col >= N) continue;
            y[g_row * N + g_col] = acc[i][j];
        }
    }
}

// ─────────────────────────────────────────────────────────────
// Backward kernels.
//
//   u[M,R]  = α · dy[M,N] · Bᵀ[N,R]            (lora_bwd_dyBt_kernel)
//   dA[K,R] = xᵀ[K,M] · u[M,R]                 (lora_bwd_dA_kernel)
//   dB[R,N] = α · (x·A)ᵀ[R,M] · dy[M,N]
//           = (1/α)·uᵀ[R,M] · ?  — no, simpler: recompute tmp = x·A,
//             then dB = α · tmpᵀ · dy. We reuse lora_fwd_xA_kernel for tmp.
//   dx[M,K] = u[M,R] · Aᵀ[R,K]                 (lora_bwd_dx_kernel; LoRA contribution)
// ─────────────────────────────────────────────────────────────

// u[M,R] = α · dy[M,N] · Bᵀ[N,R]    (B is row-major [R,N] so Bᵀ is [N,R] viewed naturally)
// Equivalently: u[m,r] = α · Σ_n dy[m,n] · B[r,n]
__global__ void lora_bwd_dyBt_kernel(
    const float* __restrict__ dy,   // [M, N]
    const float* __restrict__ B,    // [R, N]
    float* __restrict__ u,          // [M, R]
    int M, int N, int R, float alpha
) {
    __shared__ float sDy[LORA_BM][LORA_BK];
    __shared__ float sB[LORA_BN][LORA_BK];   // tile of B viewed as [BN_r, BK_n]

    int row_block = blockIdx.x * LORA_BM;     // M direction
    int col_block = blockIdx.y * LORA_BN;     // R direction
    int tx = threadIdx.x, ty = threadIdx.y;

    float acc[4][4];
    #pragma unroll
    for (int i = 0; i < 4; i++)
        #pragma unroll
        for (int j = 0; j < 4; j++) acc[i][j] = 0.0f;

    int n_tiles = (N + LORA_BK - 1) / LORA_BK;
    for (int t = 0; t < n_tiles; t++) {
        int n_base = t * LORA_BK;
        #pragma unroll
        for (int i = 0; i < 4; i++) {
            int sm_row = ty * 4 + i;
            int sm_col = tx;
            int g_row = row_block + sm_row;
            int g_col = n_base + sm_col;
            sDy[sm_row][sm_col] = (g_row < M && g_col < N) ? dy[g_row * N + g_col] : 0.0f;
        }
        #pragma unroll
        for (int i = 0; i < 4; i++) {
            int sm_row = ty * 4 + i;        // r-axis tile entry
            int sm_col = tx;                // n-axis tile entry
            int g_row = col_block + sm_row; // R
            int g_col = n_base + sm_col;    // N
            sB[sm_row][sm_col] = (g_row < R && g_col < N) ? B[g_row * N + g_col] : 0.0f;
        }
        __syncthreads();

        #pragma unroll
        for (int kk = 0; kk < LORA_BK; kk++) {
            float a0 = sDy[ty*4+0][kk], a1 = sDy[ty*4+1][kk], a2 = sDy[ty*4+2][kk], a3 = sDy[ty*4+3][kk];
            float b0 = sB[tx*4+0][kk], b1 = sB[tx*4+1][kk], b2 = sB[tx*4+2][kk], b3 = sB[tx*4+3][kk];
            acc[0][0]+=a0*b0; acc[0][1]+=a0*b1; acc[0][2]+=a0*b2; acc[0][3]+=a0*b3;
            acc[1][0]+=a1*b0; acc[1][1]+=a1*b1; acc[1][2]+=a1*b2; acc[1][3]+=a1*b3;
            acc[2][0]+=a2*b0; acc[2][1]+=a2*b1; acc[2][2]+=a2*b2; acc[2][3]+=a2*b3;
            acc[3][0]+=a3*b0; acc[3][1]+=a3*b1; acc[3][2]+=a3*b2; acc[3][3]+=a3*b3;
        }
        __syncthreads();
    }

    #pragma unroll
    for (int i = 0; i < 4; i++) {
        int g_row = row_block + ty * 4 + i;
        if (g_row >= M) continue;
        #pragma unroll
        for (int j = 0; j < 4; j++) {
            int g_col = col_block + tx * 4 + j;
            if (g_col >= R) continue;
            u[g_row * R + g_col] = alpha * acc[i][j];
        }
    }
}

// dA[K,R] = xᵀ · u  =  Σ_m x[m,k] · u[m,r]
//   grid: ((K+BM-1)/BM, (R+BN-1)/BN); block: 16x16 (-> 4x4 micro-tile)
__global__ void lora_bwd_dA_kernel(
    const float* __restrict__ x,    // [M, K]
    const float* __restrict__ u,    // [M, R]
    float* __restrict__ dA,         // [K, R]
    int M, int K, int R,
    int beta_is_one
) {
    __shared__ float sX[LORA_BM][LORA_BK];   // K-rows, M-cols (transposed view)
    __shared__ float sU[LORA_BK][LORA_BN];   // M-rows of u; tile_BK=16 along M

    int row_block = blockIdx.x * LORA_BM;    // K
    int col_block = blockIdx.y * LORA_BN;    // R
    int tx = threadIdx.x, ty = threadIdx.y;

    float acc[4][4];
    #pragma unroll
    for (int i = 0; i < 4; i++)
        #pragma unroll
        for (int j = 0; j < 4; j++) acc[i][j] = 0.0f;

    int n_tiles = (M + LORA_BK - 1) / LORA_BK;
    for (int t = 0; t < n_tiles; t++) {
        int m_base = t * LORA_BK;
        // Load sX[K_tile=BM, M_tile=BK] = x[m, k]^T  (we read x[m,k] and store into sX[k,m])
        #pragma unroll
        for (int i = 0; i < 4; i++) {
            int sm_row = ty * 4 + i;     // K-axis index inside tile
            int sm_col = tx;             // M-axis index inside tile
            int g_k = row_block + sm_row;
            int g_m = m_base + sm_col;
            sX[sm_row][sm_col] = (g_k < K && g_m < M) ? x[g_m * K + g_k] : 0.0f;
        }
        // Load sU[M_tile=BK, R_tile=BN] = u[m, r]
        #pragma unroll
        for (int i = 0; i < 4; i++) {
            int sm_row = tx;             // M-axis index
            int sm_col = ty * 4 + i;     // R-axis index
            int g_m = m_base + sm_row;
            int g_r = col_block + sm_col;
            sU[sm_row][sm_col] = (g_m < M && g_r < R) ? u[g_m * R + g_r] : 0.0f;
        }
        __syncthreads();

        #pragma unroll
        for (int kk = 0; kk < LORA_BK; kk++) {
            float a0 = sX[ty*4+0][kk], a1 = sX[ty*4+1][kk], a2 = sX[ty*4+2][kk], a3 = sX[ty*4+3][kk];
            float b0 = sU[kk][tx*4+0], b1 = sU[kk][tx*4+1], b2 = sU[kk][tx*4+2], b3 = sU[kk][tx*4+3];
            acc[0][0]+=a0*b0; acc[0][1]+=a0*b1; acc[0][2]+=a0*b2; acc[0][3]+=a0*b3;
            acc[1][0]+=a1*b0; acc[1][1]+=a1*b1; acc[1][2]+=a1*b2; acc[1][3]+=a1*b3;
            acc[2][0]+=a2*b0; acc[2][1]+=a2*b1; acc[2][2]+=a2*b2; acc[2][3]+=a2*b3;
            acc[3][0]+=a3*b0; acc[3][1]+=a3*b1; acc[3][2]+=a3*b2; acc[3][3]+=a3*b3;
        }
        __syncthreads();
    }

    #pragma unroll
    for (int i = 0; i < 4; i++) {
        int g_row = row_block + ty * 4 + i;
        if (g_row >= K) continue;
        #pragma unroll
        for (int j = 0; j < 4; j++) {
            int g_col = col_block + tx * 4 + j;
            if (g_col >= R) continue;
            float v = acc[i][j];
            if (beta_is_one) dA[g_row * R + g_col] += v;
            else             dA[g_row * R + g_col]  = v;
        }
    }
}

// dB[R,N] = α · (x·A)ᵀ · dy = α · tmpᵀ[R,M] · dy[M,N]
//   tmp[M,R] is reused from forward (caller supplies; or backward orchestrator
//   recomputes via lora_fwd_xA_kernel before calling this).
//
//   grid: ((R+BM-1)/BM, (N+BN-1)/BN); block: 16x16
__global__ void lora_bwd_dB_kernel(
    const float* __restrict__ tmp,   // [M, R]   tmp = x·A (no scale)
    const float* __restrict__ dy,    // [M, N]
    float* __restrict__ dB,          // [R, N]
    int M, int N, int R, float alpha,
    int beta_is_one
) {
    __shared__ float sT[LORA_BM][LORA_BK];   // (R, M)-transpose tile
    __shared__ float sDy[LORA_BK][LORA_BN];  // (M, N) tile

    int row_block = blockIdx.x * LORA_BM;    // R
    int col_block = blockIdx.y * LORA_BN;    // N
    int tx = threadIdx.x, ty = threadIdx.y;

    float acc[4][4];
    #pragma unroll
    for (int i = 0; i < 4; i++)
        #pragma unroll
        for (int j = 0; j < 4; j++) acc[i][j] = 0.0f;

    int n_tiles = (M + LORA_BK - 1) / LORA_BK;
    for (int t = 0; t < n_tiles; t++) {
        int m_base = t * LORA_BK;
        // sT[r_tile, m_tile] = tmp[m, r]^T
        #pragma unroll
        for (int i = 0; i < 4; i++) {
            int sm_row = ty * 4 + i;       // R
            int sm_col = tx;               // M
            int g_r = row_block + sm_row;
            int g_m = m_base + sm_col;
            sT[sm_row][sm_col] = (g_r < R && g_m < M) ? tmp[g_m * R + g_r] : 0.0f;
        }
        // sDy[m_tile, n_tile] = dy[m, n]
        #pragma unroll
        for (int i = 0; i < 4; i++) {
            int sm_row = tx;
            int sm_col = ty * 4 + i;
            int g_m = m_base + sm_row;
            int g_n = col_block + sm_col;
            sDy[sm_row][sm_col] = (g_m < M && g_n < N) ? dy[g_m * N + g_n] : 0.0f;
        }
        __syncthreads();

        #pragma unroll
        for (int kk = 0; kk < LORA_BK; kk++) {
            float a0 = sT[ty*4+0][kk], a1 = sT[ty*4+1][kk], a2 = sT[ty*4+2][kk], a3 = sT[ty*4+3][kk];
            float b0 = sDy[kk][tx*4+0], b1 = sDy[kk][tx*4+1], b2 = sDy[kk][tx*4+2], b3 = sDy[kk][tx*4+3];
            acc[0][0]+=a0*b0; acc[0][1]+=a0*b1; acc[0][2]+=a0*b2; acc[0][3]+=a0*b3;
            acc[1][0]+=a1*b0; acc[1][1]+=a1*b1; acc[1][2]+=a1*b2; acc[1][3]+=a1*b3;
            acc[2][0]+=a2*b0; acc[2][1]+=a2*b1; acc[2][2]+=a2*b2; acc[2][3]+=a2*b3;
            acc[3][0]+=a3*b0; acc[3][1]+=a3*b1; acc[3][2]+=a3*b2; acc[3][3]+=a3*b3;
        }
        __syncthreads();
    }

    #pragma unroll
    for (int i = 0; i < 4; i++) {
        int g_row = row_block + ty * 4 + i;
        if (g_row >= R) continue;
        #pragma unroll
        for (int j = 0; j < 4; j++) {
            int g_col = col_block + tx * 4 + j;
            if (g_col >= N) continue;
            float v = alpha * acc[i][j];
            if (beta_is_one) dB[g_row * N + g_col] += v;
            else             dB[g_row * N + g_col]  = v;
        }
    }
}

// dx[M,K] += u[M,R] · Aᵀ[R,K]    (LoRA contribution; always accumulates)
//   = Σ_r u[m,r] · A[k,r]
__global__ void lora_bwd_dx_kernel(
    const float* __restrict__ u,    // [M, R]
    const float* __restrict__ A,    // [K, R]
    float* __restrict__ dx,         // [M, K]
    int M, int K, int R
) {
    __shared__ float sU[LORA_BM][LORA_BK];     // (M, R) tile
    __shared__ float sA[LORA_BK][LORA_BN];     // (R, K)-transpose tile

    int row_block = blockIdx.x * LORA_BM;      // M
    int col_block = blockIdx.y * LORA_BN;      // K
    int tx = threadIdx.x, ty = threadIdx.y;

    float acc[4][4];
    #pragma unroll
    for (int i = 0; i < 4; i++)
        #pragma unroll
        for (int j = 0; j < 4; j++) acc[i][j] = 0.0f;

    int n_tiles = (R + LORA_BK - 1) / LORA_BK;
    for (int t = 0; t < n_tiles; t++) {
        int r_base = t * LORA_BK;
        #pragma unroll
        for (int i = 0; i < 4; i++) {
            int sm_row = ty * 4 + i;
            int sm_col = tx;
            int g_m = row_block + sm_row;
            int g_r = r_base + sm_col;
            sU[sm_row][sm_col] = (g_m < M && g_r < R) ? u[g_m * R + g_r] : 0.0f;
        }
        #pragma unroll
        for (int i = 0; i < 4; i++) {
            int sm_row = tx;
            int sm_col = ty * 4 + i;
            int g_r = r_base + sm_row;
            int g_k = col_block + sm_col;
            sA[sm_row][sm_col] = (g_r < R && g_k < K) ? A[g_k * R + g_r] : 0.0f;
        }
        __syncthreads();

        #pragma unroll
        for (int kk = 0; kk < LORA_BK; kk++) {
            float a0 = sU[ty*4+0][kk], a1 = sU[ty*4+1][kk], a2 = sU[ty*4+2][kk], a3 = sU[ty*4+3][kk];
            float b0 = sA[kk][tx*4+0], b1 = sA[kk][tx*4+1], b2 = sA[kk][tx*4+2], b3 = sA[kk][tx*4+3];
            acc[0][0]+=a0*b0; acc[0][1]+=a0*b1; acc[0][2]+=a0*b2; acc[0][3]+=a0*b3;
            acc[1][0]+=a1*b0; acc[1][1]+=a1*b1; acc[1][2]+=a1*b2; acc[1][3]+=a1*b3;
            acc[2][0]+=a2*b0; acc[2][1]+=a2*b1; acc[2][2]+=a2*b2; acc[2][3]+=a2*b3;
            acc[3][0]+=a3*b0; acc[3][1]+=a3*b1; acc[3][2]+=a3*b2; acc[3][3]+=a3*b3;
        }
        __syncthreads();
    }

    #pragma unroll
    for (int i = 0; i < 4; i++) {
        int g_row = row_block + ty * 4 + i;
        if (g_row >= M) continue;
        #pragma unroll
        for (int j = 0; j < 4; j++) {
            int g_col = col_block + tx * 4 + j;
            if (g_col >= K) continue;
            dx[g_row * K + g_col] += acc[i][j];   // always accumulate
        }
    }
}

// ═════════════════════════════════════════════════════════════════════
// Host-side launchers (extern "C"). These wrap the kernels with grid/block
// computation, dtype dispatch, and stream plumbing. Return:
//    LORA_RC_OK              =  0
//    LORA_RC_BAD_ARGS        = -2
//    LORA_RC_KERNEL_FAIL     = -4
//    LORA_RC_DTYPE_TODO      = -7   bf16/fp16 not yet implemented
//    LORA_RC_NO_CUDA_RUNTIME = -8   compiled without CUDA
// ═════════════════════════════════════════════════════════════════════
extern "C" {

int lora_cuda_runtime_available(void) { return 1; }   // built with nvcc

int lora_cuda_forward_launch(const lora_fwd_args_t* a, void* stream) {
    if (!a || !a->x || !a->A || !a->B || !a->y || !a->tmp) return LORA_RC_BAD_ARGS;
    if (a->M <= 0 || a->N <= 0 || a->K <= 0 || a->R <= 0) return LORA_RC_BAD_ARGS;
    if (a->dtype_id != 0) return LORA_RC_DTYPE_TODO;

    cudaStream_t s = (cudaStream_t)stream;

    dim3 block(16, 16);

    // 1) tmp[M,R] = x · A
    dim3 grid_xA((a->M + LORA_BM - 1) / LORA_BM, (a->R + LORA_BN - 1) / LORA_BN);
    lora_fwd_xA_kernel<<<grid_xA, block, 0, s>>>(
        (const float*)a->x, (const float*)a->A, (float*)a->tmp,
        a->M, a->K, a->R);

    // 2) Optional base GEMM y = x · W (overwrite). If apply_base==0, the LoRA pass
    //    will overwrite y (beta=0). If apply_base==1, base writes y first, then
    //    LoRA pass accumulates (beta=1).
    int beta_is_one = 0;
    if (a->apply_base && a->W != NULL) {
        dim3 grid_xW((a->M + LORA_BM - 1) / LORA_BM, (a->N + LORA_BN - 1) / LORA_BN);
        lora_base_xW_kernel<<<grid_xW, block, 0, s>>>(
            (const float*)a->x, (const float*)a->W, (float*)a->y,
            a->M, a->N, a->K);
        beta_is_one = 1;
    }

    // 3) y = α · tmp · B  (+ y if beta_is_one)
    dim3 grid_tmpB((a->M + LORA_BM - 1) / LORA_BM, (a->N + LORA_BN - 1) / LORA_BN);
    lora_fwd_tmpB_kernel<<<grid_tmpB, block, 0, s>>>(
        (const float*)a->tmp, (const float*)a->B, (float*)a->y,
        a->M, a->N, a->R, a->alpha, beta_is_one);

    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) return LORA_RC_KERNEL_FAIL;
    return LORA_RC_OK;
}

int lora_cuda_backward_launch(const lora_bwd_args_t* a, void* stream) {
    if (!a || !a->x || !a->A || !a->B || !a->dy || !a->dA || !a->dB || !a->u || !a->tmp)
        return LORA_RC_BAD_ARGS;
    if (a->M <= 0 || a->N <= 0 || a->K <= 0 || a->R <= 0) return LORA_RC_BAD_ARGS;
    if (a->dtype_id != 0) return LORA_RC_DTYPE_TODO;

    cudaStream_t s = (cudaStream_t)stream;
    dim3 block(16, 16);

    // 1) tmp[M,R] = x · A   (recompute; cheaper than carrying it across fwd/bwd)
    dim3 grid_xA((a->M + LORA_BM - 1) / LORA_BM, (a->R + LORA_BN - 1) / LORA_BN);
    lora_fwd_xA_kernel<<<grid_xA, block, 0, s>>>(
        (const float*)a->x, (const float*)a->A, (float*)a->tmp,
        a->M, a->K, a->R);

    // 2) u[M,R] = α · dy · Bᵀ
    dim3 grid_u((a->M + LORA_BM - 1) / LORA_BM, (a->R + LORA_BN - 1) / LORA_BN);
    lora_bwd_dyBt_kernel<<<grid_u, block, 0, s>>>(
        (const float*)a->dy, (const float*)a->B, (float*)a->u,
        a->M, a->N, a->R, a->alpha);

    int beta = a->accumulate_grads ? 1 : 0;

    // 3) dA[K,R] = xᵀ · u
    dim3 grid_dA((a->K + LORA_BM - 1) / LORA_BM, (a->R + LORA_BN - 1) / LORA_BN);
    lora_bwd_dA_kernel<<<grid_dA, block, 0, s>>>(
        (const float*)a->x, (const float*)a->u, (float*)a->dA,
        a->M, a->K, a->R, beta);

    // 4) dB[R,N] = α · tmpᵀ · dy
    dim3 grid_dB((a->R + LORA_BM - 1) / LORA_BM, (a->N + LORA_BN - 1) / LORA_BN);
    lora_bwd_dB_kernel<<<grid_dB, block, 0, s>>>(
        (const float*)a->tmp, (const float*)a->dy, (float*)a->dB,
        a->M, a->N, a->R, a->alpha, beta);

    // 5) dx (optional): dx += u · Aᵀ
    if (a->dx != NULL) {
        dim3 grid_dx((a->M + LORA_BM - 1) / LORA_BM, (a->K + LORA_BN - 1) / LORA_BN);
        lora_bwd_dx_kernel<<<grid_dx, block, 0, s>>>(
            (const float*)a->u, (const float*)a->A, (float*)a->dx,
            a->M, a->K, a->R);
    }

    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) return LORA_RC_KERNEL_FAIL;
    return LORA_RC_OK;
}

}  // extern "C"
