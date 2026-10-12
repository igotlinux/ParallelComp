#pragma once

#include <string>
#include <vector>
#include <functional>
#include <map>
#include <memory>
#include <iostream>
#include <fstream>
#include <chrono>

#include "double_double.h"
#include "memory_guard.h"
#include "classifier.h"
#include "test_case.h"
#include "isolation.h"

namespace kernelguard {

// Kernel signature types
using VecAddKernelFn = void(*)(const float*, const float*, float*, size_t);
using SaxpyKernelFn  = void(*)(float, const float*, const float*, float*, size_t);
using DotKernelFn    = float(*)(const float*, const float*, size_t);

enum class KernelType {
    VEC_ADD,
    SAXPY,
    DOT_PRODUCT
};

inline std::string kernel_type_to_string(KernelType t) {
    switch (t) {
        case KernelType::VEC_ADD: return "vec_add";
        case KernelType::SAXPY: return "saxpy";
        case KernelType::DOT_PRODUCT: return "dot_product";
    }
    return "unknown";
}

// Canonical High-Precision References computed with DoubleDouble
inline void ref_vec_add_dd(const float* a, const float* b, float* c, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        DoubleDouble da = DoubleDouble::from_float(a[i]);
        DoubleDouble db = DoubleDouble::from_float(b[i]);
        DoubleDouble res = dd_add(da, db);
        c[i] = res.to_float();
    }
}

inline void ref_saxpy_dd(float alpha, const float* x, const float* y, float* out, size_t n) {
    DoubleDouble da = DoubleDouble::from_float(alpha);
    for (size_t i = 0; i < n; ++i) {
        DoubleDouble dx = DoubleDouble::from_float(x[i]);
        DoubleDouble dy = DoubleDouble::from_float(y[i]);
        DoubleDouble prod = dd_mul(da, dx);
        DoubleDouble res = dd_add(prod, dy);
        out[i] = res.to_float();
    }
}

inline float ref_dot_dd(const float* x, const float* y, size_t n) {
    DoubleDouble accum(0.0);
    for (size_t i = 0; i < n; ++i) {
        DoubleDouble dx = DoubleDouble::from_float(x[i]);
        DoubleDouble dy = DoubleDouble::from_float(y[i]);
        DoubleDouble prod = dd_mul(dx, dy);
        accum = dd_add(accum, prod);
    }
    return accum.to_float();
}

// FTZ reference variants (subnormals flushed to zero)
inline void ref_vec_add_ftz(const float* a, const float* b, float* c, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        float fa = flush_subnormal_to_zero(a[i]);
        float fb = flush_subnormal_to_zero(b[i]);
        c[i] = flush_subnormal_to_zero(fa + fb);
    }
}

inline void ref_saxpy_ftz(float alpha, const float* x, const float* y, float* out, size_t n) {
    float fa = flush_subnormal_to_zero(alpha);
    for (size_t i = 0; i < n; ++i) {
        float fx = flush_subnormal_to_zero(x[i]);
        float fy = flush_subnormal_to_zero(y[i]);
        out[i] = flush_subnormal_to_zero(fa * fx + fy);
    }
}

// Kernel Registry entry
struct KernelEntry {
    std::string name;
    KernelType type;
    Contract contract;
    void* function_ptr;
    std::string description;
    std::string isa_target;
    bool is_seeded_bug;
};

class KernelRegistry {
public:
    static KernelRegistry& instance() {
        static KernelRegistry reg;
        return reg;
    }

    void register_vec_add(const std::string& name, VecAddKernelFn fn, Contract contract,
                          const std::string& desc = "", const std::string& isa = "scalar",
                          bool is_bug = false) {
        KernelEntry entry{name, KernelType::VEC_ADD, contract,
                          reinterpret_cast<void*>(fn), desc, isa, is_bug};
        kernels_[name] = entry;
    }

    void register_saxpy(const std::string& name, SaxpyKernelFn fn, Contract contract,
                        const std::string& desc = "", const std::string& isa = "scalar",
                        bool is_bug = false) {
        KernelEntry entry{name, KernelType::SAXPY, contract,
                          reinterpret_cast<void*>(fn), desc, isa, is_bug};
        kernels_[name] = entry;
    }

    void register_dot(const std::string& name, DotKernelFn fn, Contract contract,
                      const std::string& desc = "", const std::string& isa = "scalar",
                      bool is_bug = false) {
        KernelEntry entry{name, KernelType::DOT_PRODUCT, contract,
                          reinterpret_cast<void*>(fn), desc, isa, is_bug};
        kernels_[name] = entry;
    }

    const std::map<std::string, KernelEntry>& get_all() const {
        return kernels_;
    }

    bool has_kernel(const std::string& name) const {
        return kernels_.find(name) != kernels_.end();
    }

    const KernelEntry& get(const std::string& name) const {
        return kernels_.at(name);
    }

private:
    KernelRegistry() = default;
    std::map<std::string, KernelEntry> kernels_;
};

// Test Runner for an individual kernel
struct KernelTestSummary {
    std::string kernel_name;
    KernelType kernel_type;
    size_t total_cases;
    size_t passed_cases;
    size_t failed_cases;
    std::vector<std::string> failure_details;
    double total_time_ms;
    CaseVerdict worst_verdict;
};

inline KernelTestSummary run_kernel_tests(
    const std::string& kernel_name,
    const std::vector<TestCaseConfig>& test_cases,
    bool isolate = true,
    int timeout_sec = 2
) {
    auto& reg = KernelRegistry::instance();
    const auto& entry = reg.get(kernel_name);

    KernelTestSummary summary;
    summary.kernel_name = kernel_name;
    summary.kernel_type = entry.type;
    summary.total_cases = test_cases.size();
    summary.passed_cases = 0;
    summary.failed_cases = 0;
    summary.total_time_ms = 0.0;
    summary.worst_verdict = CaseVerdict::pass();

    for (const auto& cfg : test_cases) {
        auto execute_single_case = [&]() -> CaseVerdict {
            if (entry.type == KernelType::VEC_ADD) {
                auto a_data = generate_test_data(cfg, 0);
                auto b_data = generate_test_data(cfg, 1);

                GuardedBuffer buf_a = GuardedBuffer::allocate(cfg.size * sizeof(float), 32, cfg.misalign_bytes);
                GuardedBuffer buf_b = GuardedBuffer::allocate(cfg.size * sizeof(float), 32, cfg.misalign_bytes);
                GuardedBuffer buf_c = GuardedBuffer::allocate(cfg.size * sizeof(float), 32, cfg.misalign_bytes);

                if (cfg.size > 0) {
                    std::memcpy(buf_a.as<float>(), a_data.data(), cfg.size * sizeof(float));
                    std::memcpy(buf_b.as<float>(), b_data.data(), cfg.size * sizeof(float));
                }
                buf_c.prefill_canary_pattern();

                // Compute high-precision reference
                std::vector<float> ref_c(cfg.size);
                ref_vec_add_dd(a_data.data(), b_data.data(), ref_c.data(), cfg.size);

                std::vector<float> ftz_c(cfg.size);
                ref_vec_add_ftz(a_data.data(), b_data.data(), ftz_c.data(), cfg.size);

                // Run kernel
                auto fn = reinterpret_cast<VecAddKernelFn>(entry.function_ptr);
                fn(buf_a.as<float>(), buf_b.as<float>(), buf_c.as<float>(), cfg.size);

                // Check canaries first
                std::string canary_err;
                if (!buf_c.verify_canaries(canary_err) ||
                    !buf_a.verify_canaries(canary_err) ||
                    !buf_b.verify_canaries(canary_err)) {
                    return CaseVerdict::fail(VerdictType::CANARY_CORRUPTION, canary_err);
                }

                return evaluate_contract(buf_c.as<float>(), ref_c.data(), cfg.size,
                                         entry.contract, ftz_c.data());
            }
            else if (entry.type == KernelType::SAXPY) {
                float alpha = 2.5f;
                auto x_data = generate_test_data(cfg, 0);
                auto y_data = generate_test_data(cfg, 1);

                GuardedBuffer buf_x = GuardedBuffer::allocate(cfg.size * sizeof(float), 32, cfg.misalign_bytes);
                GuardedBuffer buf_y = GuardedBuffer::allocate(cfg.size * sizeof(float), 32, cfg.misalign_bytes);
                GuardedBuffer buf_out = GuardedBuffer::allocate(cfg.size * sizeof(float), 32, cfg.misalign_bytes);

                if (cfg.size > 0) {
                    std::memcpy(buf_x.as<float>(), x_data.data(), cfg.size * sizeof(float));
                    std::memcpy(buf_y.as<float>(), y_data.data(), cfg.size * sizeof(float));
                }
                buf_out.prefill_canary_pattern();

                std::vector<float> ref_out(cfg.size);
                ref_saxpy_dd(alpha, x_data.data(), y_data.data(), ref_out.data(), cfg.size);

                std::vector<float> ftz_out(cfg.size);
                ref_saxpy_ftz(alpha, x_data.data(), y_data.data(), ftz_out.data(), cfg.size);

                auto fn = reinterpret_cast<SaxpyKernelFn>(entry.function_ptr);
                fn(alpha, buf_x.as<float>(), buf_y.as<float>(), buf_out.as<float>(), cfg.size);

                std::string canary_err;
                if (!buf_out.verify_canaries(canary_err)) {
                    return CaseVerdict::fail(VerdictType::CANARY_CORRUPTION, canary_err);
                }

                return evaluate_contract(buf_out.as<float>(), ref_out.data(), cfg.size,
                                         entry.contract, ftz_out.data());
            }
            else if (entry.type == KernelType::DOT_PRODUCT) {
                auto x_data = generate_test_data(cfg, 0);
                auto y_data = generate_test_data(cfg, 1);

                GuardedBuffer buf_x = GuardedBuffer::allocate(cfg.size * sizeof(float), 32, cfg.misalign_bytes);
                GuardedBuffer buf_y = GuardedBuffer::allocate(cfg.size * sizeof(float), 32, cfg.misalign_bytes);

                if (cfg.size > 0) {
                    std::memcpy(buf_x.as<float>(), x_data.data(), cfg.size * sizeof(float));
                    std::memcpy(buf_y.as<float>(), y_data.data(), cfg.size * sizeof(float));
                }

                float ref_val = ref_dot_dd(x_data.data(), y_data.data(), cfg.size);

                auto fn = reinterpret_cast<DotKernelFn>(entry.function_ptr);
                float act_val = fn(buf_x.as<float>(), buf_y.as<float>(), cfg.size);

                std::string canary_err;
                if (!buf_x.verify_canaries(canary_err) || !buf_y.verify_canaries(canary_err)) {
                    return CaseVerdict::fail(VerdictType::CANARY_CORRUPTION, canary_err);
                }

                return evaluate_contract(&act_val, &ref_val, 1, entry.contract);
            }

            return CaseVerdict::pass();
        };

        ExecutionResult exec_res;
        if (isolate) {
            exec_res = run_isolated_case(execute_single_case, timeout_sec);
        } else {
            auto t0 = std::chrono::high_resolution_clock::now();
            exec_res.verdict = execute_single_case();
            auto t1 = std::chrono::high_resolution_clock::now();
            exec_res.wall_time_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        }

        summary.total_time_ms += exec_res.wall_time_ms;

        if (exec_res.verdict.type == VerdictType::PASS) {
            summary.passed_cases++;
        } else {
            summary.failed_cases++;
            summary.worst_verdict = exec_res.verdict;
            std::string msg = cfg.name + ": " + verdict_to_string(exec_res.verdict.type) +
                              " - " + exec_res.verdict.details;
            summary.failure_details.push_back(msg);
        }
    }

    return summary;
}

} // namespace kernelguard
