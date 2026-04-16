// hxmetal_kernels.metal — Metal compute kernels for hxmetal FFI
//
// Custom kernels for non-MPS operations. MPSMatrixMultiplication handles
// GEMM; these cover element-wise and reduction ops that MPS doesn't
// directly expose with the right signature.
//
// Compiled at runtime via MTLDevice newLibraryWithSource or precompiled
// via `xcrun -sdk macosx metal -c` + `xcrun -sdk macosx metallib`.

#include <metal_stdlib>
using namespace metal;

// ── GELU activation (exact formulation) ─────────────────────────
// y = 0.5 * x * (1 + tanh(sqrt(2/pi) * (x + 0.044715 * x^3)))
kernel void gelu_kernel(device const float* input  [[buffer(0)]],
                        device float*       output [[buffer(1)]],
                        constant uint&      count  [[buffer(2)]],
                        uint tid [[thread_position_in_grid]]) {
    if (tid >= count) return;
    float x = input[tid];
    float x3 = x * x * x;
    float inner = 0.7978845608f * (x + 0.044715f * x3);  // sqrt(2/pi) ~ 0.7978845608
    output[tid] = 0.5f * x * (1.0f + tanh(inner));
}

// ── Add bias — add bias vector to each row of [rows, cols] matrix ──
// out[i, j] = input[i, j] + bias[j]
kernel void add_bias_kernel(device const float* input  [[buffer(0)]],
                            device const float* bias   [[buffer(1)]],
                            device float*       output [[buffer(2)]],
                            constant uint&      rows   [[buffer(3)]],
                            constant uint&      cols   [[buffer(4)]],
                            uint tid [[thread_position_in_grid]]) {
    if (tid >= rows * cols) return;
    uint col = tid % cols;
    output[tid] = input[tid] + bias[col];
}

// ── Layer normalization ─────────────────────────────────────────
// Per-row: y[i,j] = gamma[j] * (x[i,j] - mean) / sqrt(var + eps) + beta[j]
// One threadgroup per row; uses shared memory for reduction.
kernel void layer_norm_kernel(device const float* input  [[buffer(0)]],
                              device const float* gamma  [[buffer(1)]],
                              device const float* beta   [[buffer(2)]],
                              device float*       output [[buffer(3)]],
                              constant uint&      rows   [[buffer(4)]],
                              constant uint&      cols   [[buffer(5)]],
                              constant float&     eps    [[buffer(6)]],
                              uint row_id  [[threadgroup_position_in_grid]],
                              uint lane    [[thread_position_in_threadgroup]],
                              uint tg_size [[threads_per_threadgroup]]) {
    if (row_id >= rows) return;

    device const float* row_in  = input  + row_id * cols;
    device float*       row_out = output + row_id * cols;

    // Pass 1: compute mean (strided sum)
    float sum = 0.0f;
    for (uint j = lane; j < cols; j += tg_size) {
        sum += row_in[j];
    }
    // Warp reduce
    sum += simd_shuffle_xor(sum, 1);
    sum += simd_shuffle_xor(sum, 2);
    sum += simd_shuffle_xor(sum, 4);
    sum += simd_shuffle_xor(sum, 8);
    sum += simd_shuffle_xor(sum, 16);
    float mean = sum / float(cols);

    // Pass 2: compute variance
    float var_sum = 0.0f;
    for (uint j = lane; j < cols; j += tg_size) {
        float diff = row_in[j] - mean;
        var_sum += diff * diff;
    }
    var_sum += simd_shuffle_xor(var_sum, 1);
    var_sum += simd_shuffle_xor(var_sum, 2);
    var_sum += simd_shuffle_xor(var_sum, 4);
    var_sum += simd_shuffle_xor(var_sum, 8);
    var_sum += simd_shuffle_xor(var_sum, 16);
    float inv_std = rsqrt(var_sum / float(cols) + eps);

    // Pass 3: normalize + affine
    for (uint j = lane; j < cols; j += tg_size) {
        row_out[j] = gamma[j] * (row_in[j] - mean) * inv_std + beta[j];
    }
}

// ── Row-wise softmax ────────────────────────────────────────────
// One threadgroup per row. Three-pass: max, exp+sum, normalize.
kernel void softmax_kernel(device const float* input  [[buffer(0)]],
                           device float*       output [[buffer(1)]],
                           constant uint&      rows   [[buffer(2)]],
                           constant uint&      cols   [[buffer(3)]],
                           uint row_id  [[threadgroup_position_in_grid]],
                           uint lane    [[thread_position_in_threadgroup]],
                           uint tg_size [[threads_per_threadgroup]]) {
    if (row_id >= rows) return;

    device const float* row_in  = input  + row_id * cols;
    device float*       row_out = output + row_id * cols;

    // Pass 1: find row max
    float row_max = -INFINITY;
    for (uint j = lane; j < cols; j += tg_size) {
        row_max = max(row_max, row_in[j]);
    }
    row_max = max(row_max, simd_shuffle_xor(row_max, 1));
    row_max = max(row_max, simd_shuffle_xor(row_max, 2));
    row_max = max(row_max, simd_shuffle_xor(row_max, 4));
    row_max = max(row_max, simd_shuffle_xor(row_max, 8));
    row_max = max(row_max, simd_shuffle_xor(row_max, 16));

    // Pass 2: exp + sum
    float exp_sum = 0.0f;
    for (uint j = lane; j < cols; j += tg_size) {
        float e = exp(row_in[j] - row_max);
        row_out[j] = e;
        exp_sum += e;
    }
    exp_sum += simd_shuffle_xor(exp_sum, 1);
    exp_sum += simd_shuffle_xor(exp_sum, 2);
    exp_sum += simd_shuffle_xor(exp_sum, 4);
    exp_sum += simd_shuffle_xor(exp_sum, 8);
    exp_sum += simd_shuffle_xor(exp_sum, 16);

    // Pass 3: normalize
    float inv_sum = 1.0f / exp_sum;
    for (uint j = lane; j < cols; j += tg_size) {
        row_out[j] *= inv_sum;
    }
}
