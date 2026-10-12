#include <cstddef>
#include "kernelguard/harness.h"

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#elif defined(__AVX2__) || defined(__AVX__)
#include <immintrin.h>
#endif

namespace kernelguard {

// 1. Scalar Reference Vector Addition
void vec_add_scalar(const float* a, const float* b, float* c, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        c[i] = a[i] + b[i];
    }
}

// 2. Vectorized Vector Addition (Hardware Intrinsics + Tail handling)
void vec_add_vectorized(const float* a, const float* b, float* c, size_t n) {
    size_t i = 0;

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    // NEON 128-bit: 4 floats per vector
    for (; i + 4 <= n; i += 4) {
        float32x4_t va = vld1q_f32(a + i);
        float32x4_t vb = vld1q_f32(b + i);
        float32x4_t vc = vaddq_f32(va, vb);
        vst1q_f32(c + i, vc);
    }
#elif defined(__AVX2__) || defined(__AVX__)
    // AVX 256-bit: 8 floats per vector
    for (; i + 8 <= n; i += 8) {
        __m256 va = _mm256_loadu_ps(a + i);
        __m256 vb = _mm256_loadu_ps(b + i);
        __m256 vc = _mm256_add_ps(va, vb);
        _mm256_storeu_ps(c + i, vc);
    }
#endif

    // Clean scalar tail loop
    for (; i < n; ++i) {
        c[i] = a[i] + b[i];
    }
}

// Autovectorization target kernel (written cleanly for compiler vectorizer)
void vec_add_autovec(const float* __restrict__ a,
                     const float* __restrict__ b,
                     float* __restrict__ c,
                     size_t n) {
#if defined(__clang__)
    #pragma clang loop vectorize(enable) interleave(enable)
#elif defined(__GNUC__)
    #pragma GCC ivdep
#endif
    for (size_t i = 0; i < n; ++i) {
        c[i] = a[i] + b[i];
    }
}

void register_vec_add_kernels() {
    auto& reg = KernelRegistry::instance();
    reg.register_vec_add("vec_add_scalar", vec_add_scalar, Contract::exact(),
                         "Baseline scalar vector addition", "scalar");
    reg.register_vec_add("vec_add_vectorized", vec_add_vectorized, Contract::exact(),
                         "SIMD vectorized vector addition with tail loop", "vector");
    reg.register_vec_add("vec_add_autovec", vec_add_autovec, Contract::exact(),
                         "Compiler autovectorization vector addition", "autovec");
}

} // namespace kernelguard
