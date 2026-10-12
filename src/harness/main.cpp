#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <iomanip>
#include <cstring>
#include "kernelguard/harness.h"

namespace kernelguard {
    void register_vec_add_kernels();
    void register_saxpy_kernels();
    void register_dot_kernels();
    void register_seeded_bug_kernels();
}

using namespace kernelguard;

void print_usage(const char* prog) {
    std::cout << "KernelGuard C++ Verification Harness\n"
              << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  --list                List all registered kernels\n"
              << "  --kernel <name>       Run test suite against specific kernel\n"
              << "  --all                 Run test suite on all non-bug kernels\n"
              << "  --seeded-bugs         Run detection suite on seeded bug kernels\n"
              << "  --profile <quick|full> Test profile (default: quick)\n"
              << "  --json-out <path>     Write machine-readable JSON results\n"
              << "  --no-isolate          Run in-process without fork isolation\n"
              << "  --help                Display this help message\n";
}

int main(int argc, char** argv) {
    // Register all built-in kernels
    register_vec_add_kernels();
    register_saxpy_kernels();
    register_dot_kernels();
    register_seeded_bug_kernels();

    std::string target_kernel = "";
    bool run_all = false;
    bool run_bugs = false;
    bool list_kernels = false;
    std::string profile = "quick";
    std::string json_out_path = "";
    bool isolate = true;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--list") {
            list_kernels = true;
        } else if (arg == "--kernel" && i + 1 < argc) {
            target_kernel = argv[++i];
        } else if (arg == "--all") {
            run_all = true;
        } else if (arg == "--seeded-bugs") {
            run_bugs = true;
        } else if (arg == "--profile" && i + 1 < argc) {
            profile = argv[++i];
        } else if (arg == "--json-out" && i + 1 < argc) {
            json_out_path = argv[++i];
        } else if (arg == "--no-isolate") {
            isolate = false;
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
    }

    auto& reg = KernelRegistry::instance();

    if (list_kernels) {
        std::cout << "Registered Kernels:\n";
        for (const auto& kv : reg.get_all()) {
            std::cout << "  - " << std::left << std::setw(28) << kv.first
                      << " [" << kernel_type_to_string(kv.second.type) << "] "
                      << (kv.second.is_seeded_bug ? "(SEEDED BUG)" : "(CLEAN)")
                      << " - " << kv.second.description << "\n";
        }
        return 0;
    }

    if (target_kernel.empty() && !run_all && !run_bugs) {
        print_usage(argv[0]);
        return 1;
    }

    std::vector<TestCaseConfig> test_suite;
    if (profile == "full") {
        test_suite = create_standard_test_suite();
    } else {
        test_suite = create_quick_test_suite();
    }

    std::vector<std::string> kernels_to_run;
    if (!target_kernel.empty()) {
        if (!reg.has_kernel(target_kernel)) {
            std::cerr << "Error: Unknown kernel '" << target_kernel << "'\n";
            return 3;
        }
        kernels_to_run.push_back(target_kernel);
    } else if (run_all) {
        for (const auto& kv : reg.get_all()) {
            if (!kv.second.is_seeded_bug) {
                kernels_to_run.push_back(kv.first);
            }
        }
    } else if (run_bugs) {
        for (const auto& kv : reg.get_all()) {
            if (kv.second.is_seeded_bug) {
                kernels_to_run.push_back(kv.first);
            }
        }
    }

    std::cout << "=========================================================\n"
              << "KernelGuard Verification Run (" << profile << " profile, "
              << test_suite.size() << " cases per kernel)\n"
              << "=========================================================\n";

    std::vector<KernelTestSummary> summaries;
    size_t total_passed_kernels = 0;
    size_t total_failed_kernels = 0;

    for (const auto& kname : kernels_to_run) {
        std::cout << "Testing kernel: " << std::left << std::setw(28) << kname << " ... " << std::flush;
        KernelTestSummary sum = run_kernel_tests(kname, test_suite, isolate);
        summaries.push_back(sum);

        if (sum.failed_cases == 0) {
            std::cout << "PASSED (" << sum.passed_cases << "/" << sum.total_cases
                      << " in " << std::fixed << std::setprecision(1) << sum.total_time_ms << "ms)\n";
            total_passed_kernels++;
        } else {
            std::cout << "FAILED (" << sum.failed_cases << "/" << sum.total_cases
                      << " failed, worst verdict: " << verdict_to_string(sum.worst_verdict.type) << ")\n";
            for (const auto& det : sum.failure_details) {
                std::cout << "    * " << det << "\n";
            }
            total_failed_kernels++;
        }
    }

    std::cout << "=========================================================\n";
    if (run_bugs) {
        double detection_rate = (kernels_to_run.empty()) ? 0.0 :
            (static_cast<double>(total_failed_kernels) / kernels_to_run.size()) * 100.0;
        std::cout << "Seeded Bug Detection Summary:\n"
                  << "  Injected Bugs:  " << kernels_to_run.size() << "\n"
                  << "  Detected Bugs:  " << total_failed_kernels << "\n"
                  << "  Missed Bugs:    " << total_passed_kernels << "\n"
                  << "  Detection Rate: " << std::fixed << std::setprecision(1) << detection_rate << "%\n";
    } else {
        std::cout << "Run Summary: " << total_passed_kernels << " passed, "
                  << total_failed_kernels << " failed out of " << kernels_to_run.size() << " kernels.\n";
    }
    std::cout << "=========================================================\n";

    // Write JSON output if requested
    if (!json_out_path.empty()) {
        std::ofstream jf(json_out_path);
        jf << "{\n"
           << "  \"profile\": \"" << profile << "\",\n"
           << "  \"total_kernels\": " << kernels_to_run.size() << ",\n"
           << "  \"passed_kernels\": " << total_passed_kernels << ",\n"
           << "  \"failed_kernels\": " << total_failed_kernels << ",\n"
           << "  \"results\": [\n";

        for (size_t i = 0; i < summaries.size(); ++i) {
            const auto& s = summaries[i];
            jf << "    {\n"
               << "      \"name\": \"" << s.kernel_name << "\",\n"
               << "      \"type\": \"" << kernel_type_to_string(s.kernel_type) << "\",\n"
               << "      \"total_cases\": " << s.total_cases << ",\n"
               << "      \"passed_cases\": " << s.passed_cases << ",\n"
               << "      \"failed_cases\": " << s.failed_cases << ",\n"
               << "      \"status\": \"" << (s.failed_cases == 0 ? "PASS" : "FAIL") << "\",\n"
               << "      \"time_ms\": " << s.total_time_ms << ",\n"
               << "      \"worst_verdict\": \"" << verdict_to_string(s.worst_verdict.type) << "\"\n"
               << "    }" << (i + 1 < summaries.size() ? "," : "") << "\n";
        }
        jf << "  ]\n}\n";
        std::cout << "Results written to: " << json_out_path << "\n";
    }

    if (run_bugs) {
        // When running seeded bugs, all bugs detected is success (exit code 0)
        return (total_passed_kernels == 0) ? 0 : 1;
    } else {
        return (total_failed_kernels == 0) ? 0 : 1;
    }
}
