// CUDA Vector Addition Implementation
// KernelGuard CUDA Backend
#include <cuda_runtime.h>
#include <cstdio>

__global__ void vec_add_cuda_kernel(const float* __restrict__ a,
                                    const float* __restrict__ b,
                                    float* __restrict__ c,
                                    size_t n) {
    size_t idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) {
        c[idx] = a[idx] + b[idx];
    }
}

extern "C" void launch_vec_add_cuda(const float* d_a, const float* d_b, float* d_c, size_t n, cudaStream_t stream) {
    if (n == 0) return;
    int blockSize = 256;
    int numBlocks = (n + blockSize - 1) / blockSize;
    vec_add_cuda_kernel<<<numBlocks, blockSize, 0, stream>>>(d_a, d_b, d_c, n);
}
