# CUDA & PTX Verification Guide

KernelGuard includes a dedicated **NVIDIA CUDA and PTX Inspection Backend** designed to verify GPU code generation, assembly patterns, and runtime correctness.

---

## 1. Overview of the CUDA Backend

The CUDA verification pipeline operates in two modes:
1. **PTX Assembly Inspection (Compiler Verification):**
   - Compiles CUDA source files directly to PTX assembly using `nvcc -ptx`.
   - Analyzes generated PTX directives (`.version`, `.target`, `.address_size`).
   - Verifies register counts and memory space qualifications (`.global`, `.shared`, `.param`).
   - Verifies expected instruction patterns (e.g. `fma.rn.f32`, `add.f32`, `atomicAdd`).
   - **Does NOT require an active GPU.** This can run on any system with `nvcc` installed.
2. **CUDA Device Execution (Runtime Verification):**
   - Compiles and executes CUDA kernels on a compatible NVIDIA GPU.
   - Compares GPU device outputs against the CPU double-double reference.
   - Validates memory boundaries, non-multiples of block size, and CUDA stream synchronizations.

---

## 2. PTX Pattern Assertions

In `kernelguard.yaml`, each kernel can declare pattern assertions on the generated PTX:

```yaml
kernels:
  saxpy:
    type: "saxpy"
    source: "src/kernels/cpu/saxpy.cpp"
    cuda_source: "src/kernels/cuda/saxpy.cu"
    ptx_assertions:
      expected_instructions: ["fma.rn.f32", "st.global.f32"]
      forbidden_instructions: ["atom.global"]
```

When verified, the PTX parser parses all instruction opcodes, constructs a histogram of base and full opcodes, and validates the assertions.

---

## 3. Graceful Handling When CUDA Tooling or Hardware is Unavailable

When running in an environment without `nvcc` or without an NVIDIA GPU (e.g. macOS Apple Silicon or CPU-only cloud nodes):
- The CUDA toolchain adapter detects missing tooling.
- The corresponding test cases are marked **`SKIPPED`** with exact diagnostic messages:
  ```
  [SKIPPED] vec_add (cuda) - nvcc compiler not found
  [SKIPPED] saxpy (cuda) - nvcc compiler not found
  [SKIPPED] dot_product (cuda) - nvcc compiler not found
  ```
- Skipped tests are recorded in the HTML, JSON, JUnit XML, and Markdown reports.
- **Skipped tests are NEVER falsified as passes.**

---

## 4. Running CUDA Verification

To run only the CUDA backend:
```bash
./kg run --backend cuda --profile quick
```

To run the full matrix including CPU and CUDA:
```bash
./kg run --profile full
```
