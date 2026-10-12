# KernelGuard 🛡️
### Production-Quality Compiler Verification & Numerical Correctness Framework
[![Verification CI](https://github.com/igotlinux/kernal-guard/actions/workflows/ci.yml/badge.svg)](https://github.com/igotlinux/kernal-guard/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/)
[![Python 3.10+](https://img.shields.io/badge/python-3.10+-blue.svg)](https://www.python.org/)

**KernelGuard** is an automated compiler verification framework designed to validate **numerical correctness**, **compiler optimization behavior (SIMD vectorization)**, and **performance regressions** across CPU (GCC, Clang) and GPU (NVIDIA CUDA, PTX) compilation toolchains.

Built as a credible portfolio project for compiler verification engineering, KernelGuard tests that optimization transformations never compromise mathematical integrity or introduce regressions.

---

## 🌟 Key Capabilities

### 1. High-Precision Numerical Oracle (~106-bit Double-Double)
- Evaluates outputs against an exact reference computed using Knuth's **2Sum** and Dekker's **2Prod** error-free transformations.
- Provides ~106 bits of significand precision, functioning as an exact oracle for IEEE 754 single-precision (`float32`) without external library overhead.
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

## 🚀 Quick Start

### Prerequisites
- C++17 compiler (GCC 12+ or Clang 15+)
- CMake 3.16+
- Python 3.10+ (with `pyyaml`)

### 1. Build the Harness
```bash
cmake -B build
cmake --build build
```

### 2. Run Verification Tests
```bash
# Run quick verification matrix (GCC + Clang)
./kg run --profile quick

# Run full nightly matrix
./kg run --profile full

# Run CPU-only verification
./kg run --backend cpu

# Run CUDA/PTX backend (gracefully skips if no GPU/nvcc)
./kg run --backend cuda
```

### 3. Run the Seeded Bug Suite
```bash
./build/kg_harness --seeded-bugs --profile quick
```

### 4. View Generated Reports
```bash
./kg report
# Or open the rich interactive HTML report:
open reports/report.html
```

---

## 📊 Verification Matrix & Example Output

```
======================================================================
Starting KernelGuard Verification Matrix...
Profile: quick | Target Backends: ['gcc', 'clang', 'cuda']
Host: darwin (arm64) | CUDA Present: False | GPU Hardware: False
======================================================================
  [PASS] vec_add (gcc -O0 strict)       - Verified contract (SIMD: False)
  [PASS] vec_add (gcc -O3 strict)       - Verified contract (SIMD: True)
  [PASS] vec_add (clang -O2 strict)     - Verified contract (SIMD: True)
  [SKIPPED] vec_add (cuda)              - nvcc compiler not found
  [PASS] saxpy (gcc -O3 fast-math)      - Verified contract (SIMD: True)
  [PASS] saxpy (clang -O3 strict)       - Verified contract (SIMD: True)
  [SKIPPED] saxpy (cuda)                - nvcc compiler not found
  [PASS] dot_product (gcc -O3 strict)   - Verified contract (SIMD: True)
  [PASS] dot_product (clang -O3 strict) - Verified contract (SIMD: True)
  [SKIPPED] dot_product (cuda)          - nvcc compiler not found
======================================================================
Verification Summary: 36 PASSED, 0 FAILED, 3 SKIPPED out of 39 configurations.
```

---

## 📁 Repository Structure

```
kernal-guard/
├── CMakeLists.txt              # C++ build definition
├── Dockerfile                  # Reproducible test container
├── kernelguard.yaml            # Toolchain, kernel, and matrix configuration
├── kernelguard_lock.json       # Baseline regression lockfile
├── kg                          # CLI entry point wrapper
├── include/kernelguard/
│   ├── double_double.h         # ~106-bit DoubleDouble arithmetic & ULP logic
│   ├── memory_guard.h          # PROT_NONE guard pages and canary buffers
│   ├── isolation.h             # Fork-per-case process isolation & signal handling
│   ├── classifier.h            # Failure classifier and contract evaluation
│   ├── test_case.h             # Test distributions (subnormal, cancellation, etc.)
│   └── harness.h               # Public C++ harness API & kernel registry
├── src/
│   ├── harness/main.cpp        # Standalone harness binary
│   ├── kernels/cpu/            # Vector addition, SAXPY, dot product
│   ├── kernels/cuda/           # CUDA vec_add, saxpy, dot_product
│   ├── kernels/seeded_bugs/    # Faulty test kernels for detection verification
│   └── unit_tests/             # C++ unit tests
├── python/kernelguard/
│   ├── cli.py                  # CLI command implementation
│   ├── config.py               # YAML configuration loader & validator
│   ├── host.py                 # CPU SIMD and GPU capability detector
│   ├── runner.py               # Matrix compilation & execution engine
│   ├── toolchains/             # GCC, Clang, and NVCC adapters
│   ├── analysis/               # Remarks parser, disassembler, PTX parser
│   ├── minimization/           # Delta debugging (ddmin) & reproducer generator
│   ├── baseline.py             # Lockfile manager & regression gatekeeper
│   └── reporting/              # HTML, JSON, JUnit XML, SARIF, and Markdown reporters
├── tests/                      # Python unit test suite
├── docs/                       # Architecture, engineering decisions, and guides
└── reports/                    # Generated test reports
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

## 🛠️ CLI Interface (`kg`)

| Command | Description |
| :--- | :--- |
| `kg run --profile quick` | Execute quick verification profile |
| `kg run --profile full` | Execute comprehensive nightly matrix |
| `kg run --backend cpu` | Limit execution to CPU toolchains (GCC, Clang) |
| `kg run --backend cuda` | Limit execution to CUDA/PTX backend |
| `kg report` | Display summary of latest verification run |
| `kg repro <failure-id>` | Inspect standalone reproducer bundle |
| `kg baseline update` | Update `kernelguard_lock.json` with baseline metrics |

---

## 📄 License
This project is licensed under the MIT License.
