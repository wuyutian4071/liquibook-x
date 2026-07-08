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
