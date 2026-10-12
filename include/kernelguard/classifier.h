#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "double_double.h"

namespace kernelguard {

constexpr uint32_t UNWRITTEN_FILL_PATTERN = 0x7FA55A7Fu; // Signaling/distinct float pattern

enum class VerdictType {
    PASS,
    MISMATCH,
    DENORMAL_FTZ,
    FMA_CONTRACTION,
    OVERFLOW_INF,
    NAN_MISMATCH,
    SIGNED_ZERO,
    CANARY_CORRUPTION,
    GUARD_PAGE_FAULT,
    UNWRITTEN_ELEMENTS,
    TIMEOUT,
    CRASH,
    SKIPPED
};

inline std::string verdict_to_string(VerdictType v) {
    switch (v) {
        case VerdictType::PASS: return "PASS";
        case VerdictType::MISMATCH: return "MISMATCH";
        case VerdictType::DENORMAL_FTZ: return "DENORMAL_FTZ";
        case VerdictType::FMA_CONTRACTION: return "FMA_CONTRACTION";
        case VerdictType::OVERFLOW_INF: return "OVERFLOW_INF";
        case VerdictType::NAN_MISMATCH: return "NAN_MISMATCH";
        case VerdictType::SIGNED_ZERO: return "SIGNED_ZERO";
        case VerdictType::CANARY_CORRUPTION: return "CANARY_CORRUPTION";
        case VerdictType::GUARD_PAGE_FAULT: return "GUARD_PAGE_FAULT";
        case VerdictType::UNWRITTEN_ELEMENTS: return "UNWRITTEN_ELEMENTS";
        case VerdictType::TIMEOUT: return "TIMEOUT";
        case VerdictType::CRASH: return "CRASH";
        case VerdictType::SKIPPED: return "SKIPPED";
    }
    return "UNKNOWN";
}

enum class ContractKind {
    EXACT,
    ULP,
    BOUND
};

struct Contract {
    ContractKind kind;
    int64_t max_ulp;
    double max_rel_bound;
    double max_abs_bound;

    static Contract exact() {
        return Contract{ContractKind::EXACT, 0, 0.0, 0.0};
    }

    static Contract ulp(int64_t k) {
        return Contract{ContractKind::ULP, k, 0.0, 0.0};
    }

    static Contract bound(double rel_bound, double abs_bound = 1e-6) {
        return Contract{ContractKind::BOUND, 0, rel_bound, abs_bound};
    }
};

struct CaseVerdict {
    VerdictType type;
    std::string details;
    size_t failing_index;
    float actual_value;
    float expected_value;
    int64_t observed_ulp;
    double max_abs_error;
    double max_rel_error;

    CaseVerdict()
        : type(VerdictType::PASS), details(""), failing_index(0),
          actual_value(0.0f), expected_value(0.0f), observed_ulp(0),
          max_abs_error(0.0), max_rel_error(0.0) {}

    static CaseVerdict pass() {
        return CaseVerdict();
    }

    static CaseVerdict fail(VerdictType t, const std::string& msg, size_t idx = 0,
                            float act = 0.0f, float exp = 0.0f, int64_t ulp = 0) {
        CaseVerdict v;
        v.type = t;
        v.details = msg;
        v.failing_index = idx;
        v.actual_value = act;
        v.expected_value = exp;
        v.observed_ulp = ulp;
        v.max_abs_error = std::abs(act - exp);
        double denom = std::max(std::abs(static_cast<double>(exp)), 1e-12);
        v.max_rel_error = v.max_abs_error / denom;
        return v;
    }
};

// Check outputs according to contract and classify failure cause
inline CaseVerdict evaluate_contract(
    const float* actual,
    const float* reference,
    size_t size,
    const Contract& contract,
    const float* ftz_reference = nullptr,
    const float* fma_reference = nullptr
) {
    int64_t max_observed_ulp = 0;
    double max_observed_abs_err = 0.0;

    for (size_t i = 0; i < size; ++i) {
        float act = actual[i];
        float exp = reference[i];

        // Check unwritten canary pattern
        if (float_as_uint32(act) == UNWRITTEN_FILL_PATTERN) {
            return CaseVerdict::fail(VerdictType::UNWRITTEN_ELEMENTS,
                "Output buffer element was never written by the kernel", i, act, exp, 0);
        }

        // NaN checking
        if (std::isnan(exp)) {
            if (!std::isnan(act)) {
                return CaseVerdict::fail(VerdictType::NAN_MISMATCH,
                    "Expected NaN from reference, but got finite value", i, act, exp, INT64_MAX);
            }
            continue;
        } else if (std::isnan(act)) {
            return CaseVerdict::fail(VerdictType::NAN_MISMATCH,
                "Got unexpected NaN from kernel output", i, act, exp, INT64_MAX);
        }

        // Infinity checking
        if (std::isinf(exp)) {
            if (act != exp) {
                return CaseVerdict::fail(VerdictType::OVERFLOW_INF,
                    "Infinity mismatch (expected inf)", i, act, exp, INT64_MAX);
            }
            continue;
        }

        // Signed zero check
        if (act == 0.0f && exp == 0.0f) {
            uint32_t act_u = float_as_uint32(act);
            uint32_t exp_u = float_as_uint32(exp);
            if (contract.kind == ContractKind::EXACT && act_u != exp_u) {
                return CaseVerdict::fail(VerdictType::SIGNED_ZERO,
                    "Signed zero mismatch (+0.0 vs -0.0)", i, act, exp, 1);
            }
            continue;
        }

        int64_t ulp_dist = compute_float_ulp_distance(act, exp);
        if (ulp_dist > max_observed_ulp) max_observed_ulp = ulp_dist;

        double abs_err = std::abs(static_cast<double>(act) - static_cast<double>(exp));
        if (abs_err > max_observed_abs_err) max_observed_abs_err = abs_err;

        bool ok = false;
        switch (contract.kind) {
            case ContractKind::EXACT:
                ok = (float_as_uint32(act) == float_as_uint32(exp));
                break;
            case ContractKind::ULP:
                ok = (ulp_dist <= contract.max_ulp);
                break;
            case ContractKind::BOUND: {
                double rel_denom = std::max(std::abs(static_cast<double>(exp)), 1e-9);
                double rel_err = abs_err / rel_denom;
                ok = (rel_err <= contract.max_rel_bound || abs_err <= contract.max_abs_bound);
                break;
            }
        }

        if (!ok) {
            // Classify cause using ablation/secondary references
            // 1. FTZ ablation check
            if (ftz_reference) {
                int64_t ftz_ulp = compute_float_ulp_distance(act, ftz_reference[i]);
                if (ftz_ulp <= (contract.kind == ContractKind::ULP ? contract.max_ulp : 1)) {
                    return CaseVerdict::fail(VerdictType::DENORMAL_FTZ,
                        "Mismatch caused by Flush-To-Zero / Denormals-Are-Zero behavior",
                        i, act, exp, ulp_dist);
                }
            } else if (is_subnormal_float(exp) || is_subnormal_float(act)) {
                float ftz_exp = flush_subnormal_to_zero(exp);
                if (compute_float_ulp_distance(act, ftz_exp) <= 1) {
                    return CaseVerdict::fail(VerdictType::DENORMAL_FTZ,
                        "Subnormal input or output flushed to zero by hardware/compiler",
                        i, act, exp, ulp_dist);
                }
            }

            // 2. FMA Contraction check
            if (fma_reference) {
                int64_t fma_ulp = compute_float_ulp_distance(act, fma_reference[i]);
                if (fma_ulp <= (contract.kind == ContractKind::ULP ? contract.max_ulp : 1)) {
                    return CaseVerdict::fail(VerdictType::FMA_CONTRACTION,
                        "Mismatch caused by FMA contraction (compiler fused mul+add)",
                        i, act, exp, ulp_dist);
                }
            }

            // Default to numerical mismatch
            return CaseVerdict::fail(VerdictType::MISMATCH,
                "Numerical deviation exceeded contract threshold", i, act, exp, ulp_dist);
        }
    }

    CaseVerdict v = CaseVerdict::pass();
    v.observed_ulp = max_observed_ulp;
    v.max_abs_error = max_observed_abs_err;
    return v;
}

} // namespace kernelguard
