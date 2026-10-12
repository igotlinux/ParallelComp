#include <cstddef>
#include "kernelguard/harness.h"

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#elif defined(__AVX2__) || defined(__AVX__)
#include <immintrin.h>
#endif

namespace kernelguard {

// 1. Scalar Dot Product
float dot_scalar(const float* x, const float* y, size_t n) {
    float sum = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        sum += x[i] * y[i];
    }
    return sum;
}

// 2. Vectorized Dot Product
float dot_vectorized(const float* x, const float* y, size_t n) {
    size_t i = 0;
    float sum = 0.0f;

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    float32x4_t vacc = vdupq_n_f32(0.0f);
    for (; i + 4 <= n; i += 4) {
        float32x4_t vx = vld1q_f32(x + i);
        float32x4_t vy = vld1q_f32(y + i);
        vacc = vmlaq_f32(vacc, vx, vy);
    }
    // Horizontal add of 4 lanes
    float temp[4];
    vst1q_f32(temp, vacc);
    sum = temp[0] + temp[1] + temp[2] + temp[3];
#elif defined(__AVX2__) || defined(__AVX__)
    __m256 vacc = _mm256_setzero_ps();
    for (; i + 8 <= n; i += 8) {
        __m256 vx = _mm256_loadu_ps(x + i);
        __m256 vy = _mm256_loadu_ps(y + i);
#if defined(__FMA__)
        vacc = _mm256_fmadd_ps(vx, vy, vacc);
#else
        vacc = _mm256_add_ps(vacc, _mm256_mul_ps(vx, vy));
#endif
    }
    // Horizontal reduction
    float temp[8];
    _mm256_storeu_ps(temp, vacc);
    for (int k = 0; k < 8; ++k) sum += temp[k];
#endif

    // Tail loop
    for (; i < n; ++i) {
        sum += x[i] * y[i];
    }

    return sum;
}

// 3. Autovectorized Dot Product
float dot_autovec(const float* __restrict__ x,
                  const float* __restrict__ y,
                  size_t n) {
    float sum = 0.0f;
#if defined(__clang__)
    #pragma clang loop vectorize(enable)
#elif defined(__GNUC__)
    #pragma GCC ivdep
#endif
    for (size_t i = 0; i < n; ++i) {
        sum += x[i] * y[i];
    }
    return sum;
}

// 4. llama.cpp Production Kernel (ggml_vec_dot_f32)
float ggml_vec_dot_f32(const float* x, const float* y, size_t n) {
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    float32x4_t sumv = vdupq_n_f32(0.0f);
    size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        sumv = vmlaq_f32(sumv, vld1q_f32(x + i), vld1q_f32(y + i));
    }
    float sum = vaddvq_f32(sumv);
    for (; i < n; ++i) {
        sum += x[i] * y[i];
    }
    return sum;
#elif defined(__AVX2__) || defined(__AVX__)
    __m256 sumv = _mm256_setzero_ps();
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
#if defined(__FMA__)
        sumv = _mm256_fmadd_ps(_mm256_loadu_ps(x + i), _mm256_loadu_ps(y + i), sumv);
#else
        sumv = _mm256_add_ps(_mm256_mul_ps(_mm256_loadu_ps(x + i), _mm256_loadu_ps(y + i)), sumv);
#endif
    }
    float temp[8];
    _mm256_storeu_ps(temp, sumv);
    float sum = 0.0f;
    for (int k = 0; k < 8; ++k) sum += temp[k];
    for (; i < n; ++i) {
        sum += x[i] * y[i];
    }
    return sum;
#else
    float sum = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        sum += x[i] * y[i];
    }
    return sum;
#endif
}

void register_dot_kernels() {
    auto& reg = KernelRegistry::instance();
    // Dot product accumulation order differs across vector widths, so relative bound applies
    reg.register_dot("dot_scalar", dot_scalar, Contract::bound(1e-4, 1e-5),
                     "Baseline scalar dot product", "scalar");
    reg.register_dot("dot_vectorized", dot_vectorized, Contract::bound(1e-4, 1e-5),
                     "SIMD vectorized dot product with horizontal reduction", "vector");
    reg.register_dot("dot_autovec", dot_autovec, Contract::bound(1e-4, 1e-5),
                     "Compiler autovectorized dot product", "autovec");

    // Aliases matching kernelguard.yaml config names
    reg.register_dot("dot_product_scalar", dot_scalar, Contract::bound(1e-4, 1e-5),
                     "Baseline scalar dot product", "scalar");
    reg.register_dot("dot_product_vectorized", dot_vectorized, Contract::bound(1e-4, 1e-5),
                     "SIMD vectorized dot product with horizontal reduction", "vector");
    reg.register_dot("dot_product_autovec", dot_autovec, Contract::bound(1e-4, 1e-5),
                     "Compiler autovectorized dot product", "autovec");

    // Real-world production llama.cpp kernel
    reg.register_dot("ggml_vec_dot_f32", ggml_vec_dot_f32, Contract::bound(1e-4, 1e-5),
                     "llama.cpp production transformer dot product", "llama_cpp");
}

} // namespace kernelguard

