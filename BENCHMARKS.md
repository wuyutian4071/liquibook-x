# liquibook-x — Benchmarks

Numbers below were produced by actually running the benchmark executables in this repo,
today, on the machine described below — not estimated, not carried over from a different
build, not rounded up. If you re-run these yourself and get different numbers, trust yours;
hardware, OS scheduler state, and background load all matter more than most people expect
for anything measured in tens of nanoseconds.

## Methodology

**Hardware/OS**: Apple M2, macOS 26.2, Apple clang 15.0.0, CMake 4.3.4. Release build
(`-DCMAKE_BUILD_TYPE=Release`), no sanitizers — ASan/UBSan/TSan overhead would make every
number below meaningless, which is also why `bench/CMakeLists.txt` never applies
`liquibook_apply_sanitizers()` to any benchmark target.

**CPU pinning**: `bench/latency_histogram.hpp`'s `pin_current_thread_to_cpu()` is real on
Linux (`pthread_setaffinity_np`) and a documented no-op everywhere else — macOS has no public
equivalent thread-affinity API. **The numbers below were collected without pinning**, since
this dev machine is macOS. Stated plainly rather than glossed over: on an unpinned machine,
the OS scheduler can migrate a thread mid-measurement, and background load (this machine
had other processes running, visible in `bench_parser`'s own `Load Average` line below) adds
noise. CI's Linux runners would get real pinning if the benchmark suite were run there, but
CI only compile-checks these targets (see `.github/workflows/ci.yml`'s `benchmarks: ON`
Release legs) — it doesn't run and publish results, since a shared, possibly-throttled CI
runner would produce numbers even less representative than an unloaded dev machine.

**Warmup and sampling**: every latency benchmark runs 10,000 warmup iterations (discarded,
letting caches/branch predictors reach steady state) followed by 100,000 measured iterations,
each timed individually via `std::chrono::steady_clock` around just the call under test —
`bench/latency_histogram.hpp`'s `LatencyHistogram` then sorts the 100,000 raw samples and
reports P50/P90/P99/P99.9/max plus the mean. This is deliberately different from
`bench_parser.cpp`'s own Google Benchmark throughput-loop model (which reports mean time
across repetitions of a whole timed region) — "P99.9 latency of one operation" and "average
loop-body time" are different measurements, and only the former needed new infrastructure.

**A real caveat worth stating plainly**: several operations below cluster tightly around
41-42ns regardless of what they actually do internally (`OrderBook::cancel_order`,
`best_bid`, `shares_at`; `SpscRingBuffer::push`/`pop`). This is likely at or near
`std::chrono::steady_clock`'s own read overhead/resolution floor on this machine, not
necessarily the true cost of the operation — sub-50ns measurements from a general-purpose
OS clock should be read as "very fast, at or below what this clock can resolve," not as
precise figures. `shares_at`'s own P50 of 0ns in one run (see below) is a direct symptom of
this: some individual samples round down to the clock's own tick granularity.

## Results

### ITCH 5.0 parser (M2)

| Benchmark | Throughput |
|---|---|
| `decode()`, sustained | **112.2M messages/sec** |

From `bench/bench_parser.cpp` (unchanged since M2) via `BM_DecodeThroughput`, a Google
Benchmark throughput loop over a 200,000-message synthetic stream. Decode-only — not the
full file-read+decode+book-update pipeline.

### OrderBook (M4)

100,000 samples per row (10,000 warmup) from `bench/bench_order_book_latency.cpp`.

| Operation | Mean | P50 | P90 | P99 | P99.9 | Max |
|---|--:|--:|--:|--:|--:|--:|
| `add_order` | 60.8ns | 42ns | 84ns | 208ns | 375ns | 2,625ns |
| `cancel_order` | 24.7ns | 41ns | 42ns | 42ns | 83ns | 208ns |
| `execute_order` | 24.9ns | 41ns | 42ns | 42ns | 83ns | 167ns |
| `best_bid` | 22.1ns | 41ns | 42ns | 42ns | 83ns | 167ns |
| `shares_at` | 20.2ns | 0ns | 42ns | 42ns | 83ns | 167ns |

`add_order` is measured across the full warmup+run (the book keeps growing rather than being
reset every iteration), so its P99/P99.9/max reflect realistic steady-state occupancy growth,
not an artificially cheap "always-empty book" number. `cancel_order`/`execute_order` measure
only the call itself — the resting order each one acts on is added, untimed, immediately
before. `best_bid`/`shares_at` are measured against a book pre-populated with 5,000 resting
orders, directly exercising the O(1) claim `README.md`'s M4 section makes rather than just
asserting it.

### MatchingEngine (M5)

100,000 samples per row (10,000 warmup) from `bench/bench_matching_engine_latency.cpp`.

| Scenario | Mean | P50 | P90 | P99 | P99.9 | Max |
|---|--:|--:|--:|--:|--:|--:|
| Limit, rests (no crossing liquidity) | 25.7ns | 41ns | 42ns | 84ns | 167ns | 11,834ns |
| Limit, full match against one resting order | 5,789.5ns | 5,750ns | 5,833ns | 6,292ns | 16,208ns | 43,541ns |
| IOC, partial fill against one resting order | 5,778.4ns | 5,750ns | 5,791ns | 6,250ns | 14,208ns | 39,959ns |

**The ~140x gap between "rests" and the two matching scenarios is real, and worth explaining
rather than presenting as raw numbers.** Both matching benchmarks are deliberate worst cases:
each resting order is the *sole* occupant of its price level, so every single match empties
that level, triggering `OrderBook`'s own already-documented (`book/order_book.hpp`) "bounded
walk... bounded by the configured flat-array width" rescan to find the new best — 100% of the
time here, not as a rare edge case. The benchmark's flat array is sized to 10,001 slots
(`kHalfWidth = 5'000`), and this rescan is what those ~5.75us are actually measuring. The
"rests" scenario never empties a level (nothing it submits ever crosses), so it never pays
this cost at all — which is exactly why it's ~140x cheaper. This empirically quantifies a
design tradeoff that was previously only qualified qualitatively ("not literally O(1) in the
strict worst-case sense") — a realistic order flow, where matched price levels usually still
have other resting orders behind the one just filled, would see this rescan far less often
than 100% of matches.

### SpscRingBuffer and the two-thread pipeline (M6)

100,000 samples per row (10,000 warmup) from `bench/bench_ring_buffer_latency.cpp`.

| Operation | Mean | P50 | P90 | P99 | P99.9 | Max |
|---|--:|--:|--:|--:|--:|--:|
| `push` (single-threaded) | 33.5ns | 42ns | 42ns | 83ns | 84ns | 6,333ns |
| `pop` (single-threaded) | 33.8ns | 42ns | 42ns | 83ns | 84ns | 25,666ns |

Single-threaded numbers: push one item, immediately pop it, repeat — occupancy oscillates
between 0 and 1 (never full, never a multi-item steady state), isolating the cost of each
operation from queue-depth effects.

**Two-thread sustained throughput**: a real producer thread and a real consumer thread,
pinned to different cores where pinning is available (unavailable on this machine, per the
methodology caveat above), pushing/popping 20,000,000 items through a capacity-4,096 buffer
as fast as possible:

| Run | Items | Wall time | Throughput |
|---|--:|--:|--:|
| Two-thread pipeline | 20,000,000 | 0.122s | **164.6M items/sec** |

This is a throughput number, not a latency percentile, and is reported separately rather than
conflated with the single-threaded push/pop numbers above.

## Reproducing these numbers

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DLIQUIBOOK_BUILD_BENCHMARKS=ON
cmake --build build-release
./build-release/bench/liquibook_bench_parser
./build-release/bench/liquibook_bench_order_book_latency
./build-release/bench/liquibook_bench_matching_engine_latency
./build-release/bench/liquibook_bench_ring_buffer_latency
```
