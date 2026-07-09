#include <benchmark/benchmark.h>

#include <chrono>
#include <cstdio>
#include <random>

#include "latency_histogram.hpp"
#include "order_book.hpp"

using liquibook::bench::LatencyHistogram;
using liquibook::bench::pin_current_thread_to_cpu;
using liquibook::book::OrderBook;
namespace itch = liquibook::itch;

namespace {

constexpr itch::Price4 kRef = 1'000'000;
constexpr std::size_t kHalfWidth = 5'000;
constexpr std::size_t kWarmupIterations = 10'000;
constexpr std::size_t kMeasuredIterations = 100'000;

void print_report(const char* name, const LatencyHistogram::Percentiles& p) {
    std::printf("%-28s n=%-8zu mean=%8.1fns  p50=%6lldns  p90=%6lldns  p99=%6lldns  "
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

// add_order() across the full warmup+measured run: capacity is sized generously so the book
// keeps growing throughout rather than needing cleanup mid-run. This deliberately shows
// steady-state behavior as occupancy increases, not an artificially-reset-every-iteration
// number.
void bench_add_order() {
    const std::size_t total_iterations = kWarmupIterations + kMeasuredIterations;
    OrderBook book(kRef, kHalfWidth, total_iterations + 10);

    std::mt19937_64 rng(42);
    std::uniform_int_distribution<std::int32_t> price_jitter(-4000, 4000);
    std::uniform_int_distribution<int> side_dist(0, 1);

    // Pre-generate every operation's inputs outside the timed region -- RNG cost must not
    // pollute the per-operation latency being measured.
    std::vector<itch::Price4> prices(total_iterations);
    std::vector<bool> sides(total_iterations);
    for (std::size_t i = 0; i < total_iterations; ++i) {
        prices[i] = static_cast<itch::Price4>(static_cast<std::int64_t>(kRef) + price_jitter(rng));
        sides[i] = side_dist(rng) == 0;
    }

    for (std::size_t i = 0; i < kWarmupIterations; ++i) {
        book.add_order(i + 1, prices[i], 100, sides[i]);
    }

    LatencyHistogram hist(kMeasuredIterations);
    for (std::size_t i = 0; i < kMeasuredIterations; ++i) {
        const std::size_t idx = kWarmupIterations + i;
        const auto start = std::chrono::steady_clock::now();
        book.add_order(idx + 1, prices[idx], 100, sides[idx]);
        const auto end = std::chrono::steady_clock::now();
        hist.record(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    }

    print_report("OrderBook::add_order", hist.compute());
}

// cancel_order()/execute_order() need a pre-existing resting order each iteration -- the
// setup (adding that order) is deliberately *outside* the timed region so only the
// operation under test is measured.
void bench_cancel_and_execute() {
    const std::size_t total_iterations = kWarmupIterations + kMeasuredIterations;
    OrderBook cancel_book(kRef, kHalfWidth, total_iterations + 10);
    OrderBook execute_book(kRef, kHalfWidth, total_iterations + 10);

    std::mt19937_64 rng(7);
    std::uniform_int_distribution<std::int32_t> price_jitter(-4000, 4000);

    std::vector<itch::Price4> prices(total_iterations);
    for (std::size_t i = 0; i < total_iterations; ++i) {
        prices[i] = static_cast<itch::Price4>(static_cast<std::int64_t>(kRef) + price_jitter(rng));
    }

    for (std::size_t i = 0; i < kWarmupIterations; ++i) {
        cancel_book.add_order(i + 1, prices[i], 100, true);
        cancel_book.cancel_order(i + 1, 50);
        execute_book.add_order(i + 1, prices[i], 100, true);
        execute_book.execute_order(i + 1, 50);
    }

    LatencyHistogram cancel_hist(kMeasuredIterations);
    LatencyHistogram execute_hist(kMeasuredIterations);
    for (std::size_t i = 0; i < kMeasuredIterations; ++i) {
        const std::size_t idx = kWarmupIterations + i;
        const itch::OrderRef ref = idx + 1;

        cancel_book.add_order(ref, prices[idx], 100, true);
        const auto cstart = std::chrono::steady_clock::now();
        cancel_book.cancel_order(ref, 50);
        const auto cend = std::chrono::steady_clock::now();
        cancel_hist.record(
            std::chrono::duration_cast<std::chrono::nanoseconds>(cend - cstart).count());
        cancel_book.cancel_order(ref, 50); // fully remove so it doesn't accumulate

        execute_book.add_order(ref, prices[idx], 100, true);
        const auto estart = std::chrono::steady_clock::now();
        execute_book.execute_order(ref, 50);
        const auto eend = std::chrono::steady_clock::now();
        execute_hist.record(
            std::chrono::duration_cast<std::chrono::nanoseconds>(eend - estart).count());
        execute_book.execute_order(ref, 50);
    }

    print_report("OrderBook::cancel_order", cancel_hist.compute());
    print_report("OrderBook::execute_order", execute_hist.compute());
}

// best_bid()/shares_at() on a book with a realistic number of resting orders -- verifying
// the O(1) claim (README's own "not just asserted in a comment") isn't just a comment here
// either.
void bench_queries() {
    constexpr std::size_t kRestingOrders = 5'000;
    OrderBook book(kRef, kHalfWidth, kRestingOrders + 10);

    std::mt19937_64 rng(99);
    std::uniform_int_distribution<std::int32_t> price_jitter(-4000, 4000);
    for (std::size_t i = 0; i < kRestingOrders; ++i) {
        const auto price =
            static_cast<itch::Price4>(static_cast<std::int64_t>(kRef) + price_jitter(rng));
        book.add_order(i + 1, price, 100, i % 2 == 0);
    }

    for (std::size_t i = 0; i < kWarmupIterations; ++i) {
        auto warm_result = book.best_bid();
        benchmark::DoNotOptimize(warm_result);
    }

    LatencyHistogram best_bid_hist(kMeasuredIterations);
    for (std::size_t i = 0; i < kMeasuredIterations; ++i) {
        const auto start = std::chrono::steady_clock::now();
        auto result = book.best_bid();
        const auto end = std::chrono::steady_clock::now();
        benchmark::DoNotOptimize(result);
        best_bid_hist.record(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    }

    LatencyHistogram shares_at_hist(kMeasuredIterations);
    for (std::size_t i = 0; i < kMeasuredIterations; ++i) {
        const auto start = std::chrono::steady_clock::now();
        auto result = book.shares_at(kRef, true);
        const auto end = std::chrono::steady_clock::now();
        benchmark::DoNotOptimize(result);
        shares_at_hist.record(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    }

    print_report("OrderBook::best_bid", best_bid_hist.compute());
    print_report("OrderBook::shares_at", shares_at_hist.compute());
}

} // namespace

int main() {
    const bool pinned = pin_current_thread_to_cpu(0);
    std::printf("CPU pinning: %s\n", pinned ? "active (core 0)" : "unavailable on this platform");
    std::printf("warmup=%zu measured=%zu\n\n", kWarmupIterations, kMeasuredIterations);

    bench_add_order();
    bench_cancel_and_execute();
    bench_queries();

    return 0;
}
