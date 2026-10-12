#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <memory>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include "classifier.h"

namespace kernelguard {

constexpr uint32_t CANARY_HEAD_PATTERN = 0xDEADBEEFu;
constexpr uint32_t CANARY_TAIL_PATTERN = 0xCAFEBABEu;

struct GuardedBuffer {
    void* raw_mapping;       // Base address of the mmap region
    size_t total_mapping_size;
    void* user_ptr;          // Aligned pointer presented to kernel
    size_t user_size_bytes;
    size_t page_size;
    size_t alignment;
    size_t offset_bytes;

    uint32_t* head_canary;
    uint32_t* tail_canary;

    GuardedBuffer()
        : raw_mapping(nullptr), total_mapping_size(0), user_ptr(nullptr),
          user_size_bytes(0), page_size(4096), alignment(32), offset_bytes(0),
          head_canary(nullptr), tail_canary(nullptr) {}

    ~GuardedBuffer() {
        release();
    }

    // Disable copy
    GuardedBuffer(const GuardedBuffer&) = delete;
    GuardedBuffer& operator=(const GuardedBuffer&) = delete;

    // Enable move
    GuardedBuffer(GuardedBuffer&& other) noexcept {
        move_from(other);
    }
    GuardedBuffer& operator=(GuardedBuffer&& other) noexcept {
        if (this != &other) {
            release();
            move_from(other);
        }
        return *this;
    }

    static GuardedBuffer allocate(size_t num_bytes, size_t align = 32, size_t misalign_offset = 0) {
        GuardedBuffer gb;
        long ps = sysconf(_SC_PAGESIZE);
        gb.page_size = (ps > 0) ? static_cast<size_t>(ps) : 4096;
        gb.alignment = align;
        gb.offset_bytes = misalign_offset;
        gb.user_size_bytes = num_bytes;

        // Layout:
        // [PROT_NONE Guard Page (1 page)]
        // [Head Canary (64 bytes)]
        // [User Data (rounded to multiple of page size or padded to end near tail guard)]
        // [Tail Canary (64 bytes)]
        // [PROT_NONE Guard Page (1 page)]

        size_t canary_size = 64;
        size_t data_with_canaries = canary_size + num_bytes + misalign_offset + canary_size;
        size_t pages_needed = (data_with_canaries + gb.page_size - 1) / gb.page_size;
        size_t inner_size = pages_needed * gb.page_size;

        gb.total_mapping_size = gb.page_size + inner_size + gb.page_size;

        // Allocate anonymous memory
        void* addr = mmap(nullptr, gb.total_mapping_size, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANON, -1, 0);
        if (addr == MAP_FAILED) {
            throw std::runtime_error("mmap failed to allocate guarded memory region");
        }
        gb.raw_mapping = addr;

        uint8_t* base = static_cast<uint8_t*>(addr);

        // Guard page 1 at the beginning: PROT_NONE
        if (mprotect(base, gb.page_size, PROT_NONE) != 0) {
            munmap(addr, gb.total_mapping_size);
            throw std::runtime_error("mprotect failed on leading guard page");
        }

        // Guard page 2 at the end: PROT_NONE
        uint8_t* end_guard = base + gb.page_size + inner_size;
        if (mprotect(end_guard, gb.page_size, PROT_NONE) != 0) {
            munmap(addr, gb.total_mapping_size);
            throw std::runtime_error("mprotect failed on trailing guard page");
        }

        uint8_t* active_region = base + gb.page_size;

        // Head canary
        gb.head_canary = reinterpret_cast<uint32_t*>(active_region);
        for (size_t i = 0; i < canary_size / sizeof(uint32_t); ++i) {
            gb.head_canary[i] = CANARY_HEAD_PATTERN;
        }

        // Align user pointer
        uint8_t* raw_user = active_region + canary_size;
        uintptr_t uaddr = reinterpret_cast<uintptr_t>(raw_user);
        size_t remainder = uaddr % align;
        size_t adjustment = (remainder == 0) ? 0 : (align - remainder);
        uint8_t* aligned_user = raw_user + adjustment + misalign_offset;

        gb.user_ptr = aligned_user;

        // Tail canary immediately following user data
        uint8_t* raw_tail = aligned_user + num_bytes;
        gb.tail_canary = reinterpret_cast<uint32_t*>(raw_tail);
        for (size_t i = 0; i < canary_size / sizeof(uint32_t); ++i) {
            gb.tail_canary[i] = CANARY_TAIL_PATTERN;
        }

        return gb;
    }

    template<typename T>
    T* as() {
        return static_cast<T*>(user_ptr);
    }

    template<typename T>
    const T* as() const {
        return static_cast<const T*>(user_ptr);
    }

    void prefill_canary_pattern() {
        if (!user_ptr || user_size_bytes == 0) return;
        uint32_t* p = static_cast<uint32_t*>(user_ptr);
        size_t words = user_size_bytes / sizeof(uint32_t);
        for (size_t i = 0; i < words; ++i) {
            p[i] = UNWRITTEN_FILL_PATTERN;
        }
    }

    bool verify_canaries(std::string& violation_reason) const {
        if (!head_canary || !tail_canary) return true;
        size_t canary_words = 64 / sizeof(uint32_t);

        for (size_t i = 0; i < canary_words; ++i) {
            if (head_canary[i] != CANARY_HEAD_PATTERN) {
                violation_reason = "Head canary underflow detected (memory before buffer was corrupted)";
                return false;
            }
        }
        for (size_t i = 0; i < canary_words; ++i) {
            if (tail_canary[i] != CANARY_TAIL_PATTERN) {
                violation_reason = "Tail canary overflow detected (memory after buffer was corrupted)";
                return false;
            }
        }
        return true;
    }

    void release() {
        if (raw_mapping && total_mapping_size > 0) {
            // Unprotect leading and trailing guard pages before unmapping
            uint8_t* base = static_cast<uint8_t*>(raw_mapping);
            mprotect(base, page_size, PROT_READ | PROT_WRITE);
            uint8_t* end_guard = base + total_mapping_size - page_size;
            mprotect(end_guard, page_size, PROT_READ | PROT_WRITE);

            munmap(raw_mapping, total_mapping_size);
            raw_mapping = nullptr;
            user_ptr = nullptr;
            total_mapping_size = 0;
        }
    }

private:
    void move_from(GuardedBuffer& other) noexcept {
        raw_mapping = other.raw_mapping;
        total_mapping_size = other.total_mapping_size;
        user_ptr = other.user_ptr;
        user_size_bytes = other.user_size_bytes;
        page_size = other.page_size;
        alignment = other.alignment;
        offset_bytes = other.offset_bytes;
        head_canary = other.head_canary;
        tail_canary = other.tail_canary;

        other.raw_mapping = nullptr;
        other.user_ptr = nullptr;
        other.total_mapping_size = 0;
        other.head_canary = nullptr;
        other.tail_canary = nullptr;
    }
};

} // namespace kernelguard
