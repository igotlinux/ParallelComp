// KernelGuard CUDA Hardware & Runtime Verification Harness
// Production-grade device execution runner with CPU double-double cross-check
#include <cuda_runtime.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iomanip>

#include "kernelguard/double_double.h"
#include "kernelguard/classifier.h"

// Forward declarations of CUDA kernels implemented in src/kernels/cuda/
extern "C" void launch_vec_add_cuda(const float* d_a, const float* d_b, float* d_c, size_t n, cudaStream_t stream);
extern "C" void launch_saxpy_cuda(float alpha, const float* d_x, const float* d_y, float* d_out, size_t n, cudaStream_t stream);
extern "C" void launch_dot_cuda(const float* d_x, const float* d_y, float* d_out, size_t n, cudaStream_t stream);

// Seeded CUDA bug kernels
extern "C" void launch_bug_cuda_bounds(const float* d_a, const float* d_b, float* d_c, size_t n, cudaStream_t stream);
extern "C" void launch_bug_cuda_launch_dim(const float* d_a, const float* d_b, float* d_c, size_t n, cudaStream_t stream);

#define CUDA_CHECK(call) \
    do { \
        cudaError_t err = call; \
        if (err != cudaSuccess) { \
            std::cerr << "CUDA Error at " << __FILE__ << ":" << __LINE__ << ": " \
                      << cudaGetErrorString(err) << std::endl; \
            return false; \
        } \
    } while (0)

using namespace kernelguard;

// 1. Verify CUDA Vector Addition
bool test_cuda_vec_add(size_t n) {
    if (n == 0) return true;

    std::vector<float> h_a(n), h_b(n), h_ref(n), h_res(n);
    for (size_t i = 0; i < n; ++i) {
        h_a[i] = static_cast<float>(i) * 1.5f;
        h_b[i] = static_cast<float>(i) * 2.25f;
        DoubleDouble da = DoubleDouble::from_float(h_a[i]);
        DoubleDouble db = DoubleDouble::from_float(h_b[i]);
        h_ref[i] = dd_add(da, db).to_float();
    }

    float *d_a = nullptr, *d_b = nullptr, *d_c = nullptr;
    CUDA_CHECK(cudaMalloc(&d_a, n * sizeof(float)));
    CUDA_CHECK(cudaMalloc(&d_b, n * sizeof(float)));
    CUDA_CHECK(cudaMalloc(&d_c, n * sizeof(float)));

    CUDA_CHECK(cudaMemcpy(d_a, h_a.data(), n * sizeof(float), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(d_b, h_b.data(), n * sizeof(float), cudaMemcpyHostToDevice));

    launch_vec_add_cuda(d_a, d_b, d_c, n, 0);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    CUDA_CHECK(cudaMemcpy(h_res.data(), d_c, n * sizeof(float), cudaMemcpyDeviceToHost));

    cudaFree(d_a);
    cudaFree(d_b);
    cudaFree(d_c);

    CaseVerdict v = evaluate_contract(h_res.data(), h_ref.data(), n, Contract::exact());
    return (v.type == VerdictType::PASS);
}

// 2. Verify CUDA SAXPY
bool test_cuda_saxpy(size_t n) {
    if (n == 0) return true;

    float alpha = 3.14159f;
    std::vector<float> h_x(n), h_y(n), h_ref(n), h_res(n);
    DoubleDouble d_alpha = DoubleDouble::from_float(alpha);

    for (size_t i = 0; i < n; ++i) {
        h_x[i] = static_cast<float>(i + 1) * 0.5f;
        h_y[i] = static_cast<float>(i + 1) * 1.2f;
        DoubleDouble dx = DoubleDouble::from_float(h_x[i]);
        DoubleDouble dy = DoubleDouble::from_float(h_y[i]);
        h_ref[i] = dd_add(dd_mul(d_alpha, dx), dy).to_float();
    }

    float *d_x = nullptr, *d_y = nullptr, *d_out = nullptr;
    CUDA_CHECK(cudaMalloc(&d_x, n * sizeof(float)));
    CUDA_CHECK(cudaMalloc(&d_y, n * sizeof(float)));
    CUDA_CHECK(cudaMalloc(&d_out, n * sizeof(float)));

    CUDA_CHECK(cudaMemcpy(d_x, h_x.data(), n * sizeof(float), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(d_y, h_y.data(), n * sizeof(float), cudaMemcpyHostToDevice));

    launch_saxpy_cuda(alpha, d_x, d_y, d_out, n, 0);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    CUDA_CHECK(cudaMemcpy(h_res.data(), d_out, n * sizeof(float), cudaMemcpyDeviceToHost));

    cudaFree(d_x);
    cudaFree(d_y);
    cudaFree(d_out);

    CaseVerdict v = evaluate_contract(h_res.data(), h_ref.data(), n, Contract::ulp(1));
    return (v.type == VerdictType::PASS);
}

// 3. Verify CUDA Dot Product Reduction
bool test_cuda_dot(size_t n) {
    if (n == 0) return true;

    std::vector<float> h_x(n), h_y(n);
    DoubleDouble accum(0.0);

    for (size_t i = 0; i < n; ++i) {
        h_x[i] = 0.5f;
        h_y[i] = 1.0f;
        DoubleDouble dx = DoubleDouble::from_float(h_x[i]);
        DoubleDouble dy = DoubleDouble::from_float(h_y[i]);
        accum = dd_add(accum, dd_mul(dx, dy));
    }
    float h_ref = accum.to_float();

    float *d_x = nullptr, *d_y = nullptr, *d_out = nullptr;
    CUDA_CHECK(cudaMalloc(&d_x, n * sizeof(float)));
    CUDA_CHECK(cudaMalloc(&d_y, n * sizeof(float)));
    CUDA_CHECK(cudaMalloc(&d_out, sizeof(float)));

    float zero = 0.0f;
    CUDA_CHECK(cudaMemcpy(d_out, &zero, sizeof(float), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(d_x, h_x.data(), n * sizeof(float), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(d_y, h_y.data(), n * sizeof(float), cudaMemcpyHostToDevice));

    launch_dot_cuda(d_x, d_y, d_out, n, 0);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    float h_res = 0.0f;
    CUDA_CHECK(cudaMemcpy(&h_res, d_out, sizeof(float), cudaMemcpyDeviceToHost));

    cudaFree(d_x);
    cudaFree(d_y);
    cudaFree(d_out);

    CaseVerdict v = evaluate_contract(&h_res, &h_ref, 1, Contract::bound(1e-4, 1e-4));
    return (v.type == VerdictType::PASS);
}

int main(int argc, char** argv) {
    int device_count = 0;
    cudaError_t err = cudaGetDeviceCount(&device_count);

    if (err != cudaSuccess || device_count == 0) {
        std::cout << "[SKIPPED] No compatible NVIDIA CUDA device detected ("
                  << cudaGetErrorString(err) << ")\n";
        return 0; // Graceful skip
    }

    cudaDeviceProp prop;
    cudaGetDeviceProperties(&prop, 0);
    std::cout << "=========================================================\n"
              << "KernelGuard CUDA Hardware Execution Harness\n"
              << "Device: " << prop.name << " (Compute Capability "
              << prop.major << "." << prop.minor << ")\n"
              << "=========================================================\n";

    // Non-multiples of block dimension (256) to thoroughly test boundary and tail handling
    const size_t test_sizes[] = {0, 1, 7, 33, 255, 256, 257, 1024, 65536};
    size_t passed = 0, total = 0;

    for (size_t sz : test_sizes) {
        total++;
        if (test_cuda_vec_add(sz)) passed++;
        total++;
        if (test_cuda_saxpy(sz)) passed++;
        total++;
        if (test_cuda_dot(sz)) passed++;
    }

    std::cout << "CUDA Execution Results: " << passed << "/" << total << " tests PASSED\n";
    return (passed == total) ? 0 : 1;
}
