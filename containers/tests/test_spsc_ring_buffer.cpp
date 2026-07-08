#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "spsc_ring_buffer.hpp"

using liquibook::containers::SpscRingBuffer;

TEST(SpscRingBuffer, CapacityRoundsUpToNextPowerOfTwo) {
    EXPECT_EQ(SpscRingBuffer<int>(1).capacity(), 1u);
    EXPECT_EQ(SpscRingBuffer<int>(2).capacity(), 2u);
    EXPECT_EQ(SpscRingBuffer<int>(3).capacity(), 4u);
    EXPECT_EQ(SpscRingBuffer<int>(9).capacity(), 16u);
    EXPECT_EQ(SpscRingBuffer<int>(16).capacity(), 16u);
}

TEST(SpscRingBuffer, PopOnEmptyFails) {
    SpscRingBuffer<int> buffer(4);
    int out = 0;
    EXPECT_FALSE(buffer.pop(out));
}

TEST(SpscRingBuffer, PushPopPreservesFifoOrder) {
    SpscRingBuffer<int> buffer(4);
    EXPECT_TRUE(buffer.push(1));
    EXPECT_TRUE(buffer.push(2));
    EXPECT_TRUE(buffer.push(3));

    int out = 0;
    ASSERT_TRUE(buffer.pop(out));
    EXPECT_EQ(out, 1);
    ASSERT_TRUE(buffer.pop(out));
    EXPECT_EQ(out, 2);
    ASSERT_TRUE(buffer.pop(out));
    EXPECT_EQ(out, 3);
    EXPECT_FALSE(buffer.pop(out));
}

TEST(SpscRingBuffer, PushOnFullFails) {
    SpscRingBuffer<int> buffer(4); // rounds to 4
    EXPECT_TRUE(buffer.push(1));
    EXPECT_TRUE(buffer.push(2));
    EXPECT_TRUE(buffer.push(3));
    EXPECT_TRUE(buffer.push(4));
    EXPECT_FALSE(buffer.push(5)); // full
}

TEST(SpscRingBuffer, WrapsAroundCorrectlyAfterDraining) {
    SpscRingBuffer<int> buffer(4);
    int out = 0;

    // Fill, fully drain, refill repeatedly -- exercises the physical-slot wraparound many
    // times over, not just once.
    for (int round = 0; round < 5; ++round) {
        for (int i = 0; i < 4; ++i) {
            ASSERT_TRUE(buffer.push(round * 10 + i));
        }
        EXPECT_FALSE(buffer.push(999)); // full
        for (int i = 0; i < 4; ++i) {
            ASSERT_TRUE(buffer.pop(out));
            EXPECT_EQ(out, round * 10 + i);
        }
        EXPECT_FALSE(buffer.pop(out)); // empty
    }
}

// The test ENABLE_TSAN is specifically for: a real producer thread and a real consumer
// thread, concurrently pushing/popping through a buffer far smaller than the item count so
// wraparound and full/empty contention both happen constantly, not as a rare edge case.
TEST(SpscRingBuffer, ConcurrentProducerConsumerDeliversEveryItemExactlyOnceInOrder) {
    constexpr std::size_t kCapacity = 64;
    constexpr std::uint64_t kItemCount = 200'000;

    SpscRingBuffer<std::uint64_t> buffer(kCapacity);
    std::atomic<bool> producer_done {false};
    std::vector<std::uint64_t> received;
    received.reserve(kItemCount);

    std::thread producer([&] {
        for (std::uint64_t i = 0; i < kItemCount; ++i) {
            while (!buffer.push(i)) {
                std::this_thread::yield(); // buffer full -- back off, don't hard-spin
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        std::uint64_t value = 0;
        while (received.size() < kItemCount) {
            if (buffer.pop(value)) {
                received.push_back(value);
            } else if (!producer_done.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            // else: producer is done but pop failed -- retry immediately (no yield) to
            // drain any remaining buffered items as fast as possible.
        }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(received.size(), kItemCount);
    for (std::uint64_t i = 0; i < kItemCount; ++i) {
        EXPECT_EQ(received[i], i) << "order violated or item lost/duplicated at index " << i;
    }
}
