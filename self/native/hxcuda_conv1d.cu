// hxcuda_conv1d.cu -- Conv1D CUDA kernel for HEXA-SPEAK ConvNeXt backbone
//
// bf16 I/O, fp32 accumulation. Supports stride, zero-padding, dilation.
// Grid: one block per (batch, output_channel), threads cover T_out.
// T_out = (T + 2*padding - dilation*(K-1) - 1) / stride + 1
//
// ABI: struct-args, pointers as int64_t (matches hxcuda_fused/stft).
// Compile: link into libhxcuda.so via build_hxcuda_linux.sh

#include <cuda_runtime.h>
#include <cuda_bf16.h>
#include <stdint.h>
#include <stdio.h>

#define CONV1D_BLOCK 256

// input[B,C_in,T] weight[C_out,C_in,K] bias[C_out] output[B,C_out,T_out]
__global__ void conv1d_bf16_kernel(
    const __nv_bfloat16* __restrict__ input,
    const __nv_bfloat16* __restrict__ weight,
    const __nv_bfloat16* __restrict__ bias,
    __nv_bfloat16*       __restrict__ output,
    int64_t B, int64_t C_in, int64_t T, int64_t C_out,
    int64_t K, int64_t T_out, int64_t stride,
    int64_t padding, int64_t dilation)
{
    int64_t b   = blockIdx.x;
    int64_t c_o = blockIdx.y;
    if (b >= B || c_o >= C_out) return;

    float b_val = (bias != NULL) ? __bfloat162float(bias[c_o]) : 0.0f;
    const __nv_bfloat16* w_co  = weight + c_o * C_in * K;
    const __nv_bfloat16* inp_b = input  + b * C_in * T;
    __nv_bfloat16* out_bco = output + b * C_out * T_out + c_o * T_out;

    for (int64_t t = threadIdx.x; t < T_out; t += blockDim.x) {
        float acc = b_val;
        for (int64_t c_i = 0; c_i < C_in; ++c_i) {
            const __nv_bfloat16* inp_bc = inp_b + c_i * T;
            const __nv_bfloat16* w_ck   = w_co  + c_i * K;
            for (int64_t k = 0; k < K; ++k) {
                int64_t t_in = t * stride - padding + k * dilation;
                if (t_in >= 0 && t_in < T)
                    acc += __bfloat162float(inp_bc[t_in]) * __bfloat162float(w_ck[k]);
            }
        }
        out_bco[t] = __float2bfloat16(acc);
    }
}

extern "C" {

// Struct-args ABI (12 fields, 96 bytes):
//   B, C_in, T, C_out, K, stride, padding, dilation  (int64_t x8)
//   input_p, weight_p, bias_p, output_p               (int64_t x4, device ptrs)
// bias_p = 0 means no bias. Caller pre-allocates output with correct T_out.
// Returns 0 on success, negative on error.

typedef struct {
    int64_t B, C_in, T, C_out, K;
    int64_t stride, padding, dilation;
    int64_t input_p, weight_p, bias_p, output_p;
} HxCudaConv1dArgs;

int64_t hxcuda_conv1d_bf16(int64_t args_p) {
    if (args_p == 0) return -1;
    HxCudaConv1dArgs* a = (HxCudaConv1dArgs*)(uintptr_t)args_p;

    if (a->B <= 0 || a->C_in <= 0 || a->T <= 0)  return -2;
    if (a->C_out <= 0 || a->K <= 0)               return -3;
    if (a->stride <= 0 || a->dilation <= 0)        return -4;
    if (a->padding < 0)                            return -5;

    const __nv_bfloat16* input_d  = (const __nv_bfloat16*)(uintptr_t)a->input_p;
    const __nv_bfloat16* weight_d = (const __nv_bfloat16*)(uintptr_t)a->weight_p;
    const __nv_bfloat16* bias_d   = (a->bias_p != 0)
                                    ? (const __nv_bfloat16*)(uintptr_t)a->bias_p : NULL;
    __nv_bfloat16* output_d = (__nv_bfloat16*)(uintptr_t)a->output_p;
    if (!input_d || !weight_d || !output_d) return -6;

    int64_t T_out = (a->T + 2*a->padding - a->dilation*(a->K - 1) - 1) / a->stride + 1;
    if (T_out <= 0) return -7;

    dim3 grid((unsigned)a->B, (unsigned)a->C_out);
    conv1d_bf16_kernel<<<grid, CONV1D_BLOCK>>>(
        input_d, weight_d, bias_d, output_d,
        a->B, a->C_in, a->T, a->C_out, a->K,
        T_out, a->stride, a->padding, a->dilation);

    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) {
        fprintf(stderr, "[hxcuda_conv1d] launch: %s\n", cudaGetErrorString(err));
        return -8;
    }
    return 0;
}

// Self-test: 3-tap averaging filter [1/3,1/3,1/3] on [1,2,3,4,5]
// Expected: T_out=3, output=[2.0, 3.0, 4.0]. Returns 0=PASS.
int64_t hxcuda_conv1d_selftest(void) {
    const int T = 5, K = 3, T_out = 3;
    const float third = 1.0f / 3.0f;

    __nv_bfloat16 h_in[5], h_wt[3], h_out[3];
    for (int i = 0; i < T; ++i) h_in[i] = __float2bfloat16((float)(i + 1));
    for (int i = 0; i < K; ++i) h_wt[i] = __float2bfloat16(third);

    __nv_bfloat16 *d_in, *d_wt, *d_out;
    if (cudaMalloc(&d_in,  T * sizeof(__nv_bfloat16))     != cudaSuccess) return -10;
    if (cudaMalloc(&d_wt,  K * sizeof(__nv_bfloat16))     != cudaSuccess) { cudaFree(d_in); return -11; }
    if (cudaMalloc(&d_out, T_out * sizeof(__nv_bfloat16)) != cudaSuccess) { cudaFree(d_in); cudaFree(d_wt); return -12; }

    cudaMemcpy(d_in, h_in, T * sizeof(__nv_bfloat16), cudaMemcpyHostToDevice);
    cudaMemcpy(d_wt, h_wt, K * sizeof(__nv_bfloat16), cudaMemcpyHostToDevice);

    HxCudaConv1dArgs args = {1,1,T,1,K, 1,0,1,
        (int64_t)(uintptr_t)d_in, (int64_t)(uintptr_t)d_wt, 0,
        (int64_t)(uintptr_t)d_out};

    int64_t rc = hxcuda_conv1d_bf16((int64_t)(uintptr_t)&args);
    if (rc != 0) { cudaFree(d_in); cudaFree(d_wt); cudaFree(d_out); return -13; }

    cudaDeviceSynchronize();
    cudaMemcpy(h_out, d_out, T_out * sizeof(__nv_bfloat16), cudaMemcpyDeviceToHost);
    cudaFree(d_in); cudaFree(d_wt); cudaFree(d_out);

    float expected[3] = {2.0f, 3.0f, 4.0f};
    for (int i = 0; i < T_out; ++i) {
        float got = __bfloat162float(h_out[i]);
        float diff = got - expected[i];
        if (diff < 0) diff = -diff;
        if (diff > 0.1f) {  // bf16 tolerance
            fprintf(stderr, "[conv1d_selftest] FAIL[%d]: %.4f != %.4f\n", i, got, expected[i]);
            return -14;
        }
    }
    return 0;
}

int64_t hxcuda_conv1d_version(void) {
    return 1;  // v1: Conv1D bf16, stride/padding/dilation
}

}  // extern "C"
