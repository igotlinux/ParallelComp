// Seeded Bug: Incorrect launch dimension truncating tail elements
// KernelGuard CUDA Verification
#include <cuda_runtime.h>

__global__ void bug_cuda_launch_dim_kernel(const float* a, const float* b, float* c, size_t n) {
    size_t idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) {
        c[idx] = a[idx] + b[idx];
    }
}

extern "C" void launch_bug_cuda_launch_dim(const float* d_a, const float* d_b, float* d_c, size_t n, cudaStream_t stream) {
    if (n == 0) return;
    int blockSize = 256;
    // INTENTIONAL BUG: Integer division without rounding up (n / blockSize)
    // Leaves remaining (n % 256) elements completely unprocessed!
    int numBlocks = n / blockSize;
    if (numBlocks > 0) {
        bug_cuda_launch_dim_kernel<<<numBlocks, blockSize, 0, stream>>>(d_a, d_b, d_c, n);
    }
}
