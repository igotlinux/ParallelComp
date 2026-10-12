#pragma once

#include <vector>
#include <random>
#include <cmath>
#include <cstdint>
#include <string>
#include "double_double.h"

namespace kernelguard {

enum class DistributionType {
    UNIFORM,
    SUBNORMAL,
    CANCELLATION,
    SPECIAL_VALUES,
    POWERS_OF_TWO,
    WIDE_DYNAMIC_RANGE
};

inline std::string distribution_to_string(DistributionType d) {
    switch (d) {
        case DistributionType::UNIFORM: return "uniform";
        case DistributionType::SUBNORMAL: return "subnormal";
        case DistributionType::CANCELLATION: return "cancellation";
        case DistributionType::SPECIAL_VALUES: return "special_values";
        case DistributionType::POWERS_OF_TWO: return "powers_of_two";
        case DistributionType::WIDE_DYNAMIC_RANGE: return "wide_dynamic_range";
    }
    return "unknown";
}

struct TestCaseConfig {
    size_t size;
    DistributionType distribution;
    size_t misalign_bytes;
    uint32_t seed;
    std::string name;

    TestCaseConfig(size_t n, DistributionType dist, size_t misalign = 0, uint32_t s = 42, const std::string& nm = "")
        : size(n), distribution(dist), misalign_bytes(misalign), seed(s), name(nm) {
        if (name.empty()) {
            name = "n" + std::to_string(size) + "_" + distribution_to_string(distribution) +
                   "_misalign" + std::to_string(misalign);
        }
    }
};

inline std::vector<float> generate_test_data(const TestCaseConfig& cfg, int stream_id = 0) {
    std::vector<float> data(cfg.size);
    if (cfg.size == 0) return data;

    std::mt19937 rng(cfg.seed + stream_id * 1013904223u);

    switch (cfg.distribution) {
        case DistributionType::UNIFORM: {
            std::uniform_real_distribution<float> dist(-10.0f, 10.0f);
            for (size_t i = 0; i < cfg.size; ++i) {
                data[i] = dist(rng);
            }
            break;
        }

        case DistributionType::SUBNORMAL: {
            // IEEE 754 float smallest normal is 2^-126 ≈ 1.17549435e-38f
            // Subnormals have exponent 0 and non-zero mantissa (range: ~1.4e-45 to 1.17e-38)
            std::uniform_int_distribution<uint32_t> mant_dist(1, 0x7FFFFFu);
            std::bernoulli_distribution sign_dist(0.5);
            for (size_t i = 0; i < cfg.size; ++i) {
                uint32_t mant = mant_dist(rng);
                uint32_t sign = sign_dist(rng) ? 0x80000000u : 0u;
                data[i] = uint32_as_float(sign | mant);
            }
            break;
        }

        case DistributionType::CANCELLATION: {
            // Generate values near X and -X + delta to trigger catastrophic cancellation
            std::uniform_real_distribution<float> base_dist(100.0f, 10000.0f);
            std::uniform_real_distribution<float> delta_dist(-1e-4f, 1e-4f);
            for (size_t i = 0; i < cfg.size; ++i) {
                if (stream_id == 0) {
                    data[i] = base_dist(rng);
                } else {
                    // Paired value designed to cancel
                    data[i] = -data[i] + delta_dist(rng);
                }
            }
            break;
        }

        case DistributionType::SPECIAL_VALUES: {
            static const float specials[] = {
                0.0f, -0.0f,
                1.0f, -1.0f,
                std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity(),
                std::numeric_limits<float>::quiet_NaN(),
                std::numeric_limits<float>::denorm_min(),
                -std::numeric_limits<float>::denorm_min(),
                std::numeric_limits<float>::min(),
                std::numeric_limits<float>::max(),
                -std::numeric_limits<float>::max()
            };
            const size_t num_specials = sizeof(specials) / sizeof(specials[0]);
            for (size_t i = 0; i < cfg.size; ++i) {
                data[i] = specials[i % num_specials];
            }
            break;
        }

        case DistributionType::POWERS_OF_TWO: {
            std::uniform_int_distribution<int> exp_dist(-30, 30);
            for (size_t i = 0; i < cfg.size; ++i) {
                data[i] = std::ldexp(1.0f, exp_dist(rng));
            }
            break;
        }

        case DistributionType::WIDE_DYNAMIC_RANGE: {
            // Mix of very small (1e-15) and very large (1e15) numbers
            std::uniform_real_distribution<float> u(0.0f, 1.0f);
            for (size_t i = 0; i < cfg.size; ++i) {
                if (i % 2 == 0) {
                    data[i] = 1e-15f * (1.0f + u(rng));
                } else {
                    data[i] = 1e15f * (1.0f + u(rng));
                }
            }
            break;
        }
    }

    return data;
}

// Standard test suite generator
inline std::vector<TestCaseConfig> create_standard_test_suite() {
    std::vector<TestCaseConfig> suite;

    // Boundary sizes
    const std::vector<size_t> sizes = {0, 1, 3, 7, 8, 15, 16, 31, 32, 65, 128, 1024};
    const std::vector<size_t> misalignments = {0, 4}; // Aligned and 4-byte misaligned

    for (size_t sz : sizes) {
        for (size_t misalign : misalignments) {
            suite.emplace_back(sz, DistributionType::UNIFORM, misalign, 42);
        }
    }

    // Edge distributions for representative sizes
    const std::vector<size_t> edge_sizes = {8, 33, 256};
    for (size_t sz : edge_sizes) {
        suite.emplace_back(sz, DistributionType::SUBNORMAL, 0, 101);
        suite.emplace_back(sz, DistributionType::CANCELLATION, 0, 202);
        suite.emplace_back(sz, DistributionType::SPECIAL_VALUES, 0, 303);
        suite.emplace_back(sz, DistributionType::WIDE_DYNAMIC_RANGE, 0, 404);
    }

    return suite;
}

// Quick profile test suite (fast subset)
inline std::vector<TestCaseConfig> create_quick_test_suite() {
    std::vector<TestCaseConfig> suite;
    suite.emplace_back(0, DistributionType::UNIFORM, 0, 42);
    suite.emplace_back(1, DistributionType::UNIFORM, 0, 42);
    suite.emplace_back(7, DistributionType::UNIFORM, 0, 42);
    suite.emplace_back(8, DistributionType::UNIFORM, 0, 42);
    suite.emplace_back(15, DistributionType::UNIFORM, 4, 42); // misaligned tail
    suite.emplace_back(32, DistributionType::UNIFORM, 0, 42);
    suite.emplace_back(33, DistributionType::SUBNORMAL, 0, 101);
    suite.emplace_back(128, DistributionType::CANCELLATION, 0, 202);
    return suite;
}

} // namespace kernelguard
