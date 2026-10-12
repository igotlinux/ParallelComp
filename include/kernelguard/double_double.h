#pragma once

#include <cstdint>
#include <cmath>
#include <string>
#include <algorithm>
#include <cstring>

namespace kernelguard {

// DoubleDouble represents an unevaluated sum of two IEEE 754 double-precision numbers:
//   value = hi + lo, where |lo| <= 0.5 * ulp(hi).
// This provides approximately 106 bits of significand precision (~31 decimal digits),
// providing an exact oracle for single-precision (24 bits) and near-oracle for double-precision (53 bits).
struct DoubleDouble {
    double hi;
    double lo;

    DoubleDouble() : hi(0.0), lo(0.0) {}
    DoubleDouble(double h) : hi(h), lo(0.0) {}
    DoubleDouble(double h, double l) : hi(h), lo(l) {}

    static DoubleDouble from_float(float f) {
        return DoubleDouble(static_cast<double>(f), 0.0);
    }

    float to_float() const {
        return static_cast<float>(hi + lo);
    }

    double to_double() const {
        return hi + lo;
    }

    bool is_nan() const {
        return std::isnan(hi) || std::isnan(lo);
    }

    bool is_inf() const {
        return std::isinf(hi);
    }

    bool is_zero() const {
        return hi == 0.0 && lo == 0.0;
    }
};

// Error-free transformation: Knuth 2Sum.
// Computes s = a + b and err = (a + b) - s exactly in IEEE arithmetic.
inline DoubleDouble two_sum(double a, double b) {
    double s = a + b;
    double v = s - a;
    double err = (a - (s - v)) + (b - v);
    return DoubleDouble(s, err);
}

// Error-free transformation: Fast2Sum (requires |a| >= |b|).
inline DoubleDouble fast_two_sum(double a, double b) {
    double s = a + b;
    double err = b - (s - a);
    return DoubleDouble(s, err);
}

// Dekker splitting for exact product: splits double into two 26-bit chunks.
inline void split(double a, double& ahi, double& alo) {
    const double SPLITTER = 134217729.0; // 2^27 + 1
    double c = SPLITTER * a;
    ahi = c - (c - a);
    alo = a - ahi;
}

// Error-free transformation: Dekker 2Prod.
// Computes p = a * b and err = exact error.
inline DoubleDouble two_prod(double a, double b) {
    double p = a * b;
#if defined(__FMA__) || defined(__AVX2__)
    double err = std::fma(a, b, -p);
#else
    double ahi, alo, bhi, blo;
    split(a, ahi, alo);
    split(b, bhi, blo);
    double err = ((ahi * bhi - p) + ahi * blo + alo * bhi) + alo * blo;
#endif
    return DoubleDouble(p, err);
}

// Double-double arithmetic operations
inline DoubleDouble dd_add(const DoubleDouble& a, const DoubleDouble& b) {
    DoubleDouble s = two_sum(a.hi, b.hi);
    DoubleDouble t = two_sum(a.lo, b.lo);
    double c = s.lo + t.hi;
    DoubleDouble v = fast_two_sum(s.hi, c);
    double w = t.lo + v.lo;
    return fast_two_sum(v.hi, w);
}

inline DoubleDouble dd_sub(const DoubleDouble& a, const DoubleDouble& b) {
    return dd_add(a, DoubleDouble(-b.hi, -b.lo));
}

inline DoubleDouble dd_mul(const DoubleDouble& a, const DoubleDouble& b) {
    DoubleDouble p = two_prod(a.hi, b.hi);
    p.lo += a.hi * b.lo + a.lo * b.hi;
    return fast_two_sum(p.hi, p.lo);
}

inline DoubleDouble dd_div(const DoubleDouble& a, const DoubleDouble& b) {
    double q1 = a.hi / b.hi;
    DoubleDouble p = two_prod(q1, b.hi);
    double r1 = (a.hi - p.hi) - p.lo;
    double r = r1 + a.lo - q1 * b.lo;
    double q2 = r / b.hi;
    return fast_two_sum(q1, q2);
}

// IEEE 754 Floating-Point Introspection and ULP calculations
inline uint32_t float_as_uint32(float f) {
    uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

inline float uint32_as_float(uint32_t u) {
    float f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

inline uint64_t double_as_uint64(double d) {
    uint64_t u;
    std::memcpy(&u, &d, sizeof(u));
    return u;
}

inline double uint64_as_double(uint64_t u) {
    double d;
    std::memcpy(&d, &u, sizeof(d));
    return d;
}

// Calculates exact signed distance in Units in the Last Place (ULP) between two float values.
// Handles signed zeros, subnormals, infinities, and NaNs.
inline int64_t compute_float_ulp_distance(float actual, float expected) {
    if (std::isnan(actual) || std::isnan(expected)) {
        return (std::isnan(actual) && std::isnan(expected)) ? 0 : INT64_MAX;
    }
    if (std::isinf(actual) || std::isinf(expected)) {
        if (actual == expected) return 0;
        return INT64_MAX;
    }

    uint32_t u_act = float_as_uint32(actual);
    uint32_t u_exp = float_as_uint32(expected);

    // IEEE 754 sign-magnitude to 2's complement mapping for lexicographical ordering
    int32_t i_act = (u_act & 0x80000000u) ? (0x80000000u - u_act) : static_cast<int32_t>(u_act);
    int32_t i_exp = (u_exp & 0x80000000u) ? (0x80000000u - u_exp) : static_cast<int32_t>(u_exp);

    return std::abs(static_cast<int64_t>(i_act) - static_cast<int64_t>(i_exp));
}

// Returns true if value is a subnormal (denormal) float
inline bool is_subnormal_float(float f) {
    uint32_t u = float_as_uint32(f);
    uint32_t exp = (u >> 23) & 0xFF;
    uint32_t mant = u & 0x7FFFFF;
    return exp == 0 && mant != 0;
}

// Returns float with Flush-To-Zero (FTZ) applied: subnormals flushed to signed zero
inline float flush_subnormal_to_zero(float f) {
    if (is_subnormal_float(f)) {
        uint32_t sign = float_as_uint32(f) & 0x80000000u;
        return uint32_as_float(sign);
    }
    return f;
}

} // namespace kernelguard
