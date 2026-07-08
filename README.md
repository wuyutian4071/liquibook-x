# liquibook-x

> A NASDAQ-style exchange core: an ITCH 5.0 feed handler, a low-latency limit order book, and
> a price-time-priority matching engine in C++20 — built and benchmarked with the same rigor
> real exchange/HFT infrastructure demands.

[![CI](https://github.com/wuyutian4071/liquibook-x/actions/workflows/ci.yml/badge.svg)](https://github.com/wuyutian4071/liquibook-x/actions/workflows/ci.yml)
![C++](https://img.shields.io/badge/C%2B%2B-20-blue)
![License](https://img.shields.io/badge/license-MIT-green)

**What / Why / Results (30-second version)**

- **What:** a real NASDAQ ITCH 5.0 binary protocol parser, a limit order book built for
  nanosecond-scale operations (intrusive containers, custom memory pools, zero hot-path heap
  allocation), and a matching engine supporting limit/market/IOC/FOK orders with price-time
  priority — plus lock-free thread handoff and rigorous latency benchmarking.
- **Why it's different:** this isn't a toy order book. Every performance claim is backed by a
  committed benchmark script (`bench/`), every correctness claim by a test that would actually
  fail if it were wrong (differential testing against a reference implementation, not just
  example-based tests), and the CI matrix runs sanitizer-clean (ASan/UBSan, later TSan) across
  both gcc and clang.
- **Results:** _(populated as milestones land — see `BENCHMARKS.md` once M7 exists)._

## Status

Built milestone by milestone. Current: **M4 — the order book**: a single-symbol limit order
book combining M2's ITCH messages and M3's three containers for the first time — a flat
array of price levels (with a fallback map for outliers), incrementally-tracked best bid/ask,
verified both by direct unit tests and by a differential test against an independent
reference implementation on a random operation stream.

| Milestone | Scope | State |
|-----------|-------|-------|
| M1 | Repo skeleton, CMake, CI (gcc+clang, Debug+ASan/UBSan, Release), clang-format | ✅ |
| M2 | ITCH 5.0 parser + synthetic data generator + parser throughput benchmark | ✅ |
| M3 | Object pool, intrusive list, open-addressing hash map | ✅ |
| M4 | OrderBook with ITCH-driven book building + differential tests vs. a reference `std::map` book | ✅ |
| M5 | MatchingEngine: limit/market/IOC/FOK, price-time priority | ⬜ |
| M6 | Lock-free SPSC ring buffer + two-thread pipeline + TSan | ⬜ |
| M7 | Full benchmark suite + `BENCHMARKS.md` (methodology + results) | ⬜ |
| M8 | Polished README, architecture diagram, design-decisions doc | ⬜ |

## Quickstart

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON -DENABLE_UBSAN=ON
cmake --build build
ctest --test-dir build --output-on-failure

# Parser throughput benchmark (Release, no sanitizers -- their overhead would make the
# numbers meaningless):
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DLIQUIBOOK_BUILD_BENCHMARKS=ON
cmake --build build-release
./build-release/bench/liquibook_bench_parser
```

### The ITCH 5.0 parser (M2)

`itch/decode.hpp` decodes NASDAQ TotalView-ITCH 5.0's binary protocol for the 10 message
types this project needs (System Event, Stock Directory, Add Order with/without MPID, Order
Executed with/without price, Order Cancel, Order Delete, Order Replace, Trade). Every field
is big-endian; `decode()` takes a raw byte span and an already-known type/length (validated
against each type's documented `wire_length()`) and returns a `DecodedMessage` — a raw
`union` of small trivially-copyable payload structs, not `std::variant`, so there's no
vtable, no visitation overhead, nothing beyond max-member-size storage. `noexcept`
throughout: an unrecognized type or a too-short span returns `std::nullopt`, never UB.

`itch/file_reader.hpp`'s `ItchFileReader` memory-maps a length-prefixed ITCH file
(`[2-byte length][message]`, repeated) and walks it with zero copies.

`itch/synth.hpp`'s `generate()` produces a deterministic, seeded synthetic order-flow stream
for testing and benchmarking without needing a real NASDAQ sample file: every Order
Executed/Cancel/Delete/Replace message references an order a prior Add Order in the same
stream actually created and hadn't yet fully removed — checked not by trusting the
generator's own bookkeeping but by replaying the real `decode()` over its output in
`test_synth_roundtrip.cpp` and independently verifying the invariant holds.

**Measured, not assumed**: `bench_parser` reports ~112M messages/sec decode throughput on
this machine (Apple Silicon, Release build, no sanitizers) — decode-only, not the full
file-read+decode+book-update pipeline. A full latency-histogram benchmark suite with
methodology (CPU pinning, warmup, percentiles) arrives at M7.

### Foundational containers (M3)

`containers/object_pool.hpp`'s `ObjectPool<T>` is a fixed-capacity pool that allocates one
contiguous buffer once at construction and never grows. Free slots are tracked via an
intrusive free list stored *in the unused slot memory itself* (a union of `T` and the
free-list index) — no separate bookkeeping array to touch on the hot path. Requires `T`
trivially destructible: a pool for hot-path POD-like types, so there's never destruction
work to run.

`containers/intrusive_list.hpp`'s `IntrusiveList<T>` allocates no nodes at all — a C++20
`concept` requires `T` to have public `prev`/`next` members, enforced with a clear compiler
error rather than left as an undocumented convention. This is what a per-price-level FIFO
order queue is built from directly (M4).

`containers/hash_map.hpp`'s `OpenAddressingHashMap<K, V>` is fixed-capacity (rounded to a
power of two), linear-probed (better cache behavior than double hashing), hashed with a
splitmix64-style mix rather than `std::hash` (whose quality for sequential integer keys like
ITCH order-reference numbers isn't guaranteed), with the standard 3-state slot marker for
tombstone-based deletion and first-tombstone reuse on insert to keep accumulation bounded.
Verified with a differential stress test against a `std::unordered_map` reference (2000
random keys, insert/find/erase, checked at every step).

**Zero heap allocation, actually verified**: `testing/allocation_guard.hpp` is a scoped
allocation counter backed by a global `operator new`/`operator delete` override, used to
directly prove `ObjectPool` and `OpenAddressingHashMap`'s hot-path operations (acquire/
release, insert/find/erase) allocate nothing — the promise design principle #2 below made
back at M1 is now a real, running test, not a comment.

**A real bug the tests caught immediately**, worth keeping as a reminder that this
discipline pays for itself: M4's initial `remove_order_fully()` forgot to decrement a price
level's aggregate share counters when an order was removed via `delete_order`/`replace_order`
— that decrement only happened on the execute/cancel path. 4 of 18 new `OrderBook` unit tests
failed immediately, pinpointing exactly which best-bid/ask transitions were wrong; fixed by
decrementing unconditionally regardless of how an order reaches full removal.

### The order book (M4)

`book/order_book.hpp`'s `OrderBook` is the first module to consume M2's ITCH messages and
M3's three containers together. Price levels live in a flat array indexed by raw
`Price4`-unit offset from a reference price — a real, stated design choice: "tick" here means
one raw `Price4` unit (1/10000 of a dollar), not a market-specific minimum increment, because
M2's synthetic generator doesn't guarantee prices align to any coarser tick, and bucketing by
a market tick without that guarantee would silently collide distinct prices. Prices outside
the flat array's window fall back to a `std::map`. Orders live in M3's `ObjectPool`, indexed
by order reference via M3's `OpenAddressingHashMap`, and rest in per-price-level, per-side
`IntrusiveList` FIFOs — each `Order` carries a back-pointer to its own price level, so
cancel/execute/delete never re-derive a price -> level mapping.

Best bid/ask are cached and updated incrementally, never scanned on the fast path: O(1) when
a new order improves the best, and only a bounded walk (never a full scan) when the *current*
best level's side empties — bounded by the configured flat-array width, not literally O(1) in
the strict worst-case sense, described that way rather than oversold.

Three kinds of verification, matching the milestone's own requirements:
- **Direct unit tests** (`test_order_book.cpp`) — add/execute/cancel/delete/replace
  correctness, best-bid/ask transitions including the level-empties-and-walks case, and the
  flat-array/fallback-map boundary exactly at the edge of the window.
- **A differential test** (`test_order_book_differential.cpp`) against a deliberately simple,
  obviously-correct `ReferenceOrderBook` (plain hash map, O(n) scans) — a random,
  self-consistent stream of operations applied to both books in lockstep, comparing best
  bid/ask, order count, and shares at *every* price the run ever touched (not just top of
  book) after every single operation.
- **An ITCH-driven integration test** (`test_order_book_itch_integration.cpp`) — a real
  synthetic ITCH stream (M2's `generate()`), decoded with M2's real `decode()`, fed through
  `OrderBook::apply()` end to end, checked against independently-tracked expected state.

## Design principles

1. **Correctness first, then measured performance.** No latency claim ships without a
   benchmark in the repo backing it.
2. **Zero heap allocation on the hot path.** Verified, not assumed — object pools and
   intrusive containers throughout; `testing/allocation_guard.hpp`'s counting allocator
   asserts this directly in `containers/tests/` (M3).
3. **No locks on the hot path.** Thread handoff uses a lock-free SPSC ring buffer (M6).
4. **Mechanical sympathy.** Cache-line-aware layout, branch-prediction-conscious code,
   documented and benchmarked, not just asserted in a comment.

## License

MIT — see [LICENSE](LICENSE).
