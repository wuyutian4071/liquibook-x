#include <chrono>
#include <cstdio>

#include "latency_histogram.hpp"
#include "matching_engine.hpp"
#include "order_book.hpp"

using liquibook::bench::LatencyHistogram;
using liquibook::bench::pin_current_thread_to_cpu;
using liquibook::book::OrderBook;
using liquibook::engine::IncomingOrder;
using liquibook::engine::MatchingEngine;
using liquibook::engine::OrderType;
namespace itch = liquibook::itch;

namespace {

constexpr itch::Price4 kRef = 1'000'000;
constexpr std::size_t kHalfWidth = 5'000;
constexpr std::size_t kWarmupIterations = 10'000;
constexpr std::size_t kMeasuredIterations = 100'000;

// Discards every event -- the benchmark measures submit() latency, not event delivery.
struct NullHandler {
    void on_fill(const liquibook::engine::FillEvent&) {}
    void on_rest(const liquibook::engine::RestEvent&) {}
    void on_kill(const liquibook::engine::KillEvent&) {}
};

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

// A Limit buy that fully matches exactly one resting ask at the same price and quantity --
// the resting order is added (untimed) immediately before each timed submit(), so the book
// never accumulates and every iteration measures the same "one clean match" shape.
//
// This is a deliberate WORST case, not a typical one, and the numbers below should be read
// that way: because the resting order is always the sole occupant of its price level, every
// single match empties that level, triggering OrderBook's own documented (book/order_book.hpp)
// "bounded walk... bounded by the configured flat-array width" rescan to find the new best --
// 100% of the time here, not as a rare edge case. Contrast against bench_rests_no_match()
// below, which never empties a level and never pays this cost at all. The gap between the two
// (empirically, tens of microseconds vs. tens of nanoseconds on this machine) is the real,
// measured cost of that documented design tradeoff, not a bug in this benchmark.
void bench_immediate_full_match() {
    const std::size_t total_iterations = kWarmupIterations + kMeasuredIterations;
    OrderBook book(kRef, kHalfWidth, total_iterations + 10);
    NullHandler handler;
    MatchingEngine engine(book, handler);

    itch::OrderRef next_resting_ref = 1;
    itch::OrderRef next_incoming_ref = total_iterations + 1;

    auto run_one = [&] {
        book.add_order(next_resting_ref++, kRef, 100, /*is_buy=*/false);
        IncomingOrder order {next_incoming_ref++, kRef, 100, true, OrderType::Limit};
        return order;
    };

    for (std::size_t i = 0; i < kWarmupIterations; ++i) {
        engine.submit(run_one());
    }

    LatencyHistogram hist(kMeasuredIterations);
    for (std::size_t i = 0; i < kMeasuredIterations; ++i) {
        const IncomingOrder order = run_one();
        const auto start = std::chrono::steady_clock::now();
        engine.submit(order);
        const auto end = std::chrono::steady_clock::now();
        hist.record(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    }

    print_report("MatchingEngine::submit (full match)", hist.compute());
}

// A Limit buy with no crossing liquidity at all -- every incoming order simply rests. All
// orders submitted on the same side (buy) so nothing ever crosses, regardless of how many
// already rest; the book grows across the run, showing steady-state behavior as occupancy
// increases rather than an artificially-reset number (matches bench_order_book_latency's
// add_order methodology).
void bench_rests_no_match() {
    const std::size_t total_iterations = kWarmupIterations + kMeasuredIterations;
    OrderBook book(kRef, kHalfWidth, total_iterations + 10);
    NullHandler handler;
    MatchingEngine engine(book, handler);

    for (std::size_t i = 0; i < kWarmupIterations; ++i) {
        engine.submit(IncomingOrder {i + 1, kRef, 100, true, OrderType::Limit});
    }

    LatencyHistogram hist(kMeasuredIterations);
    for (std::size_t i = 0; i < kMeasuredIterations; ++i) {
        const itch::OrderRef ref = kWarmupIterations + i + 1;
        const IncomingOrder order {ref, kRef, 100, true, OrderType::Limit};
        const auto start = std::chrono::steady_clock::now();
        engine.submit(order);
        const auto end = std::chrono::steady_clock::now();
        hist.record(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    }

    print_report("MatchingEngine::submit (rests, no match)", hist.compute());
}

// An IOC buy for more than a resting ask can fill -- partial fill against the resting
// order, remainder discarded. The resting order (50 shares) is added (untimed) immediately
// before each timed submit() of a 100-share IOC, so every iteration measures the same
// "partial fill + kill remainder" shape without the book accumulating.
//
// Same worst-case shape as bench_immediate_full_match() above (and for the same reason): the
// resting order is fully consumed and is the sole occupant of its level, so this also pays
// the level-emptying rescan cost on every iteration.
void bench_ioc_partial_fill() {
    const std::size_t total_iterations = kWarmupIterations + kMeasuredIterations;
    OrderBook book(kRef, kHalfWidth, total_iterations + 10);
    NullHandler handler;
    MatchingEngine engine(book, handler);

    itch::OrderRef next_resting_ref = 1;
    itch::OrderRef next_incoming_ref = total_iterations + 1;

    auto run_one = [&] {
        book.add_order(next_resting_ref++, kRef, 50, /*is_buy=*/false);
        IncomingOrder order {next_incoming_ref++, kRef, 100, true, OrderType::IOC};
        return order;
    };

    for (std::size_t i = 0; i < kWarmupIterations; ++i) {
        engine.submit(run_one());
    }

    LatencyHistogram hist(kMeasuredIterations);
    for (std::size_t i = 0; i < kMeasuredIterations; ++i) {
        const IncomingOrder order = run_one();
        const auto start = std::chrono::steady_clock::now();
        engine.submit(order);
        const auto end = std::chrono::steady_clock::now();
        hist.record(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    }

    print_report("MatchingEngine::submit (IOC partial fill)", hist.compute());
}

} // namespace

int main() {
    const bool pinned = pin_current_thread_to_cpu(0);
    std::printf("CPU pinning: %s\n", pinned ? "active (core 0)" : "unavailable on this platform");
    std::printf("warmup=%zu measured=%zu\n\n", kWarmupIterations, kMeasuredIterations);

    bench_immediate_full_match();
    bench_rests_no_match();
    bench_ioc_partial_fill();

    return 0;
}
