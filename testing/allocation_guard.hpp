#pragma once

#include <atomic>
#include <cstddef>

// Detects a ThreadSanitizer build. TSan ships its own operator new/delete overrides in its
// runtime (libclang_rt.tsan_cxx) that it needs for its own allocation tracking to work
// correctly -- linking a second, user-defined set of overrides alongside it is a genuine ODR
// violation ("multiple definition of operator new") at link time, not just unnecessary.
// allocation_guard.cpp uses this to omit its overrides entirely under TSan; the two tests
// that rely on them (containers/tests/test_object_pool.cpp,
// containers/tests/test_hash_map.cpp) use it to skip cleanly rather than silently always
// pass with a counter that nothing feeds anymore.
// GCC's preprocessor doesn't know __has_feature at all -- not even as "defined but always
// false" -- so a single-line `defined(__has_feature) && __has_feature(...)` still fails to
// parse on GCC (short-circuiting applies to the *value*, not to whether the right operand
// must be syntactically valid). The portable fix, used by several major C++ codebases: give
// __has_feature a no-op fallback definition when it doesn't already exist, so the expression
// below is always well-formed.
#ifndef __has_feature
#define __has_feature(x) 0
#endif

#if defined(__SANITIZE_THREAD__) || __has_feature(thread_sanitizer)
#define LIQUIBOOK_UNDER_TSAN 1
#else
#define LIQUIBOOK_UNDER_TSAN 0
#endif

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
