#include "allocation_guard.hpp"

#include <cstdlib>
#include <new>

namespace liquibook::testing {

std::atomic<std::size_t>& allocation_counter() noexcept {
    static std::atomic<std::size_t> counter {0};
    return counter;
}

std::atomic<bool>& counting_enabled() noexcept {
    static std::atomic<bool> enabled {false};
    return enabled;
}

} // namespace liquibook::testing

// See allocation_guard.hpp's LIQUIBOOK_UNDER_TSAN comment: these overrides are omitted
// entirely under TSan, which needs its own runtime new/delete for its allocation tracking to
// work -- linking both is a link-time ODR violation, not merely redundant.
#if !LIQUIBOOK_UNDER_TSAN

void* operator new(std::size_t size) {
    if (liquibook::testing::counting_enabled().load(std::memory_order_relaxed)) {
        liquibook::testing::allocation_counter().fetch_add(1, std::memory_order_relaxed);
    }
    void* ptr = std::malloc(size);
    if (ptr == nullptr) {
        throw std::bad_alloc();
    }
    return ptr;
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* ptr) noexcept {
    std::free(ptr);
}

void operator delete[](void* ptr) noexcept {
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    std::free(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
    std::free(ptr);
}

// The nothrow overloads matter, not just for completeness: libstdc++'s internal
// std::stable_sort (used by GoogleTest to order tests) allocates its temp buffer via
// std::get_temporary_buffer, which calls operator new(size, nothrow) directly. Without an
// override here, that allocation goes through ASan's own default nothrow-new instead of this
// file's malloc-backed one, and gets freed via the (overridden) sized operator delete above
// -- an alloc-dealloc-mismatch ASan correctly flags as a real bug. This didn't surface on
// macOS/libc++ locally because its stable_sort doesn't use this code path; only caught once
// pushed to CI's gcc/clang-on-libstdc++ Linux builds.
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    if (liquibook::testing::counting_enabled().load(std::memory_order_relaxed)) {
        liquibook::testing::allocation_counter().fetch_add(1, std::memory_order_relaxed);
    }
    return std::malloc(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    return ::operator new(size, std::nothrow);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
    std::free(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
    std::free(ptr);
}

#endif // !LIQUIBOOK_UNDER_TSAN
