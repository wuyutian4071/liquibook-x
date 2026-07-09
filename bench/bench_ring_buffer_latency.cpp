#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#include "latency_histogram.hpp"
#include "spsc_ring_buffer.hpp"

using liquibook::bench::LatencyHistogram;
using liquibook::bench::pin_current_thread_to_cpu;
using liquibook::containers::SpscRingBuffer;

namespace {

constexpr std::size_t kWarmupIterations = 10'000;
constexpr std::size_t kMeasuredIterations = 100'000;

void print_report(const char* name, const LatencyHistogram::Percentiles& p) {
    std::printf("%-40s n=%-8zu mean=%8.1fns  p50=%6lldns  p90=%6lldns  p99=%6lldns  "
                "p99.9=%6lldns  max=%8lldns\n",
                name,
                p.sample_count,
                p.mean,
                static_cast<long long>(p.p50),
                static_cast<long long>(p.p90),
                static_cast<long long>(p.p99),
                static_cast<long long>(p.p999),
                static_cast<long long>(p.max));
}

// Single-threaded push/pop latency: push one item, immediately pop it, repeat -- keeps
// occupancy oscillating between 0 and 1 (never full, never a multi-item steady state) while
// still measuring the real cost of each operation in isolation.
void bench_single_threaded_push_pop() {
    SpscRingBuffer<std::uint64_t> ring(1024);

    for (std::size_t i = 0; i < kWarmupIterations; ++i) {
        (void)ring.push(i);
        std::uint64_t out = 0;
        (void)ring.pop(out);
    }

    LatencyHistogram push_hist(kMeasuredIterations);
    LatencyHistogram pop_hist(kMeasuredIterations);
    for (std::size_t i = 0; i < kMeasuredIterations; ++i) {
        const auto pstart = std::chrono::steady_clock::now();
        (void)ring.push(i);
        const auto pend = std::chrono::steady_clock::now();
        push_hist.record(
            std::chrono::duration_cast<std::chrono::nanoseconds>(pend - pstart).count());

        std::uint64_t out = 0;
        const auto ostart = std::chrono::steady_clock::now();
        (void)ring.pop(out);
        const auto oend = std::chrono::steady_clock::now();
        pop_hist.record(
            std::chrono::duration_cast<std::chrono::nanoseconds>(oend - ostart).count());
    }

    print_report("SpscRingBuffer::push", push_hist.compute());
    print_report("SpscRingBuffer::pop", pop_hist.compute());
}

// A real two-thread sustained-throughput run: producer and consumer pinned to *different*
// cores on Linux (best-effort elsewhere), pushing/popping as fast as possible through a
// buffer far smaller than the item count, reporting items/sec -- a throughput number, not a
// latency percentile, deliberately not conflated with the single-threaded numbers above. The
// shutdown handshake mirrors pipeline/itch_pipeline.hpp's own proven approach: an atomic
// "producer done" flag plus one guaranteed re-check after observing it, since a plain
// "empty and done" check alone can race against the producer's very last push.
void bench_two_thread_throughput() {
    constexpr std::size_t kCapacity = 4096;
    constexpr std::uint64_t kItemCount = 20'000'000;

    SpscRingBuffer<std::uint64_t> ring(kCapacity);
    std::atomic<bool> producer_done {false};
    std::atomic<bool> producer_pinned {false};
    std::atomic<bool> consumer_pinned {false};
    std::uint64_t received_count = 0;

    const auto start = std::chrono::steady_clock::now();

    std::thread producer([&] {
        producer_pinned.store(pin_current_thread_to_cpu(0), std::memory_order_relaxed);
        for (std::uint64_t i = 0; i < kItemCount; ++i) {
            while (!ring.push(i)) {
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        consumer_pinned.store(pin_current_thread_to_cpu(1), std::memory_order_relaxed);
        std::uint64_t value = 0;
        for (;;) {
            if (ring.pop(value)) {
                ++received_count;
                continue;
            }
            if (producer_done.load(std::memory_order_acquire)) {
                if (!ring.pop(value)) {
                    break;
                }
                ++received_count;
                continue;
            }
            std::this_thread::yield();
        }
    });

    producer.join();
    consumer.join();
    const auto end = std::chrono::steady_clock::now();

    const double seconds = std::chrono::duration<double>(end - start).count();
    const double items_per_sec = static_cast<double>(received_count) / seconds;

    std::printf("CPU pinning (producer/consumer): %s / %s\n",
                producer_pinned.load() ? "core 0" : "unavailable",
                consumer_pinned.load() ? "core 1" : "unavailable");
    std::printf("SpscRingBuffer two-thread pipeline: %llu items in %.3fs (%.1fM items/sec)\n",
                static_cast<unsigned long long>(received_count),
                seconds,
                items_per_sec / 1e6);
}

} // namespace

int main() {
    const bool pinned = pin_current_thread_to_cpu(0);
    std::printf("CPU pinning: %s\n", pinned ? "active (core 0)" : "unavailable on this platform");
    std::printf("warmup=%zu measured=%zu\n\n", kWarmupIterations, kMeasuredIterations);

    bench_single_threaded_push_pop();
    std::printf("\n");
    bench_two_thread_throughput();

    return 0;
}
