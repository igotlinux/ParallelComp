#include <iostream>
#include <cassert>
#include <string>
#include "kernelguard/memory_guard.h"
#include "kernelguard/isolation.h"

using namespace kernelguard;

void test_buffer_allocation_and_canaries() {
    GuardedBuffer buf = GuardedBuffer::allocate(100 * sizeof(float), 32, 0);
    assert(buf.user_ptr != nullptr);
    assert(reinterpret_cast<uintptr_t>(buf.user_ptr) % 32 == 0);

    // Initial canaries must be intact
    std::string err;
    assert(buf.verify_canaries(err));

    // Fill buffer normally
    float* ptr = buf.as<float>();
    for (size_t i = 0; i < 100; ++i) {
        ptr[i] = static_cast<float>(i);
    }
    assert(buf.verify_canaries(err));
    std::cout << "[PASS] test_buffer_allocation_and_canaries\n";
}

void test_canary_corruption_detection() {
    GuardedBuffer buf = GuardedBuffer::allocate(10 * sizeof(float), 32, 0);
    std::string err;
    assert(buf.verify_canaries(err));

    // Clobber tail canary intentionally
    uint32_t* tail = buf.tail_canary;
    tail[0] = 0x12345678;
    assert(!buf.verify_canaries(err));
    assert(err.find("Tail canary overflow") != std::string::npos);
    std::cout << "[PASS] test_canary_corruption_detection\n";
}

void test_guard_page_catch() {
    // In an isolated sub-process, attempt to write directly into the trailing guard page
    auto bad_write = []() -> CaseVerdict {
        GuardedBuffer buf = GuardedBuffer::allocate(16 * sizeof(float), 32, 0);
        // Page size offset past buffer reaches the trailing guard page
        uint8_t* guard_page = static_cast<uint8_t*>(buf.raw_mapping) + buf.total_mapping_size - buf.page_size;
        *guard_page = 0xFF; // Trigger PROT_NONE violation
        return CaseVerdict::pass();
    };

    ExecutionResult res = run_isolated_case(bad_write, 2);
    assert(res.verdict.type == VerdictType::GUARD_PAGE_FAULT);
    std::cout << "[PASS] test_guard_page_catch (SIGSEGV cleanly caught and classified as GUARD_PAGE_FAULT)\n";
}

int main() {
    std::cout << "Running Memory Guard Unit Tests...\n";
    test_buffer_allocation_and_canaries();
    test_canary_corruption_detection();
    test_guard_page_catch();
    std::cout << "All Memory Guard Unit Tests PASSED!\n";
    return 0;
}
