# Architectural Decisions & Design Rationale

This document details the engineering decisions made during the design and implementation of **KernelGuard**.

---

## 1. Double-Double Arithmetic Reference (~106-bit Precision)

### Context
Evaluating compiler optimizations and vectorization transformations requires comparing kernel floating-point outputs against a trustworthy mathematical reference. Single-precision (`float32`) has 24 significand bits; double-precision (`float64`) has 53 significand bits. 

### Decision
We implemented a native, header-only `DoubleDouble` arithmetic engine based on Knuth's **2Sum** and Dekker's **2Prod** error-free transformations. Each number is stored as an unevaluated sum $s = hi + lo$ where $|lo| \le 0.5 \cdot \text{ulp}(hi)$.

### Rationale
- **Zero External Dependencies:** Unlike MPFR or GMP, `DoubleDouble` compiles into standalone binaries without requiring external dynamic libraries or complex linker flags.
- **Formally Exact for Single-Precision:** ~106 bits provides more than $4\times$ the precision of `float32`. For operations such as addition, subtraction, multiplication, and fused multiply-add, double-double evaluation represents an exact oracle relative to IEEE 754 single precision.
- **Fast and Inlinable:** Evaluates directly in CPU floating-point registers, running orders of magnitude faster than multi-precision heap-allocated software libraries.

---

## 2. Memory Isolation: Guard Pages + Canary Boundaries

### Context
Faulty or improperly vectorized kernels frequently introduce memory errors: off-by-one indexing, unaligned SIMD loads, or loop tail overruns.

### Decision
We implemented `GuardedBuffer` utilizing OS virtual memory pages via `mmap` and `mprotect`:
1. **Leading & Trailing Guard Pages:** 4KB memory pages configured with `PROT_NONE` bookend the allocation.
2. **Head & Tail Canaries:** 64-byte blocks filled with distinct bit patterns (`0xDEADBEEF`, `0xCAFEBABE`) placed immediately before and after the active user buffer.
3. **Unwritten Fill Pattern:** Output buffers are pre-filled with `0x7FA55A7F` before kernel invocation.

### Rationale
- Immediate page fault (`SIGSEGV`) if a vectorized memory access leaps outside the active region.
- Canaries catch sub-page buffer overflows (e.g. 4-byte overwrites) that would otherwise go unnoticed inside a padded page.
- Pre-filling detects missing tail loops where output elements were skipped or left unwritten.

---

## 3. Fork-per-Case Process Isolation with Timeout Enforcement

### Context
A miscompiled kernel or seeded bug can crash with `SIGSEGV`, trigger an infinite loop, or abort via sanitizer panics. In a monolithic test runner, a single crashing kernel halts the entire verification matrix.

### Decision
Every test case execution is forked into a dedicated child process with:
- Strict POSIX `alarm()` timeout enforcement.
- Signal interception (`SIGSEGV`, `SIGBUS`, `SIGFPE`, `SIGABRT`, `SIGALRM`).
- Inter-process communication via Unix pipe transferring structured `CaseVerdict` payloads.

### Rationale
- Complete isolation: crashes are caught, cleanly mapped to verdicts (`GUARD_PAGE_FAULT`, `TIMEOUT`, `CRASH`), and recorded without interrupting subsequent matrix tests.
- Sanitizer panics (`ASan`/`UBSan`) are intercepted and attributed accurately to the specific offending configuration.

---

## 4. Decoupling PTX Compilation from GPU Hardware Execution

### Context
NVIDIA compiler verification encompasses both code generation (compiling CUDA to PTX assembly) and device execution. Developers and CI pipelines frequently run on machines without physical NVIDIA GPUs (e.g., macOS laptops, standard cloud runners).

### Decision
We decoupled PTX compilation and inspection from CUDA execution:
- **PTX Inspection:** Compiles CUDA kernels to PTX (`nvcc -ptx`), parses the PTX assembly into an abstract representation, and verifies instruction pattern assertions (e.g. `fma.rn.f32`, `st.global`, register usage).
- **Graceful Skipping:** If `nvcc` is missing or GPU hardware is absent, the corresponding tests are explicitly marked `SKIPPED` with detailed diagnostics. They are **never** converted into artificial passes.

### Rationale
- Enables meaningful compiler verification in non-GPU environments.
- Enforces strict verification integrity by never falsifying hardware test passes.

---

## 5. Delta Debugging (ddmin) for Automated Failure Minimization

### Context
When a kernel fails on a large array (e.g., $N=1024$), analyzing the failure manually is labor-intensive.

### Decision
We implemented Zeller's classical **Delta Debugging (`ddmin`)** algorithm adapted for numerical arrays, producing a standalone reproducer bundle containing `repro.cpp`, the minimal input, compiler flags, and a self-contained reproduction script.

### Rationale
- Automatically isolates the minimal slice triggering numerical mismatch or memory violation.
- Generates zero-dependency reproducers that compiler engineers can compile and run directly in `gdb` or `lldb`.

---

## 6. Deterministic Instruction Gating vs. Advisory Wall-Clock Time

### Context
Wall-clock execution time varies widely between test runs due to CPU frequency scaling, OS scheduling, thermal throttling, and background tasks.

### Decision
- **Wall-clock timings** are explicitly categorized as **advisory**.
- **Instruction count gates** use deterministic measurements (Valgrind Callgrind) where supported. If unavailable on the host, instruction-count regressions are marked non-fatal and advisory.

### Rationale
- Prevents flaky CI failures caused by non-deterministic wall-clock jitter.
- Guarantees that regression gates only trigger on genuine compiler code-generation changes (e.g., instruction count inflation or vectorization loss).
