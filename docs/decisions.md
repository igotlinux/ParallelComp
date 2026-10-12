# Architectural Decisions & Design Rationale

This document details the engineering decisions made during the design and implementation of **KernelGuard**.

---

## Decision Log (Core Interview & Design Questions)

| Decision | Rejected Alternative | Why |
|---|---|---|
| **Python orchestrator + C++ harness** | All Rust / Go / C++ | Orchestration is I/O-bound and quick to write in Python. The harness must be C++ so it links the user's code in-process under the exact same flags and compiler passes. It directly matches systems compiler verification requirements. |
| **Explicit binding macro / registry `KG_KERNEL`** | Auto-discovering kernels | Argument semantics, pointer aliasing, and reduction bounds cannot be inferred reliably without contracts. Explicit binding costs ~10 lines per kernel. |
| **Reference = double-double accumulation (~106-bit class)** | `long double`, MPFR, `__float128` | `long double` varies by platform (80-bit on x86, 64-bit on MSVC, 128-bit on PowerPC). MPFR is a heavy external library dependency. Double-double is portable, header-only, fast, and an exact oracle for float32. |
| **Gate on deterministic signals (contracts, remarks, instruction counts)** | Gate on wall-clock timing | Shared CI runners have high timing variance due to CPU frequency scaling, hyperthreading, and noisy neighbors. Deterministic instruction counts (Callgrind) prevent false CI alarms. |
| **Never compare timings across machines; interleaved A/B in the same job** | Stored timing baselines | Cross-machine drift makes stored wall-clock baselines meaningless. Baseline vs PR code must run interleaved in the exact same process environment. |
| **Committed lockfile for deterministic metrics (`kernelguard_lock.json`)** | External database | Works like snapshot testing, is reviewable in the PR diff, and requires zero cloud infrastructure. |
| **Toolchain Docker images pinned by digest** | `apt install` latest at job time | Reproducibility. Compiler "latest" minor versions change optimization remarks and heuristics unexpectedly. |
| **Apache-2.0 License** | MIT, GPL | Explicit patent grant, corporate-friendly, same license family as LLVM and the wider compiler ecosystem. |
| **Backend interface for compilers, CUDA as an experimental backend** | CUDA in v1 core | CI runners usually have no physical GPU. A clean interface decouples PTX compilation inspection from device execution without rework. |
| **MPI dropped from core execution** | MPI job farm in runner | A work-queue with `multiprocessing` is simpler, more resilient, and sufficient for CI test farms. MPI kernels remain supported as domain workloads. |

---

## Detailed Technical Rationales

### 1. Double-Double Arithmetic Reference (~106-bit Precision)
- **Context:** Evaluating compiler optimizations and vectorization transformations requires comparing kernel floating-point outputs against a trustworthy mathematical reference. Single-precision (`float32`) has 24 significand bits; double-precision (`float64`) has 53 significand bits.
- **Mechanism:** Implemented native `DoubleDouble` arithmetic based on Knuth's **2Sum** and Dekker's **2Prod** error-free transformations ($s = hi + lo$, $|lo| \le 0.5 \cdot \text{ulp}(hi)$).
- **Benefit:** ~106 bits provides more than $4\times$ the precision of `float32`. For operations such as addition, subtraction, multiplication, and fused multiply-add, double-double evaluation represents an exact oracle relative to IEEE 754 single precision without external library overhead.

### 2. Memory Isolation: Guard Pages + Canary Boundaries
- **Context:** Faulty or improperly vectorized kernels frequently introduce memory errors: off-by-one indexing, unaligned SIMD loads, or loop tail overruns.
- **Mechanism:** `GuardedBuffer` allocates regions bookended by `PROT_NONE` virtual memory guard pages using `mmap`/`mprotect`, plus 64-byte head and tail canaries (`0xDEADBEEF`, `0xCAFEBABE`), and pre-filling output buffers with `0x7FA55A7F`.
- **Benefit:** Immediate page fault (`SIGSEGV`) if a vectorized memory access leaps outside the active region; canaries catch sub-page buffer overflows (e.g. 4-byte overwrites); pre-filling detects missing tail loops where output elements were skipped or left unwritten.

### 3. Fork-per-Case Process Isolation with Timeout Enforcement
- **Context:** A miscompiled kernel or seeded bug can crash with `SIGSEGV`, trigger an infinite loop, or abort via sanitizer panics. In a monolithic test runner, a single crashing kernel halts the entire verification matrix.
- **Mechanism:** Every test case execution is forked into a dedicated child process with POSIX `alarm()` timeout enforcement, signal interception (`SIGSEGV`, `SIGBUS`, `SIGFPE`, `SIGABRT`), and pipe IPC transferring structured `CaseVerdict` payloads.
- **Benefit:** Complete isolation: crashes are caught, cleanly mapped to verdicts (`GUARD_PAGE_FAULT`, `TIMEOUT`, `CRASH`), and recorded without interrupting subsequent matrix tests.

### 4. Decoupling PTX Compilation from GPU Hardware Execution
- **Context:** NVIDIA compiler verification encompasses both code generation (compiling CUDA to PTX assembly) and device execution. Developers and CI pipelines frequently run on machines without physical NVIDIA GPUs (e.g., macOS laptops, standard cloud runners).
- **Mechanism:** Compiles CUDA kernels to PTX (`nvcc -ptx`), parses the PTX assembly into an abstract representation, and verifies instruction pattern assertions (e.g. `fma.rn.f32`, `st.global`, register usage).
- **Benefit:** If `nvcc` is missing or GPU hardware is absent, the corresponding tests are explicitly marked `SKIPPED` with detailed diagnostics. They are **never** converted into artificial passes.

### 5. Delta Debugging (ddmin) for Automated Failure Minimization
- **Context:** When a kernel fails on a large array (e.g., $N=1024$), analyzing the failure manually is labor-intensive.
- **Mechanism:** Classical Delta Debugging (`ddmin`) algorithm adapted for numerical arrays, producing a standalone reproducer bundle containing `repro.cpp`, the minimal input, compiler flags, and a self-contained reproduction script.
- **Benefit:** Automatically isolates the minimal slice triggering numerical mismatch or memory violation, producing zero-dependency reproducers that compiler engineers can compile and run directly in `gdb` or `lldb`.

### 6. Deterministic Instruction Gating vs. Advisory Wall-Clock Time
- **Context:** Wall-clock execution time varies widely between test runs due to CPU frequency scaling, OS scheduling, thermal throttling, and background tasks.
- **Mechanism:** Wall-clock timings are explicitly categorized as advisory. Instruction count gates use deterministic measurements (Valgrind Callgrind) where supported.
- **Benefit:** Prevents flaky CI failures caused by non-deterministic wall-clock jitter while ensuring regression gates trigger on genuine compiler code-generation changes.
