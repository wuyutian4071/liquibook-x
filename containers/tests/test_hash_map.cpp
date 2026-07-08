#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "hash_map.hpp"
#include "testing/allocation_guard.hpp"

using liquibook::containers::OpenAddressingHashMap;
using liquibook::testing::AllocationGuard;

TEST(HashMap, InsertFindEraseRoundTrip) {
    OpenAddressingHashMap<std::uint64_t, int> map(16);

    EXPECT_TRUE(map.insert(1, 100));
    EXPECT_TRUE(map.insert(2, 200));
    EXPECT_TRUE(map.insert(3, 300));
    EXPECT_EQ(map.size(), 3u);

    ASSERT_NE(map.find(1), nullptr);
    EXPECT_EQ(*map.find(1), 100);
    ASSERT_NE(map.find(2), nullptr);
    EXPECT_EQ(*map.find(2), 200);
    EXPECT_EQ(map.find(999), nullptr);

    EXPECT_TRUE(map.erase(2));
    EXPECT_EQ(map.find(2), nullptr);
    EXPECT_EQ(map.size(), 2u);
    EXPECT_FALSE(map.erase(2)); // already gone
}

TEST(HashMap, InsertingADuplicateKeyFails) {
    OpenAddressingHashMap<std::uint64_t, int> map(16);
    EXPECT_TRUE(map.insert(42, 1));
    EXPECT_FALSE(map.insert(42, 2));
    ASSERT_NE(map.find(42), nullptr);
    EXPECT_EQ(*map.find(42), 1); // unchanged by the failed insert
}

TEST(HashMap, EraseThenReinsertReusesTombstoneSlotAndSizeStaysBounded) {
    OpenAddressingHashMap<std::uint64_t, int> map(8);
    for (std::uint64_t i = 0; i < 4; ++i) {
        ASSERT_TRUE(map.insert(i, static_cast<int>(i)));
    }
    EXPECT_EQ(map.size(), 4u);
    EXPECT_EQ(map.tombstone_count(), 0u);

    // Many erase/reinsert cycles on the same key: size and tombstone_count must both stay
    // bounded rather than growing without limit (which would eventually starve insert() even
    // though the *logical* number of live entries never exceeds 4).
    for (int cycle = 0; cycle < 100; ++cycle) {
        ASSERT_TRUE(map.erase(0));
        ASSERT_TRUE(map.insert(0, cycle));
        EXPECT_EQ(map.size(), 4u);
        EXPECT_LE(map.tombstone_count(), 1u);
    }
    ASSERT_NE(map.find(0), nullptr);
    EXPECT_EQ(*map.find(0), 99);
}

TEST(HashMap, InsertPastCapacityFailsCleanly) {
    OpenAddressingHashMap<std::uint64_t, int> map(4); // rounds up to capacity 4
    int inserted = 0;
    for (std::uint64_t i = 0; i < 100 && inserted < static_cast<int>(map.capacity()); ++i) {
        if (map.insert(i, static_cast<int>(i))) {
            ++inserted;
        }
    }
    EXPECT_EQ(map.size(), map.capacity());

    // The map is completely full: inserting a genuinely new key must fail, not corrupt state.
    EXPECT_FALSE(map.insert(999'999, -1));
    EXPECT_EQ(map.size(), map.capacity());
    EXPECT_EQ(map.find(999'999), nullptr);
}

TEST(HashMap, CapacityRoundsUpToPowerOfTwo) {
    OpenAddressingHashMap<std::uint64_t, int> a(1);
    OpenAddressingHashMap<std::uint64_t, int> b(5);
    OpenAddressingHashMap<std::uint64_t, int> c(16);
    OpenAddressingHashMap<std::uint64_t, int> d(17);
    EXPECT_EQ(a.capacity(), 1u);
    EXPECT_EQ(b.capacity(), 8u);
    EXPECT_EQ(c.capacity(), 16u);
    EXPECT_EQ(d.capacity(), 32u);
}

// Differential/property-style test: insert a batch of pseudo-random keys, verify every one
// is findable with the correct value against an independent std::unordered_map reference,
// erase half at random, then verify exactly the right ones remain findable and the rest
// don't -- not just hand-picked examples.
TEST(HashMap, StressAgainstUnorderedMapReference) {
    constexpr std::uint64_t kCapacityHint = 4096;
    constexpr int kNumKeys = 2000; // well under capacity for a safe load factor

    OpenAddressingHashMap<std::uint64_t, std::uint64_t> map(kCapacityHint);
    std::unordered_map<std::uint64_t, std::uint64_t> reference;

    std::mt19937_64 rng(2024);
    std::unordered_set<std::uint64_t> used_keys;
    while (used_keys.size() < static_cast<std::size_t>(kNumKeys)) {
        used_keys.insert(rng());
    }

    for (std::uint64_t key : used_keys) {
        const std::uint64_t value = key ^ 0xA5A5A5A5A5A5A5A5ULL;
        ASSERT_TRUE(map.insert(key, value));
        reference.emplace(key, value);
    }
    ASSERT_EQ(map.size(), reference.size());

    for (const auto& [key, value] : reference) {
        auto* found = map.find(key);
        ASSERT_NE(found, nullptr) << "missing key " << key;
        EXPECT_EQ(*found, value);
    }

    // Erase roughly half, chosen at random.
    std::vector<std::uint64_t> all_keys(used_keys.begin(), used_keys.end());
    std::shuffle(all_keys.begin(), all_keys.end(), rng);
    const std::size_t erase_count = all_keys.size() / 2;
    std::unordered_set<std::uint64_t> erased;
    for (std::size_t i = 0; i < erase_count; ++i) {
        ASSERT_TRUE(map.erase(all_keys[i]));
        reference.erase(all_keys[i]);
        erased.insert(all_keys[i]);
    }
    ASSERT_EQ(map.size(), reference.size());

    for (std::uint64_t key : all_keys) {
        auto* found = map.find(key);
        if (erased.count(key) > 0) {
            EXPECT_EQ(found, nullptr) << "erased key " << key << " still found";
        } else {
            ASSERT_NE(found, nullptr) << "surviving key " << key << " went missing";
            EXPECT_EQ(*found, reference.at(key));
        }
    }
}

TEST(HashMap, InsertFindEraseAllocateNoHeapMemory) {
#if LIQUIBOOK_UNDER_TSAN
    GTEST_SKIP() << "allocation_guard's operator new/delete override is disabled under TSan "
                    "(it would conflict with TSan's own runtime allocator interception)";
#else
    OpenAddressingHashMap<std::uint64_t, int> map(64);
    // Warm up outside the guarded region.
    for (std::uint64_t i = 0; i < 32; ++i) {
        map.insert(i, static_cast<int>(i));
    }
    for (std::uint64_t i = 0; i < 32; ++i) {
        map.erase(i);
    }

    {
        AllocationGuard guard;
        for (std::uint64_t i = 0; i < 32; ++i) {
            map.insert(i, static_cast<int>(i));
        }
        for (std::uint64_t i = 0; i < 32; ++i) {
            volatile bool found = (map.find(i) != nullptr);
            (void)found;
        }
        for (std::uint64_t i = 0; i < 32; ++i) {
            map.erase(i);
        }
        const std::size_t allocations = guard.count();
        EXPECT_EQ(allocations, 0u);
    }
#endif // LIQUIBOOK_UNDER_TSAN
}
