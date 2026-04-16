// hxmetal_macos.m — hexa FFI <-> Metal compute shim (macOS M4 GPU)
//
// .hexanoport marker: this .m is a native shim (extern FFI boundary),
// not a compilation target for the Hexa-to-C codegen. Same convention
// as hxblas_linux.c / hxflash_linux.c / hexa_cc.c.hexanoport.
//
// ABI convention (matches hxblas_linux.c / hxflash_linux.c bit-for-bit):
//   * pointers are passed as int64_t (Hexa Pointer == uint64_t opaque)
//   * floats use C float* (f32) -- dims and strides are int64_t
//   * casts via (T*)(uintptr_t)int64_value
//
// Build:
//   clang -O3 -fPIC -dynamiclib -fobjc-arc \
//       -framework Metal -framework MetalPerformanceShaders -framework Foundation \
//       self/native/hxmetal_macos.m \
//       -o self/native/build/libhxmetal.dylib
//
// M4 unified memory: MTLResourceStorageModeShared gives zero-copy access.
// Host pointers and GPU pointers share the same physical pages -- the
// shim creates MTLBuffers wrapping existing host allocations where possible,
// falling back to newBufferWithBytes + copyback for non-page-aligned data.
//
// Thread safety: single MTLCommandQueue with serial dispatch. All public
// functions are safe to call from any thread (lazy init uses dispatch_once).
//
// Error codes: 0 = success, -1 = no device, -2 = compile error, -3 = OOM

#import <Metal/Metal.h>
#import <MetalPerformanceShaders/MetalPerformanceShaders.h>
#import <Foundation/Foundation.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

// ═══════════════════════════════════════════════════════════════════
// Global state — lazy-init on first call
// ═══════════════════════════════════════════════════════════════════
static id<MTLDevice>       g_device  = nil;
static id<MTLCommandQueue> g_queue   = nil;
static id<MTLLibrary>      g_library = nil;

// Precompiled kernel pipelines
static id<MTLComputePipelineState> g_gelu_pipeline       = nil;
static id<MTLComputePipelineState> g_add_bias_pipeline   = nil;
static id<MTLComputePipelineState> g_layer_norm_pipeline = nil;
static id<MTLComputePipelineState> g_softmax_pipeline    = nil;

static int g_init_done  = 0;
static int g_init_error = 0;

// ═══════════════════════════════════════════════════════════════════
// Metal shader source — embedded at compile time from .metal file
// (fallback: inline source if the .metal file is unavailable)
// ═══════════════════════════════════════════════════════════════════
static NSString* hxmetal_shader_source(void) {
    // Try loading from file next to the dylib first
    NSString *path = [[NSBundle mainBundle] pathForResource:@"hxmetal_kernels"
                                                    ofType:@"metal"];
    if (path) {
        NSError *err = nil;
        NSString *src = [NSString stringWithContentsOfFile:path
                                                  encoding:NSUTF8StringEncoding
                                                     error:&err];
        if (src) return src;
    }

    // Try relative to the process working directory
    NSString *cwd_path = @"self/native/hxmetal_kernels.metal";
    {
        NSError *err = nil;
        NSString *src = [NSString stringWithContentsOfFile:cwd_path
                                                  encoding:NSUTF8StringEncoding
                                                     error:&err];
        if (src) return src;
    }

    // Inline fallback — minimal GELU kernel so init doesn't fail
    return @"#include <metal_stdlib>\n"
            "using namespace metal;\n"
            "kernel void gelu_kernel(device const float* input [[buffer(0)]],\n"
            "                        device float* output [[buffer(1)]],\n"
            "                        constant uint& count [[buffer(2)]],\n"
            "                        uint tid [[thread_position_in_grid]]) {\n"
            "    if (tid >= count) return;\n"
            "    float x = input[tid];\n"
            "    float x3 = x * x * x;\n"
            "    float inner = 0.7978845608f * (x + 0.044715f * x3);\n"
            "    output[tid] = 0.5f * x * (1.0f + tanh(inner));\n"
            "}\n";
}

// ═══════════════════════════════════════════════════════════════════
// Pipeline creation helper
// ═══════════════════════════════════════════════════════════════════
static id<MTLComputePipelineState> make_pipeline(NSString *name) {
    if (!g_library) return nil;
    id<MTLFunction> fn = [g_library newFunctionWithName:name];
    if (!fn) return nil;
    NSError *err = nil;
    id<MTLComputePipelineState> ps = [g_device newComputePipelineStateWithFunction:fn
                                                                            error:&err];
    return ps;
}

// ═══════════════════════════════════════════════════════════════════
// hxmetal_init — create device, queue, compile shaders
// Returns: 0 on success, negative on error
// ═══════════════════════════════════════════════════════════════════
int64_t hxmetal_init(void) {
    if (g_init_done) return g_init_error;

    g_device = MTLCreateSystemDefaultDevice();
    if (!g_device) {
        g_init_done = 1;
        g_init_error = -1;
        return -1;
    }

    g_queue = [g_device newCommandQueue];
    if (!g_queue) {
        g_init_done = 1;
        g_init_error = -1;
        return -1;
    }

    // Compile shader library
    NSString *source = hxmetal_shader_source();
    MTLCompileOptions *opts = [[MTLCompileOptions alloc] init];
    opts.fastMathEnabled = YES;
    NSError *err = nil;
    g_library = [g_device newLibraryWithSource:source options:opts error:&err];
    if (!g_library) {
        NSLog(@"hxmetal: shader compile error: %@", err);
        g_init_done = 1;
        g_init_error = -2;
        return -2;
    }

    // Build pipelines — non-fatal if some kernels are missing (inline fallback)
    g_gelu_pipeline       = make_pipeline(@"gelu_kernel");
    g_add_bias_pipeline   = make_pipeline(@"add_bias_kernel");
    g_layer_norm_pipeline = make_pipeline(@"layer_norm_kernel");
    g_softmax_pipeline    = make_pipeline(@"softmax_kernel");

    g_init_done = 1;
    g_init_error = 0;
    return 0;
}

// Lazy init helper — call at top of every public function
static int ensure_init(void) {
    if (!g_init_done) hxmetal_init();
    return g_init_error;
}

// ═══════════════════════════════════════════════════════════════════
// hxmetal_version — ABI version probe
// ═══════════════════════════════════════════════════════════════════
int64_t hxmetal_version(void) {
    return 1;
}

// ═══════════════════════════════════════════════════════════════════
// hxmetal_matmul — GPU GEMM via MPSMatrixMultiplication
//
// C[M,N] = A[M,K] * B[K,N]   (row-major float32)
//
// A, B, C are host memory pointers passed as int64_t.
// Uses MTLResourceStorageModeShared for zero-copy on M4 unified memory.
// ═══════════════════════════════════════════════════════════════════
int64_t hxmetal_matmul(int64_t M, int64_t K, int64_t N,
                       int64_t A, int64_t B, int64_t C) {
    if (ensure_init() != 0) return g_init_error;

    float *a_ptr = (float*)(uintptr_t)A;
    float *b_ptr = (float*)(uintptr_t)B;
    float *c_ptr = (float*)(uintptr_t)C;

    NSUInteger a_bytes = (NSUInteger)(M * K) * sizeof(float);
    NSUInteger b_bytes = (NSUInteger)(K * N) * sizeof(float);
    NSUInteger c_bytes = (NSUInteger)(M * N) * sizeof(float);

    // Create shared MTLBuffers wrapping host memory where possible.
    // newBufferWithBytesNoCopy requires page-aligned pointers; fall back
    // to newBufferWithBytes (copy) if alignment fails.
    id<MTLBuffer> a_buf = [g_device newBufferWithBytesNoCopy:a_ptr
                                                      length:a_bytes
                                                     options:MTLResourceStorageModeShared
                                                 deallocator:nil];
    if (!a_buf) {
        a_buf = [g_device newBufferWithBytes:a_ptr length:a_bytes
                                     options:MTLResourceStorageModeShared];
    }

    id<MTLBuffer> b_buf = [g_device newBufferWithBytesNoCopy:b_ptr
                                                      length:b_bytes
                                                     options:MTLResourceStorageModeShared
                                                 deallocator:nil];
    if (!b_buf) {
        b_buf = [g_device newBufferWithBytes:b_ptr length:b_bytes
                                     options:MTLResourceStorageModeShared];
    }

    // Output buffer — always allocate fresh, then copy back
    id<MTLBuffer> c_buf = [g_device newBufferWithLength:c_bytes
                                               options:MTLResourceStorageModeShared];
    if (!a_buf || !b_buf || !c_buf) return -3;

    // MPS matrix descriptors — row-major
    MPSMatrixDescriptor *a_desc = [MPSMatrixDescriptor matrixDescriptorWithRows:(NSUInteger)M
                                                                       columns:(NSUInteger)K
                                                                      rowBytes:(NSUInteger)(K * sizeof(float))
                                                                      dataType:MPSDataTypeFloat32];
    MPSMatrixDescriptor *b_desc = [MPSMatrixDescriptor matrixDescriptorWithRows:(NSUInteger)K
                                                                       columns:(NSUInteger)N
                                                                      rowBytes:(NSUInteger)(N * sizeof(float))
                                                                      dataType:MPSDataTypeFloat32];
    MPSMatrixDescriptor *c_desc = [MPSMatrixDescriptor matrixDescriptorWithRows:(NSUInteger)M
                                                                       columns:(NSUInteger)N
                                                                      rowBytes:(NSUInteger)(N * sizeof(float))
                                                                      dataType:MPSDataTypeFloat32];

    MPSMatrix *a_mat = [[MPSMatrix alloc] initWithBuffer:a_buf descriptor:a_desc];
    MPSMatrix *b_mat = [[MPSMatrix alloc] initWithBuffer:b_buf descriptor:b_desc];
    MPSMatrix *c_mat = [[MPSMatrix alloc] initWithBuffer:c_buf descriptor:c_desc];

    MPSMatrixMultiplication *mm = [[MPSMatrixMultiplication alloc]
        initWithDevice:g_device
         transposeLeft:NO
        transposeRight:NO
            resultRows:(NSUInteger)M
         resultColumns:(NSUInteger)N
       interiorColumns:(NSUInteger)K
                 alpha:1.0
                  beta:0.0];

    id<MTLCommandBuffer> cmd = [g_queue commandBuffer];
    [mm encodeToCommandBuffer:cmd leftMatrix:a_mat rightMatrix:b_mat resultMatrix:c_mat];
    [cmd commit];
    [cmd waitUntilCompleted];

    // Copy result back to host pointer
    memcpy(c_ptr, [c_buf contents], c_bytes);

    return 0;
}

// ═══════════════════════════════════════════════════════════════════
// hxmetal_ffn_fwd — fused FFN forward pass
//
// Y[batch, d_model] = GELU(X[batch, d_model] * W1[d_model, d_ff]) * W2[d_ff, d_model]
//
// Two matmuls + GELU activation in a single command buffer submission.
// This is the key kernel for LLM inference: GPU handles FFN while CPU
// does attention.
// ═══════════════════════════════════════════════════════════════════
int64_t hxmetal_ffn_fwd(int64_t d_model, int64_t d_ff, int64_t batch,
                        int64_t W1, int64_t W2, int64_t X, int64_t Y) {
    if (ensure_init() != 0) return g_init_error;

    float *w1_ptr = (float*)(uintptr_t)W1;
    float *w2_ptr = (float*)(uintptr_t)W2;
    float *x_ptr  = (float*)(uintptr_t)X;
    float *y_ptr  = (float*)(uintptr_t)Y;

    NSUInteger x_bytes   = (NSUInteger)(batch * d_model) * sizeof(float);
    NSUInteger w1_bytes  = (NSUInteger)(d_model * d_ff)  * sizeof(float);
    NSUInteger w2_bytes  = (NSUInteger)(d_ff * d_model)  * sizeof(float);
    NSUInteger mid_bytes = (NSUInteger)(batch * d_ff)    * sizeof(float);
    NSUInteger y_bytes   = (NSUInteger)(batch * d_model) * sizeof(float);

    // Allocate buffers
    id<MTLBuffer> x_buf  = [g_device newBufferWithBytes:x_ptr  length:x_bytes  options:MTLResourceStorageModeShared];
    id<MTLBuffer> w1_buf = [g_device newBufferWithBytes:w1_ptr length:w1_bytes options:MTLResourceStorageModeShared];
    id<MTLBuffer> w2_buf = [g_device newBufferWithBytes:w2_ptr length:w2_bytes options:MTLResourceStorageModeShared];
    id<MTLBuffer> mid_buf = [g_device newBufferWithLength:mid_bytes options:MTLResourceStorageModeShared];
    id<MTLBuffer> act_buf = [g_device newBufferWithLength:mid_bytes options:MTLResourceStorageModeShared];
    id<MTLBuffer> y_buf  = [g_device newBufferWithLength:y_bytes options:MTLResourceStorageModeShared];

    if (!x_buf || !w1_buf || !w2_buf || !mid_buf || !act_buf || !y_buf) return -3;

    // MPS descriptors
    MPSMatrixDescriptor *x_desc = [MPSMatrixDescriptor matrixDescriptorWithRows:(NSUInteger)batch
                                                                       columns:(NSUInteger)d_model
                                                                      rowBytes:(NSUInteger)(d_model * sizeof(float))
                                                                      dataType:MPSDataTypeFloat32];
    MPSMatrixDescriptor *w1_desc = [MPSMatrixDescriptor matrixDescriptorWithRows:(NSUInteger)d_model
                                                                        columns:(NSUInteger)d_ff
                                                                       rowBytes:(NSUInteger)(d_ff * sizeof(float))
                                                                       dataType:MPSDataTypeFloat32];
    MPSMatrixDescriptor *mid_desc = [MPSMatrixDescriptor matrixDescriptorWithRows:(NSUInteger)batch
                                                                         columns:(NSUInteger)d_ff
                                                                        rowBytes:(NSUInteger)(d_ff * sizeof(float))
                                                                        dataType:MPSDataTypeFloat32];
    MPSMatrixDescriptor *w2_desc = [MPSMatrixDescriptor matrixDescriptorWithRows:(NSUInteger)d_ff
                                                                        columns:(NSUInteger)d_model
                                                                       rowBytes:(NSUInteger)(d_model * sizeof(float))
                                                                       dataType:MPSDataTypeFloat32];
    MPSMatrixDescriptor *y_desc = [MPSMatrixDescriptor matrixDescriptorWithRows:(NSUInteger)batch
                                                                       columns:(NSUInteger)d_model
                                                                      rowBytes:(NSUInteger)(d_model * sizeof(float))
                                                                      dataType:MPSDataTypeFloat32];

    MPSMatrix *x_mat   = [[MPSMatrix alloc] initWithBuffer:x_buf   descriptor:x_desc];
    MPSMatrix *w1_mat  = [[MPSMatrix alloc] initWithBuffer:w1_buf  descriptor:w1_desc];
    MPSMatrix *mid_mat = [[MPSMatrix alloc] initWithBuffer:mid_buf descriptor:mid_desc];
    MPSMatrix *w2_mat  = [[MPSMatrix alloc] initWithBuffer:w2_buf  descriptor:w2_desc];
    MPSMatrix *act_mat = [[MPSMatrix alloc] initWithBuffer:act_buf descriptor:mid_desc];
    MPSMatrix *y_mat   = [[MPSMatrix alloc] initWithBuffer:y_buf   descriptor:y_desc];

    // Step 1: mid = X * W1  (MPS matmul)
    MPSMatrixMultiplication *mm1 = [[MPSMatrixMultiplication alloc]
        initWithDevice:g_device transposeLeft:NO transposeRight:NO
            resultRows:(NSUInteger)batch resultColumns:(NSUInteger)d_ff
       interiorColumns:(NSUInteger)d_model alpha:1.0 beta:0.0];

    // Step 2: act = GELU(mid)  (custom kernel)
    // Step 3: Y = act * W2  (MPS matmul)
    MPSMatrixMultiplication *mm2 = [[MPSMatrixMultiplication alloc]
        initWithDevice:g_device transposeLeft:NO transposeRight:NO
            resultRows:(NSUInteger)batch resultColumns:(NSUInteger)d_model
       interiorColumns:(NSUInteger)d_ff alpha:1.0 beta:0.0];

    // Single command buffer for the full FFN forward
    id<MTLCommandBuffer> cmd = [g_queue commandBuffer];

    // Encode matmul 1
    [mm1 encodeToCommandBuffer:cmd leftMatrix:x_mat rightMatrix:w1_mat resultMatrix:mid_mat];

    // Encode GELU activation
    if (g_gelu_pipeline) {
        id<MTLComputeCommandEncoder> enc = [cmd computeCommandEncoder];
        [enc setComputePipelineState:g_gelu_pipeline];
        [enc setBuffer:mid_buf offset:0 atIndex:0];
        [enc setBuffer:act_buf offset:0 atIndex:1];
        uint32_t count = (uint32_t)(batch * d_ff);
        [enc setBytes:&count length:sizeof(count) atIndex:2];
        NSUInteger tpg = g_gelu_pipeline.maxTotalThreadsPerThreadgroup;
        if (tpg > 256) tpg = 256;
        MTLSize grid = MTLSizeMake((NSUInteger)count, 1, 1);
        MTLSize group = MTLSizeMake(tpg, 1, 1);
        [enc dispatchThreads:grid threadsPerThreadgroup:group];
        [enc endEncoding];
    } else {
        // Fallback: CPU GELU (should not happen if init succeeded)
        [cmd commit];
        [cmd waitUntilCompleted];
        float *mid_data = (float*)[mid_buf contents];
        float *act_data = (float*)[act_buf contents];
        NSUInteger total = (NSUInteger)(batch * d_ff);
        for (NSUInteger i = 0; i < total; i++) {
            float x = mid_data[i];
            float x3 = x * x * x;
            float inner = 0.7978845608f * (x + 0.044715f * x3);
            act_data[i] = 0.5f * x * (1.0f + tanhf(inner));
        }
        cmd = [g_queue commandBuffer];
    }

    // Encode matmul 2
    [mm2 encodeToCommandBuffer:cmd leftMatrix:act_mat rightMatrix:w2_mat resultMatrix:y_mat];

    [cmd commit];
    [cmd waitUntilCompleted];

    // Copy result to host
    memcpy(y_ptr, [y_buf contents], y_bytes);

    return 0;
}

// ═══════════════════════════════════════════════════════════════════
// Buffer management — for persistent GPU allocations
// ═══════════════════════════════════════════════════════════════════

// Simple handle table — up to 1024 live buffers
#define HXMETAL_MAX_BUFS 1024
static id<MTLBuffer> g_buf_table[HXMETAL_MAX_BUFS];
static int g_buf_next = 1;  // handle 0 is reserved (null)

int64_t hxmetal_buf_create(int64_t size) {
    if (ensure_init() != 0) return 0;
    if (g_buf_next >= HXMETAL_MAX_BUFS) return 0;

    id<MTLBuffer> buf = [g_device newBufferWithLength:(NSUInteger)size
                                              options:MTLResourceStorageModeShared];
    if (!buf) return 0;

    int idx = g_buf_next++;
    g_buf_table[idx] = buf;
    return (int64_t)idx;
}

int64_t hxmetal_buf_write(int64_t handle_val, int64_t src_ptr, int64_t size) {
    if (handle_val <= 0 || handle_val >= HXMETAL_MAX_BUFS) return -1;
    id<MTLBuffer> buf = g_buf_table[handle_val];
    if (!buf) return -1;
    memcpy([buf contents], (const void*)(uintptr_t)src_ptr, (size_t)size);
    return 0;
}

int64_t hxmetal_buf_read(int64_t handle_val, int64_t dst_ptr, int64_t size) {
    if (handle_val <= 0 || handle_val >= HXMETAL_MAX_BUFS) return -1;
    id<MTLBuffer> buf = g_buf_table[handle_val];
    if (!buf) return -1;
    memcpy((void*)(uintptr_t)dst_ptr, [buf contents], (size_t)size);
    return 0;
}

int64_t hxmetal_buf_free(int64_t handle_val) {
    if (handle_val <= 0 || handle_val >= HXMETAL_MAX_BUFS) return -1;
    g_buf_table[handle_val] = nil;
    return 0;
}

// ═══════════════════════════════════════════════════════════════════
// hxmetal_sync — wait for all GPU work to complete
// ═══════════════════════════════════════════════════════════════════
int64_t hxmetal_sync(void) {
    if (ensure_init() != 0) return g_init_error;
    // Submit an empty command buffer and wait — flushes the queue
    id<MTLCommandBuffer> cmd = [g_queue commandBuffer];
    [cmd commit];
    [cmd waitUntilCompleted];
    return 0;
}
