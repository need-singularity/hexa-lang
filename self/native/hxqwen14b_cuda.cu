// hxqwen14b_cuda.cu — v5.3 CUDA kernels for hxqwen14b FFI shim.
//
// This file ships the 4 attention primitive CUDA kernels +
// host-side launchers exposed as extern "C" so the main .c shim can
// link against them. It's compiled with nvcc; the main .c continues
// to compile with gcc, avoiding the C++ goto-over-init errors that
// would otherwise fire against v5.1 pre-existing code.
//
// Symbols exported (all return int rc; 0=OK, nonzero=CUDA error):
//   hxqwen14b_cu_launch_rmsnorm
//   hxqwen14b_cu_launch_rope
//   hxqwen14b_cu_launch_gqa
//   hxqwen14b_cu_launch_swiglu
//   hxqwen14b_cu_memcpy_h2d
//   hxqwen14b_cu_memcpy_d2h
//   hxqwen14b_cu_malloc_bytes   (returns device ptr as int64)
//   hxqwen14b_cu_free_ptr
//   hxqwen14b_cu_device_sync

#include <cuda_runtime.h>
#include <cublas_v2.h>
#include <stdint.h>
#include <math.h>

#define HXQ_RC_OK          0
#define HXQ_RC_KERNEL_FAIL -4

// ═════════════════════════════════════════════════════════════════════
// Kernel 1: RMSNorm forward
//   y[i,j] = x[i,j] * rsqrt(mean(x[i]^2) + eps) * w[j]
// Grid: (M, 1). Block: threads tree-reduce per row then broadcast.
// ═════════════════════════════════════════════════════════════════════
__global__ void v53_rmsnorm_kernel(
    const float* __restrict__ x,
    const float* __restrict__ w,
    float* __restrict__ y,
    int d, float eps
) {
    int row = blockIdx.x;
    int tid = threadIdx.x;
    const float* xr = x + row * d;
    float*       yr = y + row * d;

    float sum_sq = 0.0f;
    for (int j = tid; j < d; j += blockDim.x) {
        float v = xr[j];
        sum_sq += v * v;
    }
    // warp-level reduce then cross-warp via shared.
    __shared__ float s_red[32];
    unsigned mask = 0xffffffff;
    for (int off = 16; off > 0; off >>= 1) {
        sum_sq += __shfl_down_sync(mask, sum_sq, off);
    }
    int warp_id = tid >> 5;
    int lane    = tid & 31;
    if (lane == 0) s_red[warp_id] = sum_sq;
    __syncthreads();
    if (warp_id == 0) {
        int n_warps = (blockDim.x + 31) >> 5;
        float v = (lane < n_warps) ? s_red[lane] : 0.0f;
        for (int off = 16; off > 0; off >>= 1) {
            v += __shfl_down_sync(mask, v, off);
        }
        if (lane == 0) s_red[0] = v;
    }
    __syncthreads();
    float mean_sq = s_red[0] / (float)d;
    float inv_rms = rsqrtf(mean_sq + eps);
    for (int j = tid; j < d; j += blockDim.x) {
        yr[j] = xr[j] * inv_rms * w[j];
    }
}

// ═════════════════════════════════════════════════════════════════════
// Kernel 2: RoPE interleaved forward (in-place)
// Grid: (B*S, n_heads). Block: head_dim/2 threads — one per rotation pair.
// ═════════════════════════════════════════════════════════════════════
__global__ void v53_rope_kernel(
    float* __restrict__ x,
    int B, int S, int H, int D,
    float theta_base, int pos_offset
) {
    int bs   = blockIdx.x;
    int h    = blockIdx.y;
    int k    = threadIdx.x;
    int half = D >> 1;
    if (k >= half) return;
    (void)B;
    int s = bs - (bs / S) * S;
    float pos = (float)(pos_offset + s);
    float inv_freq = powf(theta_base, -((float)(2 * k)) / (float)D);
    float angle    = pos * inv_freq;
    float cs, sn;
    __sincosf(angle, &sn, &cs);

    int offset = (bs * H + h) * D + 2 * k;
    float x0 = x[offset];
    float x1 = x[offset + 1];
    x[offset    ] = x0 * cs - x1 * sn;
    x[offset + 1] = x0 * sn + x1 * cs;
}

// ═════════════════════════════════════════════════════════════════════
// Kernel 3: GQA attention forward (hand-rolled online softmax)
// Grid: (B, HQ, S). Block: head_dim threads. No flash tiling — reference.
// ═════════════════════════════════════════════════════════════════════
__global__ void v53_gqa_kernel(
    const float* __restrict__ Q,
    const float* __restrict__ K,
    const float* __restrict__ V,
    float* __restrict__ O,
    int B, int S, int HQ, int HK, int D,
    float scale, int causal
) {
    int b  = blockIdx.x;
    int hq = blockIdx.y;
    int i  = blockIdx.z;
    int d  = threadIdx.x;
    if (d >= D) return;
    int G  = HQ / HK;
    int hk = hq / G;
    (void)B;

    const float* qi = Q + (((b * S + i) * HQ + hq) * D);
    float*       oi = O + (((b * S + i) * HQ + hq) * D);

    __shared__ float s_m;
    __shared__ float s_l;
    __shared__ float s_qi[256];
    __shared__ float s_dot_red[8];
    __shared__ float s_alpha;
    __shared__ float s_mnew;

    if (d == 0) { s_m = -1e30f; s_l = 0.0f; }
    s_qi[d] = qi[d];
    __syncthreads();

    float acc = 0.0f;
    int j_end = causal ? (i + 1) : S;
    unsigned mask = 0xffffffff;

    for (int j = 0; j < j_end; j++) {
        const float* kj = K + (((b * S + j) * HK + hk) * D);
        const float* vj = V + (((b * S + j) * HK + hk) * D);

        float partial = s_qi[d] * kj[d];
        for (int off = 16; off > 0; off >>= 1) {
            partial += __shfl_down_sync(mask, partial, off);
        }
        int warp = d >> 5;
        int lane = d & 31;
        if (lane == 0) s_dot_red[warp] = partial;
        __syncthreads();
        if (warp == 0) {
            int n_warps = (D + 31) >> 5;
            float v = (lane < n_warps) ? s_dot_red[lane] : 0.0f;
            for (int off = 16; off > 0; off >>= 1) {
                v += __shfl_down_sync(mask, v, off);
            }
            if (lane == 0) s_dot_red[0] = v;
        }
        __syncthreads();
        float dot = s_dot_red[0] * scale;

        if (d == 0) {
            float m_new = (dot > s_m) ? dot : s_m;
            s_alpha = expf(s_m - m_new);
            s_mnew  = m_new;
            s_m     = m_new;
        }
        __syncthreads();
        float a  = s_alpha;
        float mn = s_mnew;

        acc = acc * a;
        float p = expf(dot - mn);
        if (d == 0) s_l = s_l * a + p;
        __syncthreads();
        acc += p * vj[d];
    }
    __syncthreads();
    float inv_l = (s_l > 0.0f) ? 1.0f / s_l : 0.0f;
    oi[d] = acc * inv_l;
}

// ═════════════════════════════════════════════════════════════════════
// Kernel 4: SwiGLU forward, element-wise.
//   y[i] = silu(gate[i]) * up[i]
// ═════════════════════════════════════════════════════════════════════
__global__ void v53_swiglu_kernel(
    const float* __restrict__ g,
    const float* __restrict__ u,
    float* __restrict__ y,
    int64_t N
) {
    int64_t i = (int64_t)blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= N) return;
    float gi = g[i];
    float sig;
    if (gi >= 0.0f) {
        sig = 1.0f / (1.0f + expf(-gi));
    } else {
        float e = expf(gi);
        sig = e / (1.0f + e);
    }
    y[i] = (gi * sig) * u[i];
}

// ═════════════════════════════════════════════════════════════════════
// Host-side launchers — extern "C" so the main .c shim can link these.
// Each returns 0 on success, nonzero on CUDA error.
// ═════════════════════════════════════════════════════════════════════
extern "C" {

int hxqwen14b_cu_launch_rmsnorm(
    const float* x_dev, const float* w_dev, float* y_dev,
    int64_t M, int64_t d, float eps)
{
    int threads = 256;
    if (d < 256) threads = 128;
    if (d < 128) threads = 64;
    v53_rmsnorm_kernel<<<(unsigned)M, threads>>>(x_dev, w_dev, y_dev, (int)d, eps);
    return (cudaGetLastError() == cudaSuccess) ? HXQ_RC_OK : HXQ_RC_KERNEL_FAIL;
}

int hxqwen14b_cu_launch_rope(
    float* x_dev, int64_t B, int64_t S, int64_t H, int64_t D,
    float theta_base, int64_t pos_offset)
{
    dim3 grid((unsigned)(B * S), (unsigned)H, 1);
    int  threads = (int)(D / 2);
    if (threads < 32) threads = 32;
    v53_rope_kernel<<<grid, threads>>>(x_dev, (int)B, (int)S, (int)H, (int)D,
                                        theta_base, (int)pos_offset);
    return (cudaGetLastError() == cudaSuccess) ? HXQ_RC_OK : HXQ_RC_KERNEL_FAIL;
}

int hxqwen14b_cu_launch_gqa(
    const float* Q_dev, const float* K_dev, const float* V_dev, float* O_dev,
    int64_t B, int64_t S, int64_t HQ, int64_t HK, int64_t D,
    float scale, int64_t causal)
{
    dim3 grid((unsigned)B, (unsigned)HQ, (unsigned)S);
    int  threads = (int)D;
    v53_gqa_kernel<<<grid, threads>>>(Q_dev, K_dev, V_dev, O_dev, (int)B, (int)S,
                                       (int)HQ, (int)HK, (int)D,
                                       scale, (int)causal);
    return (cudaGetLastError() == cudaSuccess) ? HXQ_RC_OK : HXQ_RC_KERNEL_FAIL;
}

int hxqwen14b_cu_launch_swiglu(
    const float* g_dev, const float* u_dev, float* y_dev, int64_t N)
{
    int threads = 256;
    unsigned blocks = (unsigned)((N + threads - 1) / threads);
    v53_swiglu_kernel<<<blocks, threads>>>(g_dev, u_dev, y_dev, N);
    return (cudaGetLastError() == cudaSuccess) ? HXQ_RC_OK : HXQ_RC_KERNEL_FAIL;
}

// ─────────────────────────────────────────────────────────────
// Memory + sync helpers. These let the main .c shim manage device
// buffers without needing to include cuda_runtime.h directly.
// Pointers returned via int64_t for ABI cleanliness.
// ─────────────────────────────────────────────────────────────
int64_t hxqwen14b_cu_malloc_bytes(int64_t n_bytes) {
    if (n_bytes <= 0) return 0;
    void* p = NULL;
    cudaError_t e = cudaMalloc(&p, (size_t)n_bytes);
    if (e != cudaSuccess) return 0;
    return (int64_t)(uintptr_t)p;
}

int hxqwen14b_cu_free_ptr(int64_t dev_ptr) {
    if (dev_ptr == 0) return HXQ_RC_OK;
    cudaError_t e = cudaFree((void*)(uintptr_t)dev_ptr);
    return (e == cudaSuccess) ? HXQ_RC_OK : HXQ_RC_KERNEL_FAIL;
}

int hxqwen14b_cu_memcpy_h2d(int64_t dst_dev, const void* src_host, int64_t n_bytes) {
    if (dst_dev == 0 || src_host == NULL || n_bytes <= 0) return -1;
    cudaError_t e = cudaMemcpy((void*)(uintptr_t)dst_dev, src_host,
                                (size_t)n_bytes, cudaMemcpyHostToDevice);
    return (e == cudaSuccess) ? HXQ_RC_OK : HXQ_RC_KERNEL_FAIL;
}

int hxqwen14b_cu_memcpy_d2h(void* dst_host, int64_t src_dev, int64_t n_bytes) {
    if (dst_host == NULL || src_dev == 0 || n_bytes <= 0) return -1;
    cudaError_t e = cudaMemcpy(dst_host, (const void*)(uintptr_t)src_dev,
                                (size_t)n_bytes, cudaMemcpyDeviceToHost);
    return (e == cudaSuccess) ? HXQ_RC_OK : HXQ_RC_KERNEL_FAIL;
}

int hxqwen14b_cu_device_sync(void) {
    cudaError_t e = cudaDeviceSynchronize();
    return (e == cudaSuccess) ? HXQ_RC_OK : HXQ_RC_KERNEL_FAIL;
}

}  // extern "C"
