#pragma once

#include <atomic>
#include <cstddef>

// A global operator new/delete override (defined once in allocation_guard.cpp -- replacement
// new/delete must have external linkage, so it can't live in this header) that counts
// allocations while an AllocationGuard is active, so a test can assert a hot-path operation
// performs zero heap allocation. This affects the whole process it's linked into -- since
// each CMake test executable here is its own process, that means only whichever *_tests
// binary links testing/allocation_guard.cpp (containers/tests/, and later milestones' test
// binaries), never other unrelated test executables.
//
// The override calls through to std::malloc/std::free rather than replacing the underlying
// allocator, so ASan's own interposition still applies underneath this -- both memory-safety
// checking and allocation counting work simultaneously.
//
// Usage discipline: keep the guarded region minimal -- just the call(s) under test, with
// results stored in plain local variables and every EXPECT_*/ASSERT_* check *outside* the
// guard's scope. GoogleTest's own bookkeeping is allocation-light on a passing assertion's
// fast path, but there's no guarantee it's allocation-*free*, so don't rely on it inside the
// counted region.

namespace liquibook::testing {

std::atomic<std::size_t>& allocation_counter() noexcept;
std::atomic<bool>& counting_enabled() noexcept;

class AllocationGuard {
public:
    AllocationGuard() noexcept {
        allocation_counter().store(0, std::memory_order_relaxed);
        counting_enabled().store(true, std::memory_order_relaxed);
    }

    ~AllocationGuard() noexcept { counting_enabled().store(false, std::memory_order_relaxed); }

    AllocationGuard(const AllocationGuard&) = delete;
    AllocationGuard& operator=(const AllocationGuard&) = delete;

    [[nodiscard]] std::size_t count() const noexcept {
        return allocation_counter().load(std::memory_order_relaxed);
    }
};

} // namespace liquibook::testing
