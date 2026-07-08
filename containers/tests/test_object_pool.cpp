#include <gtest/gtest.h>

#include <vector>

#include "object_pool.hpp"
#include "testing/allocation_guard.hpp"

using liquibook::containers::ObjectPool;
using liquibook::testing::AllocationGuard;

namespace {

struct Point {
    int x;
    int y;
    Point(int x_, int y_) : x(x_), y(y_) {}
};

} // namespace

TEST(ObjectPool, AcquireConstructsWithForwardedArgs) {
    ObjectPool<Point> pool(4);
    Point* p = pool.acquire(3, 4);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->x, 3);
    EXPECT_EQ(p->y, 4);
}

TEST(ObjectPool, AcquireFillsToCapacityThenReturnsNullptr) {
    ObjectPool<Point> pool(3);
    Point* a = pool.acquire(1, 1);
    Point* b = pool.acquire(2, 2);
    Point* c = pool.acquire(3, 3);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_NE(c, nullptr);
    EXPECT_TRUE(pool.full());

    Point* d = pool.acquire(4, 4);
    EXPECT_EQ(d, nullptr);
    EXPECT_EQ(pool.size(), 3u);
}

TEST(ObjectPool, ReleaseAndReacquireReusesFreedSlots) {
    ObjectPool<Point> pool(2);
    Point* a = pool.acquire(1, 1);
    Point* b = pool.acquire(2, 2);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_TRUE(pool.full());

    pool.release(b);
    EXPECT_EQ(pool.size(), 1u);
    EXPECT_FALSE(pool.full());

    // The freed slot (b's) should be handed back out first (LIFO free list).
    Point* c = pool.acquire(5, 5);
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c, b);
    EXPECT_EQ(c->x, 5);
    EXPECT_EQ(c->y, 5);
    EXPECT_TRUE(pool.full());
}

TEST(ObjectPool, SizeAndCapacityTrackCorrectly) {
    ObjectPool<Point> pool(5);
    EXPECT_EQ(pool.capacity(), 5u);
    EXPECT_TRUE(pool.empty());

    std::vector<Point*> acquired;
    for (int i = 0; i < 5; ++i) {
        Point* p = pool.acquire(i, i);
        ASSERT_NE(p, nullptr);
        acquired.push_back(p);
    }
    EXPECT_EQ(pool.size(), 5u);
    EXPECT_TRUE(pool.full());

    for (Point* p : acquired) {
        pool.release(p);
    }
    EXPECT_TRUE(pool.empty());
    EXPECT_EQ(pool.size(), 0u);
}

TEST(ObjectPool, ZeroCapacityPoolAlwaysReturnsNullptr) {
    ObjectPool<Point> pool(0);
    EXPECT_EQ(pool.acquire(1, 1), nullptr);
    EXPECT_TRUE(pool.empty());
    EXPECT_TRUE(pool.full());
}

TEST(ObjectPool, AcquireReleaseCycleAllocatesNoHeapMemory) {
#if LIQUIBOOK_UNDER_TSAN
    GTEST_SKIP() << "allocation_guard's operator new/delete override is disabled under TSan "
                    "(it would conflict with TSan's own runtime allocator interception)";
#else
    ObjectPool<Point> pool(16);
    // Warm up: touch every slot once outside the guarded region so any one-time lazy
    // initialization (there isn't any here, but this keeps the test robust to future
    // changes) doesn't get misattributed as a hot-path allocation.
    std::vector<Point*> warm;
    for (int i = 0; i < 16; ++i) {
        warm.push_back(pool.acquire(i, i));
    }
    for (Point* p : warm) {
        pool.release(p);
    }

    Point* held[16] = {};
    {
        AllocationGuard guard;
        for (int i = 0; i < 16; ++i) {
            held[i] = pool.acquire(i, i * 2);
        }
        for (int i = 0; i < 16; ++i) {
            pool.release(held[i]);
        }
        const std::size_t allocations = guard.count();
        EXPECT_EQ(allocations, 0u);
    }
#endif // LIQUIBOOK_UNDER_TSAN
}
