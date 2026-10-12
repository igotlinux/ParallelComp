# KernelGuard 🛡️
### Production-Quality Compiler Verification & Numerical Correctness Framework
[![Verification CI](https://github.com/igotlinux/kernal-guard/actions/workflows/ci.yml/badge.svg)](https://github.com/igotlinux/kernal-guard/actions/workflows/ci.yml)
[![GitHub Pages](https://img.shields.io/badge/docs-website-brightgreen.svg)](https://igotlinux.github.io/kernal-guard/)
[![License: Apache-2.0](https://img.shields.io/badge/License-Apache--2.0-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![Python 3.10+](https://img.shields.io/badge/python-3.10+-blue.svg)](https://www.python.org/)
[![CUDA 12](https://img.shields.io/badge/CUDA-12.x-green.svg)](https://developer.nvidia.com/cuda-zone)

**KernelGuard** is a commercial-grade automated compiler verification and regression gatekeeper framework for C/C++ and NVIDIA CUDA/PTX. It gates pull requests in real CI/CD pipelines to guarantee that refactors, compiler updates, and flag changes **never silently break mathematical precision or kill SIMD/GPU vectorization**.

🌐 **Landing Page & Pricing:** [https://igotlinux.github.io/kernal-guard/](https://igotlinux.github.io/kernal-guard/)

---

## 💎 Commercial Plans & Pricing

KernelGuard is dual-licensed: free and open-source for community and academic use, with commercial tiers for production engineering teams. Full details and interactive ROI calculator: [https://igotlinux.github.io/kernal-guard/#pricing](https://igotlinux.github.io/kernal-guard/#pricing).

| Plan | Pricing | Target Audience | Key Inclusions |
| :--- | :--- | :--- | :--- |
| **Community** | **$0** / forever | Open-source repos, researchers, students | Double-double oracle, local CLI (`kg`), GCC/Clang matrix, HTML/JSON/JUnit reports |
| **Pro / Team** | **$99** / seat / mo | ML inference teams, DSP/audio engines, game studios | Automated PR gatekeeper, Delta Debugging minimizer (`ddmin`), Standalone `repro.cpp` generator, Vectorization remark tracking, SARIF GitHub code scanning, Lockfile regression gates |
| **Enterprise** | **$499** / repo / mo | HPC organizations, GPU cloud providers, defense/finance | Live NVIDIA GPU hardware runner farms, automated PTX assembly assertions, pinned toolchain Docker digests, custom microarchitecture contracts (AVX-512, SVE2), dedicated SLA & compiler consulting |

### Why Teams Pay for KernelGuard
- **Prevent Multi-Million Dollar GPU Regressions:** A silent vectorization loss or subnormal FTZ rounding bug in an ML kernel wastes hundreds of GPU cluster training hours.
- **Instant Root Cause with Standalone `repro.cpp`:** Instead of debugging a 50,000-line codebase, KernelGuard's Delta Debugger extracts the failing input array down to 1 minimal case and bundles a standalone 50-line C++ file ready for the LLVM/GCC/NVCC bug tracker.
- **Deterministic CI Signals:** Unlike flaky benchmark timings that jitter in cloud virtual machines, KernelGuard gates on deterministic contracts: ULP bounds, compiler remarks, disassembly SIMD opcodes, and Callgrind instruction counts.

---

## 🌟 Key Capabilities

### 1. High-Precision Numerical Oracle (~106-bit Double-Double)
- Evaluates outputs against an exact reference computed using Knuth's **2Sum** and Dekker's **2Prod** error-free transformations.
- Provides ~106 bits of significand precision, functioning as an exact oracle for IEEE 754 single-precision (`float32`) without external library dependencies.
- Supports configurable error contracts:
  - `exact`: Bit-exact equality.
  - `ulp(k)`: Bounded Units in the Last Place distance ($k$ ULPs).
  - `bound`: Documented absolute and relative error bounds for reductions.
- Explicit failure classification: distinguishes genuine bugs from **Flush-To-Zero (FTZ)**, **FMA contraction**, **cancellation**, or **signed zero** differences.

### 2. Guard-Page & Canary Buffer Protection
- Isolates memory regions using OS virtual memory pages (`mprotect` with `PROT_NONE`) bookending the allocation.
- Embeds 64-byte head and tail canaries (`0xDEADBEEF`, `0xCAFEBABE`) to detect sub-page buffer overflows and unaligned overruns immediately.
- Pre-fills output buffers with `0x7FA55A7F` to flag unwritten elements caused by omitted tail loops.

### 3. Fork-per-Case Isolation & Crash Recovery
- Executes each test case in a separate forked child process with POSIX `alarm()` timeouts.
- Intercepts signals (`SIGSEGV`, `SIGBUS`, `SIGFPE`, `SIGABRT`) and sanitizer panics, cleanly attributing failures to individual kernels without halting the test matrix.

### 4. Compiler Remark & Disassembly SIMD Analysis
- Discovers GCC (`-fopt-info-vec-optimized`, `-fopt-info-vec-missed`) and Clang (`-Rpass=loop-vectorize`) optimization remarks.
- Disassembles compiled object binaries to extract and count packed SIMD instructions (NEON `fadd.4s`, `fmla.4s` on ARM64; AVX2 `vaddps`, `vfmadd` on x86_64).

### 5. NVIDIA CUDA & PTX Inspection Backend
- Compiles CUDA source to PTX assembly (`nvcc -ptx`) and parses directives, function entries, and register allocations.
- Validates instruction pattern assertions (e.g. `fma.rn.f32`, `st.global`, `red.relaxed`) and builds instruction histograms.
- Gracefully skips CUDA execution when NVIDIA hardware or `nvcc` is absent—**without falsifying test passes**.

### 6. Delta Debugging (ddmin) & Standalone Reproducer Bundles
- When a failure is detected, an automated Delta Debugging algorithm minimizes the failing input array to its 1-minimal failing subset.
- Generates a standalone, zero-dependency reproducer bundle in `repro_bundles/<failure-id>/` containing:
  - `repro.cpp`: Standalone executable reproduction code.
  - `build.sh`: Exact compiler command and flags.
  - `metadata.json`: Failure classification, ULP distance, and toolchain versions.
  - `README.md`: Concise reproduction guide.

### 7. Seeded Bug Detection Suite (100% Detection Rate)
Includes 7 distinct, intentionally faulty CPU kernels and 2 CUDA bug kernels:
- Missing tail handling
- Off-by-one indexing writing past buffer
- Unaligned pointer crash
- Signed overflow undefined behavior
- Incorrect reduction identity accumulator
- FMA contraction numerical divergence
- Subnormal flush-to-zero discrepancy
- CUDA bounds omission & launch dimension truncation

---

## 🚀 Cross-Platform Support

KernelGuard is battle-tested across operating systems and microarchitectures:

| Platform | Microarchitecture | Compilers Supported | Verification Mode |
| :--- | :--- | :--- | :--- |
| **Linux (Ubuntu, Debian, RHEL, Arch)** | x86_64 (AVX2, AVX-512) | GCC 9–14, Clang 12–19 | Native execution, Callgrind, Valgrind |
| **macOS (Sonoma, Sequoia)** | Apple Silicon (ARM64 NEON) | Apple Clang, Homebrew GCC | Native NEON execution, remark tracking |
| **Linux + NVIDIA CUDA** | NVIDIA GPU (Ampere, Hopper, Blackwell) | NVCC 11.x, 12.x, Clang-CUDA | Real GPU device execution + offline PTX inspection |

---

## 🔄 Real CI/CD Pipeline Integration

### 1. Reusable GitHub Action (One-Line Setup)
Add KernelGuard to any GitHub repository in `.github/workflows/kernelguard.yml`:

```yaml
name: KernelGuard Compiler Gatekeeper

on:
  pull_request:
    branches: [ main ]
  push:
    branches: [ main ]

jobs:
  verify:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Run KernelGuard Verification
        uses: igotlinux/kernal-guard@v1
        with:
          profile: 'quick'          # 'quick' for PRs, 'full' for nightlies
          config: 'kernelguard.yaml'
          fail-on-regression: 'true'

      # Upload SARIF results to GitHub Security tab (Pro/Enterprise)
      - name: Upload SARIF Security Scan
        if: always()
        uses: github/codeql-action/upload-sarif@v3
        with:
          sarif_file: 'reports/kernelguard_results.sarif'
```

### 2. Docker / GitLab CI / Jenkins
KernelGuard ships with a reproducible Linux container image:

```bash
# Pull or build container
docker build -t kernelguard .

# Run verification in your CI runner
docker run --rm -v $(pwd):/workspace kernelguard kg run --profile quick
```

---

## 💻 Quick Start & Local Usage

### Installation
```bash
# Clone the repository
git clone https://github.com/igotlinux/kernal-guard.git
cd kernal-guard

# Install Python package
pip install .

# Build C++ harness
cmake -B build
cmake --build build
```

### Command-Line Interface (`kg`)

```bash
# Run quick verification matrix (GCC + Clang)
kg run --profile quick

# Run full nightly matrix with all boundary distributions
kg run --profile full

# Run CPU-only verification
kg run --backend cpu

# Run CUDA/PTX backend (runs live kernels on GPU or PTX parser offline)
kg run --backend cuda

# Inspect seeded bug detection suite
./build/kg_harness --seeded-bugs --profile quick

# View latest summary report
kg report

# Update baseline lockfile for pull request gating
kg baseline update

# Open interactive HTML dashboard
open reports/report.html
```

---

## 📈 Seeded Bug Detection Verification

| Bug Identifier | Seeded Defect | Detection Mechanism | Status |
| :--- | :--- | :--- | :---: |
| `bug_tail_missing` | Omitted vector loop tail | `UNWRITTEN_ELEMENTS` canary detection | **DETECTED** |
| `bug_off_by_one` | `i <= n` loop condition | `CANARY_CORRUPTION` (tail canary overrun) | **DETECTED** |
| `bug_misaligned` | Requires 32-byte alignment | `GUARD_PAGE_FAULT` (memory fault caught) | **DETECTED** |
| `bug_signed_overflow` | Signed index calculation UB | Numerical `MISMATCH` | **DETECTED** |
| `bug_reduction_identity` | Accumulator initialized to 1.0f | Numerical `MISMATCH` | **DETECTED** |
| `bug_fma_divergence` | Evaluates perturbed order | Numerical `MISMATCH` / `DENORMAL_FTZ` | **DETECTED** |
| `bug_denormal_ftz` | Flushes subnormals to zero | `DENORMAL_FTZ` ablation classification | **DETECTED** |

**Actual Measured Detection Rate: 100.0% (7/7 CPU seeded bugs detected, 0 missed).**

---

## 📁 Repository Structure

```
kernal-guard/
├── .github/workflows/          # Production CI/CD pipelines
│   ├── ci.yml                  # Cross-platform matrix (Linux GCC/Clang, macOS ARM64, CUDA)
│   ├── cuda-ci.yml             # Dedicated CUDA/PTX container test
│   └── pages.yml               # Automated GitHub Pages marketing deployment
├── action.yml                  # Reusable composite GitHub Action for downstream repos
├── CMakeLists.txt              # C++ build & installation rules
├── Dockerfile                  # Production Ubuntu 22.04 container image
├── docs/                       # Commercial landing page & engineering decision logs
│   ├── index.html              # Marketing website with pricing, calculator & demo
│   └── decisions.md            # Architectural decision records
├── kernelguard.yaml            # Toolchain, kernel, and matrix configuration
├── kernelguard_lock.json       # Baseline regression lockfile
├── kg                          # Root CLI executable wrapper
├── include/kernelguard/        # Core C++ headers
│   ├── double_double.h         # ~106-bit Knuth 2Sum & Dekker 2Prod arithmetic oracle
│   ├── memory_guard.h          # PROT_NONE guard pages & 64-byte canaries
│   ├── isolation.h             # Fork-per-case process isolation & signal handling
│   ├── classifier.h            # Contract evaluation & failure diagnostics
│   ├── test_case.h             # 20 vector-width boundaries × 6 distributions
│   └── harness.h               # Kernel registry & harness API
├── src/                        # Kernel implementations & test harness
│   ├── harness/main.cpp        # Standalone CPU harness binary
│   ├── kernels/cpu/            # Clean kernels (vec_add, saxpy, dot_product)
│   ├── kernels/cuda/           # CUDA kernels & cuda_harness.cu
│   ├── kernels/seeded_bugs/    # 7 CPU bug kernels + 2 CUDA bug kernels
│   └── unit_tests/             # C++ unit tests (double-double, guard pages)
├── python/kernelguard/         # Python verification & analysis engine
│   ├── cli.py                  # CLI argument parser & subcommands
│   ├── runner.py               # Matrix compilation & execution engine
│   ├── toolchains/             # GCC, Clang, and NVCC adapters
│   ├── analysis/               # Remarks parser, disassembler, PTX parser, icount
│   ├── minimization/           # Delta debugging (ddmin) & repro.cpp generator
│   ├── baseline.py             # Lockfile manager & regression gatekeeper
│   └── reporting/              # HTML, JSON, JUnit XML, SARIF, Summary reporters
└── tests/                      # Python unit tests (15 test suites)
```

---

## 📄 License
Released under the [Apache-2.0 License](LICENSE).
Copyright © 2026 KernelGuard Contributors. Commercial licenses available at [sales@kernelguard.dev](mailto:sales@kernelguard.dev).
