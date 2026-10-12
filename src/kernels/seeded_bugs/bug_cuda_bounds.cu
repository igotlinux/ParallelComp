// Seeded Bug: Missing bounds check on GPU
// KernelGuard CUDA Verification
#include <cuda_runtime.h>

__global__ void bug_cuda_bounds_kernel(const float* a, const float* b, float* c, size_t n) {
    size_t idx = blockIdx.x * blockDim.x + threadIdx.x;
    // INTENTIONAL BUG: Omitted `if (idx < n)` check!
    // Threads where idx >= n write out-of-bounds into global memory!
    c[idx] = a[idx] + b[idx];
}

extern "C" void launch_bug_cuda_bounds(const float* d_a, const float* d_b, float* d_c, size_t n, cudaStream_t stream) {
    if (n == 0) return;
    int blockSize = 256;
    int numBlocks = (n + blockSize - 1) / blockSize;
    bug_cuda_bounds_kernel<<<numBlocks, blockSize, 0, stream>>>(d_a, d_b, d_c, n);
}
