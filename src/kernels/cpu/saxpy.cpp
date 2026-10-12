#include <cstddef>
#include "kernelguard/harness.h"

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#elif defined(__AVX2__) || defined(__AVX__)
#include <immintrin.h>
#endif

namespace kernelguard {

// 1. Scalar SAXPY: y[i] = alpha * x[i] + y[i]
void saxpy_scalar(float alpha, const float* x, const float* y, float* out, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        out[i] = alpha * x[i] + y[i];
    }
}

// 2. Vectorized SAXPY
void saxpy_vectorized(float alpha, const float* x, const float* y, float* out, size_t n) {
    size_t i = 0;

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    float32x4_t valpha = vdupq_n_f32(alpha);
    for (; i + 4 <= n; i += 4) {
        float32x4_t vx = vld1q_f32(x + i);
        float32x4_t vy = vld1q_f32(y + i);
        // vmlaq_f32 computes vy + valpha * vx
        float32x4_t vres = vmlaq_f32(vy, valpha, vx);
        vst1q_f32(out + i, vres);
    }
#elif defined(__AVX2__) || defined(__AVX__)
    __m256 valpha = _mm256_set1_ps(alpha);
    for (; i + 8 <= n; i += 8) {
        __m256 vx = _mm256_loadu_ps(x + i);
        __m256 vy = _mm256_loadu_ps(y + i);
#if defined(__FMA__)
        __m256 vres = _mm256_fmadd_ps(valpha, vx, vy);
#else
        __m256 vres = _mm256_add_ps(_mm256_mul_ps(valpha, vx), vy);
#endif
        _mm256_storeu_ps(out + i, vres);
    }
#endif

    // Scalar tail loop
    for (; i < n; ++i) {
        out[i] = alpha * x[i] + y[i];
    }
}

// 3. Autovectorized SAXPY
void saxpy_autovec(float alpha,
                   const float* __restrict__ x,
                   const float* __restrict__ y,
                   float* __restrict__ out,
                   size_t n) {
#if defined(__clang__)
    #pragma clang loop vectorize(enable)
#elif defined(__GNUC__)
    #pragma GCC ivdep
#endif
    for (size_t i = 0; i < n; ++i) {
        out[i] = alpha * x[i] + y[i];
    }
}

void register_saxpy_kernels() {
    auto& reg = KernelRegistry::instance();
    // Allow up to 2 ULP due to FMA (single rounding) vs separate mul+add (two roundings)
    reg.register_saxpy("saxpy_scalar", saxpy_scalar, Contract::ulp(2),
                       "Baseline scalar SAXPY", "scalar");
    reg.register_saxpy("saxpy_vectorized", saxpy_vectorized, Contract::ulp(2),
                       "SIMD vectorized SAXPY with tail loop", "vector");
    reg.register_saxpy("saxpy_autovec", saxpy_autovec, Contract::ulp(2),
                       "Compiler autovectorized SAXPY", "autovec");
}

} // namespace kernelguard
