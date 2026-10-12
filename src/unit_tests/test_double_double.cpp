#include <iostream>
#include <cassert>
#include <cmath>
#include "kernelguard/double_double.h"
#include "kernelguard/classifier.h"

using namespace kernelguard;

void test_two_sum() {
    double a = 1.0;
    double b = 1e-16;
    DoubleDouble s = two_sum(a, b);
    assert(s.hi == 1.0);
    assert(s.lo == 1e-16);
    std::cout << "[PASS] test_two_sum\n";
}

void test_two_prod() {
    double x = std::ldexp(1.0, -30); // 2^-30
    double a = 1.0 + x;
    double b = 1.0 - x;
    DoubleDouble p = two_prod(a, b);
    // (1 + 2^-30)(1 - 2^-30) = 1 - 2^-60. In 53-bit double, p.hi rounds to 1.0,
    // and exact residual p.lo is -2^-60.
    double expected_err = -std::ldexp(1.0, -60);
    assert(p.hi == 1.0);
    assert(p.lo == expected_err);
    std::cout << "[PASS] test_two_prod\n";
}

void test_ulp_calculation() {
    float f1 = 1.0f;
    float f2 = uint32_as_float(float_as_uint32(1.0f) + 1);
    assert(compute_float_ulp_distance(f1, f2) == 1);

    float f3 = uint32_as_float(float_as_uint32(1.0f) + 10);
    assert(compute_float_ulp_distance(f1, f3) == 10);

    // Identical numbers have 0 ULP distance
    assert(compute_float_ulp_distance(1.2345f, 1.2345f) == 0);

    // Signed zeroes
    assert(compute_float_ulp_distance(0.0f, -0.0f) == 0);
    std::cout << "[PASS] test_ulp_calculation\n";
}

void test_subnormal_detection() {
    float normal = 1.0f;
    float subnormal = std::numeric_limits<float>::denorm_min();
    assert(!is_subnormal_float(normal));
    assert(is_subnormal_float(subnormal));

    float flushed = flush_subnormal_to_zero(subnormal);
    assert(flushed == 0.0f);
    assert(!is_subnormal_float(flushed));
    std::cout << "[PASS] test_subnormal_detection\n";
}

int main() {
    std::cout << "Running DoubleDouble Unit Tests...\n";
    test_two_sum();
    test_two_prod();
    test_ulp_calculation();
    test_subnormal_detection();
    std::cout << "All DoubleDouble Unit Tests PASSED!\n";
    return 0;
}
