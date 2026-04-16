// hxcuda_fused.cu -- C4 CUDA fused mega-kernel MVP (bf16 Tensor Core)
//
// C4 roadmap -- "CUDA fused mega-kernels + bf16 Tensor Core"
// Target: H100 (sm_90) MFU 27% -> 60%+
//
// Contains:
//   hxcuda_matmul_bf16   -- bf16 Tensor Core GEMM via WMMA (16x16x16 tiles)
//   hxcuda_fused_lmhead_fwd -- fused matmul + softmax (logits + probs + loss)
//   hxcuda_version       -- ABI version probe
//
// Architecture:
//   Inputs/outputs are fp32 host-side. Internal computation casts to bf16,
//   uses nvcuda::wmma for 16x16x16 Tensor Core tiles, accumulates in fp32.
//   This avoids changing the hexa-side data layout while exploiting the
//   ~2x throughput of bf16 Tensor Cores on H100 (sm_90).
//
// ABI convention (matches hxblas_linux.c / hxflash_linux.c):
//   * pointers passed as int64_t (Hexa Pointer == u64), cast via uintptr_t
//   * scalar floats as double on hexa side, cast to float internally
//   * struct-args ABI for >6 arg functions (host_ffi_call_6 ceiling)
//
// Compile:
//   nvcc -arch=sm_90 -O2 --shared -Xcompiler -fPIC \
//        self/native/hxcuda_fused.cu -o self/native/build/libhxcuda.so
//   See scripts/build_hxcuda_linux.sh for the canonical builder.
//
// .hexanoport marker convention:
//   This .cu is a native shim (extern FFI boundary), not a compilation
//   target for the Hexa-to-C codegen. See hexa_cc.c.hexanoport.

#include <cuda_runtime.h>
#include <cuda_fp16.h>
#include <mma.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>

using namespace nvcuda;

// =========================================================================
//  Constants
// =========================================================================

// WMMA tile dimensions for bf16 (16x16x16 is the native H100 shape)
#define WMMA_M 16
#define WMMA_N 16
#define WMMA_K 16

// Thread block config for matmul: one warp per 16x16 output tile
#define MATMUL_BLOCK_X 16
#define MATMUL_BLOCK_Y 16

// Softmax block size (threads per row)
#define SOFTMAX_BLOCK 256

// =========================================================================
//  Kernel 1: hxcuda_matmul_bf16_kernel
//
//  C[M,N] = A[M,K] * B[K,N]   (row-major fp32 I/O, bf16 internal)
//
//  Each warp computes one 16x16 output tile. For each k-step we load
//  16x16 fragments of A and B, cast fp32->bf16 on the fly, execute
//  wmma::mma_sync, and accumulate into an fp32 accumulator fragment.
//  Final result is stored back as fp32.
//
//  Grid: (ceil(N/16), ceil(M/16))
//  Block: (32, 1, 1) -- one warp per tile
// =========================================================================

__global__ void hxcuda_matmul_bf16_kernel(
    const float* __restrict__ A,   // [M, K] row-major
    const float* __restrict__ B,   // [K, N] row-major
    float*       __restrict__ C,   // [M, N] row-major
    int M, int N, int K)
{
    // Each block is one warp (32 threads) computing one 16x16 output tile.
    const int warpM = blockIdx.y * WMMA_M;
    const int warpN = blockIdx.x * WMMA_N;

    // Declare WMMA fragments
    wmma::fragment<wmma::matrix_a, WMMA_M, WMMA_N, WMMA_K, __nv_bfloat16, wmma::row_major> a_frag;
    wmma::fragment<wmma::matrix_b, WMMA_M, WMMA_N, WMMA_K, __nv_bfloat16, wmma::row_major> b_frag;
    wmma::fragment<wmma::accumulator, WMMA_M, WMMA_N, WMMA_K, float> c_frag;

    // Zero the accumulator
    wmma::fill_fragment(c_frag, 0.0f);

    // Shared memory for fp32->bf16 staging (one tile of A and one of B)
    __shared__ __nv_bfloat16 smem_a[WMMA_M * WMMA_K];
    __shared__ __nv_bfloat16 smem_b[WMMA_K * WMMA_N];

    const int tid = threadIdx.x;  // 0..31

    // Loop over K dimension in steps of WMMA_K (=16)
    for (int k_step = 0; k_step < K; k_step += WMMA_K) {

        // Cooperatively load A tile [warpM..warpM+16, k_step..k_step+16]
        // 256 elements, 32 threads => 8 elements per thread
        for (int i = tid; i < WMMA_M * WMMA_K; i += 32) {
            int row = i / WMMA_K;
            int col = i % WMMA_K;
            int gRow = warpM + row;
            int gCol = k_step + col;
            float val = 0.0f;
            if (gRow < M && gCol < K) {
                val = A[gRow * K + gCol];
            }
            smem_a[i] = __float2bfloat16(val);
        }

        // Cooperatively load B tile [k_step..k_step+16, warpN..warpN+16]
        for (int i = tid; i < WMMA_K * WMMA_N; i += 32) {
            int row = i / WMMA_N;
            int col = i % WMMA_N;
            int gRow = k_step + row;
            int gCol = warpN + col;
            float val = 0.0f;
            if (gRow < K && gCol < N) {
                val = B[gRow * N + gCol];
            }
            smem_b[i] = __float2bfloat16(val);
        }

        __syncthreads();

        // Load fragments from shared memory and compute
        wmma::load_matrix_sync(a_frag, smem_a, WMMA_K);
        wmma::load_matrix_sync(b_frag, smem_b, WMMA_N);
        wmma::mma_sync(c_frag, a_frag, b_frag, c_frag);

        __syncthreads();
    }

    // Store the accumulator back to global memory (fp32)
    // Bounds check: only write valid elements
    if (warpM < M && warpN < N) {
        wmma::store_matrix_sync(C + warpM * N + warpN, c_frag, N, wmma::mem_row_major);
    }
}

// =========================================================================
//  Kernel 2: hxcuda_softmax_kernel
//
//  Per-row softmax: out[i,:] = softmax(in[i,:])
//  Also computes per-row log-sum-exp for cross-entropy loss.
//
//  Grid: (R, 1, 1)   -- one block per row
//  Block: (min(C, SOFTMAX_BLOCK), 1, 1)
//
//  Uses shared memory for max-reduction and sum-reduction.
//  Handles C > blockDim.x via thread-striding.
// =========================================================================

__global__ void hxcuda_softmax_kernel(
    const float* __restrict__ logits,   // [R, C]
    float*       __restrict__ probs,    // [R, C]
    float*       __restrict__ row_lse,  // [R] -- log-sum-exp per row (optional, may be NULL)
    int R, int C)
{
    int row = blockIdx.x;
    if (row >= R) return;
    int tid = threadIdx.x;
    int bsize = blockDim.x;

    extern __shared__ float sdata[];

    const float* in_row = logits + row * C;
    float* out_row = probs + row * C;

    // Phase 1: find max (thread-strided loop for C > bsize)
    float local_max = -1.0e30f;
    for (int j = tid; j < C; j += bsize) {
        float v = in_row[j];
        if (v > local_max) local_max = v;
    }
    sdata[tid] = local_max;
    __syncthreads();

    // Tree reduction for max
    for (int s = bsize >> 1; s > 0; s >>= 1) {
        if (tid < s) {
            float a = sdata[tid];
            float b = sdata[tid + s];
            sdata[tid] = (a > b) ? a : b;
        }
        __syncthreads();
    }
    float row_max = sdata[0];
    __syncthreads();

    // Phase 2: compute exp(x - max) and sum
    float local_sum = 0.0f;
    for (int j = tid; j < C; j += bsize) {
        float e = expf(in_row[j] - row_max);
        out_row[j] = e;   // store exp temporarily
        local_sum += e;
    }
    sdata[tid] = local_sum;
    __syncthreads();

    // Tree reduction for sum
    for (int s = bsize >> 1; s > 0; s >>= 1) {
        if (tid < s) sdata[tid] += sdata[tid + s];
        __syncthreads();
    }
    float sum = sdata[0];

    // Phase 3: normalize
    float inv_sum = 1.0f / sum;
    for (int j = tid; j < C; j += bsize) {
        out_row[j] *= inv_sum;
    }

    // Optionally store log-sum-exp for CE loss computation
    if (tid == 0 && row_lse != NULL) {
        row_lse[row] = row_max + logf(sum);
    }
}

// =========================================================================
//  Kernel 3: hxcuda_ce_loss_kernel
//
//  Cross-entropy loss from pre-computed probs and log-sum-exp.
//  loss = mean_i( lse[i] - logits[i, target[i]] )
//
//  Grid: (1, 1, 1)   -- single block aggregation
//  Block: (min(R, 256), 1, 1)
// =========================================================================

__global__ void hxcuda_ce_loss_kernel(
    const float* __restrict__ logits,   // [R, C]
    const float* __restrict__ row_lse,  // [R]
    const int*   __restrict__ targets,  // [R]
    float*       __restrict__ loss_out, // scalar
    int R, int C)
{
    int tid = threadIdx.x;
    int bsize = blockDim.x;

    extern __shared__ float sdata[];

    float local_loss = 0.0f;
    for (int i = tid; i < R; i += bsize) {
        int t = targets[i];
        // CE = log_sum_exp - logit[target]
        local_loss += row_lse[i] - logits[i * C + t];
    }
    sdata[tid] = local_loss;
    __syncthreads();

    for (int s = bsize >> 1; s > 0; s >>= 1) {
        if (tid < s) sdata[tid] += sdata[tid + s];
        __syncthreads();
    }

    if (tid == 0) {
        *loss_out = sdata[0] / (float)R;
    }
}

// =========================================================================
//  Host-side C wrappers (extern "C" for Hexa FFI)
//
//  ABI: all args as int64_t, pointers cast from uintptr_t.
//  Struct-args for functions with >6 args (host_ffi_call_6 ceiling).
// =========================================================================

extern "C" {

// ── Version probe ────────────────────────────────────────────────
//
// v1: initial MVP (matmul_bf16 + fused_lmhead_fwd)
int64_t hxcuda_version(void) {
    return 1;
}

// ── hxcuda_matmul_bf16 ──────────────────────────────────────────
//
// C[M,N] = A[M,K] * B[K,N]
// All pointers are DEVICE pointers passed as int64_t.
//
// Struct-args ABI (6 fields):
//   off 0:  M        (int64_t)
//   off 8:  K        (int64_t)
//   off 16: N        (int64_t)
//   off 24: A_ptr    (int64_t, device float*)
//   off 32: B_ptr    (int64_t, device float*)
//   off 40: C_ptr    (int64_t, device float*)
//
// Exactly 6 args, so can also call positionally -- but struct is
// provided for consistency with fused_lmhead_fwd.

typedef struct {
    int64_t M, K, N;
    int64_t A_p, B_p, C_p;
} HxCudaMatmulArgs;

int64_t hxcuda_matmul_bf16(int64_t args_p) {
    if (args_p == 0) return -1;
    HxCudaMatmulArgs* a = (HxCudaMatmulArgs*)(uintptr_t)args_p;

    int M = (int)a->M;
    int K = (int)a->K;
    int N = (int)a->N;
    const float* A = (const float*)(uintptr_t)a->A_p;
    const float* B = (const float*)(uintptr_t)a->B_p;
    float*       C = (float*)(uintptr_t)a->C_p;

    if (M <= 0 || K <= 0 || N <= 0) return -2;

    // Grid: one warp (32 threads) per 16x16 output tile
    dim3 grid((N + WMMA_N - 1) / WMMA_N, (M + WMMA_M - 1) / WMMA_M);
    dim3 block(32);  // one warp

    hxcuda_matmul_bf16_kernel<<<grid, block>>>(A, B, C, M, N, K);

    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) {
        fprintf(stderr, "[hxcuda] matmul_bf16 launch error: %s\n",
                cudaGetErrorString(err));
        return -3;
    }
    return 0;
}

// ── hxcuda_matmul_bf16_positional ───────────────────────────────
// For callers who prefer 6 positional int64 args (M,K,N,A,B,C).
// Wraps the struct path so callers can choose either ABI.
int64_t hxcuda_matmul_bf16_pos(int64_t M, int64_t K, int64_t N,
                                int64_t A_p, int64_t B_p, int64_t C_p) {
    HxCudaMatmulArgs args;
    args.M = M; args.K = K; args.N = N;
    args.A_p = A_p; args.B_p = B_p; args.C_p = C_p;
    return hxcuda_matmul_bf16((int64_t)(uintptr_t)&args);
}

// ── hxcuda_fused_lmhead_fwd ─────────────────────────────────────
//
// Fused matmul + softmax + cross-entropy for LM head forward pass:
//   logits = x[S,D] @ W[D,V]           (Tensor Core bf16 GEMM)
//   probs  = softmax(logits, dim=-1)    (shared-memory reduction)
//   loss   = mean CE(probs, targets)    (parallel reduction)
//
// Struct-args ABI (10 fields):
//   off  0: S         (int64_t)   -- sequence length
//   off  8: D         (int64_t)   -- hidden dim
//   off 16: V         (int64_t)   -- vocab size
//   off 24: x_p       (int64_t)   -- device float* [S, D]
//   off 32: W_p       (int64_t)   -- device float* [D, V]
//   off 40: logits_p  (int64_t)   -- device float* [S, V]  (output, preallocated)
//   off 48: probs_p   (int64_t)   -- device float* [S, V]  (output, preallocated)
//   off 56: targets_p (int64_t)   -- device int*   [S]
//   off 64: loss_p    (int64_t)   -- device float* scalar  (output, preallocated)
//   off 72: reserved0 (int64_t)

typedef struct {
    int64_t S, D, V;
    int64_t x_p, W_p;
    int64_t logits_p, probs_p;
    int64_t targets_p;
    int64_t loss_p;
    int64_t reserved0;
} HxCudaFusedLmheadArgs;

int64_t hxcuda_fused_lmhead_fwd(int64_t args_p) {
    if (args_p == 0) return -1;
    HxCudaFusedLmheadArgs* a = (HxCudaFusedLmheadArgs*)(uintptr_t)args_p;

    int S = (int)a->S;
    int D = (int)a->D;
    int V = (int)a->V;
    const float* x       = (const float*)(uintptr_t)a->x_p;
    const float* W       = (const float*)(uintptr_t)a->W_p;
    float*       logits  = (float*)(uintptr_t)a->logits_p;
    float*       probs   = (float*)(uintptr_t)a->probs_p;
    const int*   targets = (const int*)(uintptr_t)a->targets_p;
    float*       loss    = (float*)(uintptr_t)a->loss_p;

    if (S <= 0 || D <= 0 || V <= 0) return -2;

    cudaError_t err;

    // --- Phase 1: logits = x @ W via bf16 Tensor Core GEMM ---
    // x[S, D] @ W[D, V] = logits[S, V]
    {
        dim3 grid((V + WMMA_N - 1) / WMMA_N, (S + WMMA_M - 1) / WMMA_M);
        dim3 block(32);
        hxcuda_matmul_bf16_kernel<<<grid, block>>>(x, W, logits, S, V, D);
        err = cudaGetLastError();
        if (err != cudaSuccess) {
            fprintf(stderr, "[hxcuda] fused_lmhead matmul launch: %s\n",
                    cudaGetErrorString(err));
            return -3;
        }
    }

    // --- Phase 2: probs = softmax(logits) + log-sum-exp ---
    // Allocate temp row_lse on device
    float* row_lse = NULL;
    err = cudaMalloc(&row_lse, S * sizeof(float));
    if (err != cudaSuccess) {
        fprintf(stderr, "[hxcuda] fused_lmhead row_lse alloc: %s\n",
                cudaGetErrorString(err));
        return -4;
    }

    {
        int softmax_threads = V;
        if (softmax_threads > SOFTMAX_BLOCK) softmax_threads = SOFTMAX_BLOCK;
        // Round up to next power of 2 for reduction correctness
        int t = 1;
        while (t < softmax_threads) t <<= 1;
        softmax_threads = t;
        if (softmax_threads > 1024) softmax_threads = 1024;

        dim3 grid(S);
        dim3 block(softmax_threads);
        int shmem = softmax_threads * sizeof(float);

        hxcuda_softmax_kernel<<<grid, block, shmem>>>(
            logits, probs, row_lse, S, V);
        err = cudaGetLastError();
        if (err != cudaSuccess) {
            fprintf(stderr, "[hxcuda] fused_lmhead softmax launch: %s\n",
                    cudaGetErrorString(err));
            cudaFree(row_lse);
            return -5;
        }
    }

    // --- Phase 3: loss = mean CE ---
    {
        int ce_threads = S;
        if (ce_threads > 256) ce_threads = 256;
        int t = 1;
        while (t < ce_threads) t <<= 1;
        ce_threads = t;
        if (ce_threads > 256) ce_threads = 256;

        dim3 grid(1);
        dim3 block(ce_threads);
        int shmem = ce_threads * sizeof(float);

        hxcuda_ce_loss_kernel<<<grid, block, shmem>>>(
            logits, row_lse, targets, loss, S, V);
        err = cudaGetLastError();
        if (err != cudaSuccess) {
            fprintf(stderr, "[hxcuda] fused_lmhead ce_loss launch: %s\n",
                    cudaGetErrorString(err));
            cudaFree(row_lse);
            return -6;
        }
    }

    cudaFree(row_lse);
    return 0;
}

// ── hxcuda_device_info ──────────────────────────────────────────
// Returns compute capability * 10 (e.g. 900 for sm_90, 800 for sm_80).
// Returns 0 if no device or error.
int64_t hxcuda_device_info(void) {
    int device = 0;
    cudaError_t err = cudaGetDevice(&device);
    if (err != cudaSuccess) return 0;

    int major = 0, minor = 0;
    cudaDeviceGetAttribute(&major, cudaDevAttrComputeCapabilityMajor, device);
    cudaDeviceGetAttribute(&minor, cudaDevAttrComputeCapabilityMinor, device);
    return (int64_t)(major * 100 + minor * 10);
}

// ── hxcuda_sync ─────────────────────────────────────────────────
// Full device synchronize (for benchmarking / correctness checks).
int64_t hxcuda_sync(void) {
    cudaError_t err = cudaDeviceSynchronize();
    return (err == cudaSuccess) ? 0 : (int64_t)err;
}

} // extern "C"
