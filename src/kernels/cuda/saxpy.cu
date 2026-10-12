// CUDA SAXPY Implementation
// KernelGuard CUDA Backend
#include <cuda_runtime.h>

__global__ void saxpy_cuda_kernel(float alpha,
                                  const float* __restrict__ x,
                                  const float* __restrict__ y,
                                  float* __restrict__ out,
                                  size_t n) {
    size_t idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) {
        out[idx] = fmaf(alpha, x[idx], y[idx]);
    }
}

extern "C" void launch_saxpy_cuda(float alpha, const float* d_x, const float* d_y, float* d_out, size_t n, cudaStream_t stream) {
    if (n == 0) return;
    int blockSize = 256;
    int numBlocks = (n + blockSize - 1) / blockSize;
    saxpy_cuda_kernel<<<numBlocks, blockSize, 0, stream>>>(alpha, d_x, d_y, d_out, n);
}
