#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

#ifdef __linux__
#include <pthread.h>
#endif

namespace liquibook::bench {

// Bench-only infrastructure -- not part of the core libraries, matching bench/'s existing
// self-contained role (mirrors bench_parser.cpp's own scope). Google Benchmark's own model
// times a whole loop body across many iterations and reports mean/median across
// *repetitions* of that timed region; it doesn't naturally produce a percentile distribution
// of *individual operation* latencies, which is what "P50/P99/P99.9" actually means. This
// records one raw sample per operation and computes percentiles directly.
class LatencyHistogram {
public:
    // Reserves capacity upfront so record() never allocates during a timed region.
    explicit LatencyHistogram(std::size_t capacity_hint) { samples_.reserve(capacity_hint); }

    void record(std::int64_t nanoseconds) { samples_.push_back(nanoseconds); }

    struct Percentiles {
        std::int64_t p50 = 0;
        std::int64_t p90 = 0;
        std::int64_t p99 = 0;
        std::int64_t p999 = 0;
        std::int64_t max = 0;
        double mean = 0.0;
        std::size_t sample_count = 0;
    };

    // Sorts a copy rather than mutating internal state, so this can safely be called more
    // than once (e.g. once for a live sanity check, once for the final report) without
    // affecting record()'s append-only invariant.
    [[nodiscard]] Percentiles compute() const {
        Percentiles result;
        result.sample_count = samples_.size();
        if (samples_.empty()) {
            return result;
        }

        std::vector<std::int64_t> sorted = samples_;
        std::sort(sorted.begin(), sorted.end());

        result.p50 = percentile_at(sorted, 0.50);
        result.p90 = percentile_at(sorted, 0.90);
        result.p99 = percentile_at(sorted, 0.99);
        result.p999 = percentile_at(sorted, 0.999);
        result.max = sorted.back();
        result.mean =
            std::accumulate(sorted.begin(), sorted.end(), 0.0) / static_cast<double>(sorted.size());
        return result;
    }

private:
    [[nodiscard]] static std::int64_t percentile_at(const std::vector<std::int64_t>& sorted,
                                                    double p) {
        const auto index = static_cast<std::size_t>(p * static_cast<double>(sorted.size() - 1));
        return sorted[index];
    }

    std::vector<std::int64_t> samples_;
};

// Best-effort CPU pinning for the calling thread. Real on Linux via
// pthread_setaffinity_np; a documented no-op everywhere else -- macOS has no public
// equivalent thread-affinity API (thread_policy_set's affinity tags are a grouping hint, not
// a guarantee), so this is a genuine platform gap, not hidden behind a fake success. Returns
// whether pinning actually took effect, so callers (and BENCHMARKS.md) can report honestly
// rather than silently assuming it worked.
[[nodiscard]] inline bool pin_current_thread_to_cpu([[maybe_unused]] int cpu_id) noexcept {
#ifdef __linux__
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);
    CPU_SET(cpu_id, &cpu_set);
    return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpu_set) == 0;
#else
    return false;
#endif
}

} // namespace liquibook::bench
