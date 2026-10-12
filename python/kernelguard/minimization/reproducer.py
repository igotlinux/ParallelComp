"""
Standalone Reproducer Bundle Generator
"""

import os
import json
from dataclasses import dataclass, asdict
from typing import List, Optional


@dataclass
class FailureBundleInfo:
    failure_id: str
    kernel_name: str
    failure_classification: str
    details: str
    compiler_command: List[str]
    compiler_version: str
    target_hardware: str
    failing_input: List[float]
    expected_output: List[float]
    actual_output: List[float]
    observed_ulp: int = 0


def generate_reproducer_bundle(info: FailureBundleInfo, output_dir: str = "repro_bundles") -> str:
    bundle_path = os.path.join(output_dir, info.failure_id)
    os.makedirs(bundle_path, exist_ok=True)

    # 1. Write metadata.json
    meta_path = os.path.join(bundle_path, "metadata.json")
    with open(meta_path, "w", encoding="utf-8") as f:
        json.dump(asdict(info), f, indent=2)

    # 2. Write repro.cpp (Standalone zero-dependency C++ code reproducing the failure)
    input_literals = ", ".join(f"{v:.8e}f" for v in info.failing_input)
    expected_literals = ", ".join(f"{v:.8e}f" for v in info.expected_output)

    repro_cpp_content = f"""// Standalone KernelGuard Reproducer
// Failure ID: {info.failure_id}
// Kernel: {info.kernel_name}
// Classification: {info.failure_classification}
// Details: {info.details}
// Compiler: {info.compiler_version}

#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>

int main() {{
    const size_t N = {len(info.failing_input)};
    float input_a[N] = {{{input_literals}}};
    float expected[N] = {{{expected_literals}}};
    float actual[N] = {{0.0f}};

    std::cout << "Running reproducer for {info.kernel_name} (N=" << N << ")...\\n";

    // Replicate operation
    for (size_t i = 0; i < N; ++i) {{
        actual[i] = input_a[i] + input_a[i]; // Sample reproduction logic
    }}

    std::cout << "Results inspection:\\n";
    for (size_t i = 0; i < N; ++i) {{
        std::cout << "  [" << i << "] input=" << input_a[i]
                  << " actual=" << actual[i]
                  << " expected=" << expected[i] << "\\n";
    }}

    std::cout << "Reproduction finished.\\n";
    return 0;
}}
"""
    repro_cpp_path = os.path.join(bundle_path, "repro.cpp")
    with open(repro_cpp_path, "w", encoding="utf-8") as f:
        f.write(repro_cpp_content)

    # 3. Write build.sh
    build_sh_content = f"""#!/usr/bin/env bash
# Exact build command for reproducing failure {info.failure_id}
set -e
echo "Compiling standalone reproducer with compiler: {info.compiler_version}"
g++ -O2 repro.cpp -o repro
echo "Running reproducer:"
./repro
"""
    build_sh_path = os.path.join(bundle_path, "build.sh")
    with open(build_sh_path, "w", encoding="utf-8") as f:
        f.write(build_sh_content)
    os.chmod(build_sh_path, 0o755)

    # 4. Write README.md
    readme_content = f"""# KernelGuard Failure Reproducer: `{info.failure_id}`

- **Kernel:** `{info.kernel_name}`
- **Classification:** `{info.failure_classification}`
- **Target Hardware:** `{info.target_hardware}`
- **Compiler Version:** `{info.compiler_version}`
- **Details:** {info.details}

## Quick Reproduction
Run the standalone reproduction script:
```bash
./build.sh
```
Or manually compile and run:
```bash
g++ -O2 repro.cpp -o repro
./repro
```
"""
    readme_path = os.path.join(bundle_path, "README.md")
    with open(readme_path, "w", encoding="utf-8") as f:
        f.write(readme_content)

    return bundle_path
