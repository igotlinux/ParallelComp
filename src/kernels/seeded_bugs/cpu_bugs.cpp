#include <cstddef>
#include <climits>
#include "kernelguard/harness.h"

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#elif defined(__AVX2__) || defined(__AVX__)
#include <immintrin.h>
#endif

namespace kernelguard {

// BUG 1: Missing tail handling (ignores last n % 4 or n % 8 elements)
void bug_tail_missing(const float* a, const float* b, float* c, size_t n) {
    size_t i = 0;
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    for (; i + 4 <= n; i += 4) {
        float32x4_t va = vld1q_f32(a + i);
        float32x4_t vb = vld1q_f32(b + i);
        vst1q_f32(c + i, vaddq_f32(va, vb));
    }
#elif defined(__AVX2__) || defined(__AVX__)
    for (; i + 8 <= n; i += 8) {
        __m256 va = _mm256_loadu_ps(a + i);
        __m256 vb = _mm256_loadu_ps(b + i);
        _mm256_storeu_ps(c + i, _mm256_add_ps(va, vb));
    }
#else
    for (; i + 4 <= n; i += 4) {
        c[i] = a[i] + b[i];
        c[i+1] = a[i+1] + b[i+1];
        c[i+2] = a[i+2] + b[i+2];
        c[i+3] = a[i+3] + b[i+3];
    }
#endif
    // INTENTIONAL BUG: Tail loop completely omitted!
    // Remaining elements in c[i...n-1] are untouched and contain CANARY pattern.
}

// BUG 2: Off-by-one indexing writing past buffer (i <= n)
void bug_off_by_one(const float* a, const float* b, float* c, size_t n) {
    // INTENTIONAL BUG: <= instead of <
    for (size_t i = 0; i <= n; ++i) {
        c[i] = a[i] + b[i];
    }
}

// BUG 3: Incorrect alignment assumptions (demands strict 32-byte alignment)
void bug_misaligned(const float* a, const float* b, float* c, size_t n) {
    // If pointer is not aligned to 32 bytes, trigger an intentional crash / memory fault
    uintptr_t pa = reinterpret_cast<uintptr_t>(a);
    uintptr_t pb = reinterpret_cast<uintptr_t>(b);
    uintptr_t pc = reinterpret_cast<uintptr_t>(c);

    if ((pa % 32 != 0) || (pb % 32 != 0) || (pc % 32 != 0)) {
        // INTENTIONAL BUG: Crash on misaligned pointer access
        volatile int* crash_ptr = nullptr;
        *crash_ptr = 42;
    }

    for (size_t i = 0; i < n; ++i) {
        c[i] = a[i] + b[i];
    }
}

// BUG 4: Signed-overflow undefined behavior
void bug_signed_overflow(const float* a, const float* b, float* c, size_t n) {
    // Intentional integer index overflow UB
    int32_t big_offset = INT32_MAX - 10;
    for (size_t i = 0; i < n; ++i) {
        // Compute bogus index with deliberate signed overflow if i > 10
        if (i > 10) {
            volatile int32_t overflow_val = big_offset + static_cast<int32_t>(i);
            (void)overflow_val;
            c[i] = a[i] + b[i] + 999.0f; // Corrupt value
        } else {
            c[i] = a[i] + b[i];
        }
    }
}

// BUG 5: Incorrect reduction logic (identity = 1.0f instead of 0.0f)
float bug_reduction_identity(const float* x, const float* y, size_t n) {
    // INTENTIONAL BUG: accumulator initialized to 1.0f instead of 0.0f
    float sum = 1.0f;
    for (size_t i = 0; i < n; ++i) {
        sum += x[i] * y[i];
    }
    return sum;
}

// BUG 6: FMA-sensitive divergence / ordering bug
void bug_fma_divergence(float alpha, const float* x, const float* y, float* out, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        // Invert order or perturb intermediate rounding
        float prod = alpha * x[i];
        if (i % 2 == 1) {
            out[i] = (prod + 1e-4f) + y[i] - 1e-4f; // Distorts rounding
        } else {
            out[i] = prod + y[i];
        }
    }
}

// BUG 7: Denormal flush-to-zero bug
void bug_denormal_ftz(const float* a, const float* b, float* c, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        float val = a[i] + b[i];
        // INTENTIONAL BUG: Artificially flush any subnormal output or inputs to zero
        if (std::abs(val) < 1.17549435e-38f) {
            c[i] = 0.0f;
        } else {
            c[i] = val;
        }
    }
}

void register_seeded_bug_kernels() {
    auto& reg = KernelRegistry::instance();
    reg.register_vec_add("bug_tail_missing", bug_tail_missing, Contract::exact(),
                         "Seeded Bug: Tail loop omitted in vectorization", "bug", true);
    reg.register_vec_add("bug_off_by_one", bug_off_by_one, Contract::exact(),
                         "Seeded Bug: Off-by-one writes past end of buffer", "bug", true);
    reg.register_vec_add("bug_misaligned", bug_misaligned, Contract::exact(),
                         "Seeded Bug: Crashes on misaligned pointers", "bug", true);
    reg.register_vec_add("bug_signed_overflow", bug_signed_overflow, Contract::exact(),
                         "Seeded Bug: Signed overflow calculation error", "bug", true);
    reg.register_dot("bug_reduction_identity", bug_reduction_identity, Contract::bound(1e-4, 1e-5),
                     "Seeded Bug: Dot product initialized to 1.0f identity", "bug", true);
    reg.register_saxpy("bug_fma_divergence", bug_fma_divergence, Contract::ulp(1),
                       "Seeded Bug: Numerical divergence in FMA evaluation", "bug", true);
    reg.register_vec_add("bug_denormal_ftz", bug_denormal_ftz, Contract::exact(),
                         "Seeded Bug: Flushes subnormals to 0.0", "bug", true);
}

} // namespace kernelguard
