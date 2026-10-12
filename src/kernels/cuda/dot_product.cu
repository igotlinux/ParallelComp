// CUDA Dot Product Reduction Implementation
// KernelGuard CUDA Backend
#include <cuda_runtime.h>

__global__ void dot_cuda_kernel(const float* __restrict__ x,
                                const float* __restrict__ y,
                                float* __restrict__ out,
                                size_t n) {
    __shared__ float sdata[256];
    unsigned int tid = threadIdx.x;
    size_t idx = blockIdx.x * blockDim.x + threadIdx.x;

    float my_val = 0.0f;
    if (idx < n) {
        my_val = x[idx] * y[idx];
    }
    sdata[tid] = my_val;
    __syncthreads();

    // In-block tree reduction
    for (unsigned int s = blockDim.x / 2; s > 0; s >>= 1) {
        if (tid < s) {
            sdata[tid] += sdata[tid + s];
        }
        __syncthreads();
    }

    if (tid == 0) {
        atomicAdd(out, sdata[0]);
    }
}

extern "C" void launch_dot_cuda(const float* d_x, const float* d_y, float* d_out, size_t n, cudaStream_t stream) {
    if (n == 0) return;
    int blockSize = 256;
    int numBlocks = (n + blockSize - 1) / blockSize;
    dot_cuda_kernel<<<numBlocks, blockSize, 0, stream>>>(d_x, d_y, d_out, n);
}
