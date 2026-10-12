cmake_minimum_required(VERSION 3.16)
project(KernelGuard LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Enable compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

include_directories(include)

# KernelGuard Core Library
add_library(kernelguard_core STATIC
    src/kernels/cpu/vec_add.cpp
    src/kernels/cpu/saxpy.cpp
    src/kernels/cpu/dot_product.cpp
    src/kernels/seeded_bugs/cpu_bugs.cpp
)

# Harness Standalone CLI Executable
add_executable(kg_harness
    src/harness/main.cpp
)
target_link_libraries(kg_harness PRIVATE kernelguard_core)

# C++ Unit Tests
add_executable(test_double_double
    src/unit_tests/test_double_double.cpp
)
target_link_libraries(test_double_double PRIVATE kernelguard_core)

add_executable(test_guard_pages
    src/unit_tests/test_guard_pages.cpp
)
target_link_libraries(test_guard_pages PRIVATE kernelguard_core)

# Optional CUDA Hardware Harness
include(CheckLanguage)
check_language(CUDA)
if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)

    add_executable(kg_cuda_harness
        src/kernels/cuda/cuda_harness.cu
        src/kernels/cuda/vec_add.cu
        src/kernels/cuda/saxpy.cu
        src/kernels/cuda/dot_product.cu
        src/kernels/seeded_bugs/bug_cuda_bounds.cu
        src/kernels/seeded_bugs/bug_cuda_launch_dim.cu
    )
    target_include_directories(kg_cuda_harness PRIVATE include)
    message(STATUS "CUDA compiler detected: building kg_cuda_harness")
else()
    message(STATUS "CUDA compiler not detected: skipping kg_cuda_harness (CPU harness active)")
endif()

# Installation Targets for Production Deployment
include(GNUInstallDirs)
install(TARGETS kg_harness kernelguard_core
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

enable_testing()
add_test(NAME DoubleDoubleTest COMMAND test_double_double)
add_test(NAME GuardPagesTest COMMAND test_guard_pages)
if(CMAKE_CUDA_COMPILER)
    add_test(NAME CudaHarnessTest COMMAND kg_cuda_harness)
endif()
add_test(NAME HarnessSmokeClean COMMAND kg_harness --all --profile quick)
add_test(NAME HarnessSmokeSeededBugs COMMAND kg_harness --seeded-bugs --profile quick)
