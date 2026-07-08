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

Built milestone by milestone. Current: **M3 — foundational containers**: the three
allocation-free data structures `OrderBook` (M4) will be built on — an object pool, an
intrusive doubly-linked list, and an open-addressing hash map — each independently tested,
including a differential stress test against `std::unordered_map` and a real, verified
zero-heap-allocation proof (not just a claim).

| Milestone | Scope | State |
|-----------|-------|-------|
| M1 | Repo skeleton, CMake, CI (gcc+clang, Debug+ASan/UBSan, Release), clang-format | ✅ |
| M2 | ITCH 5.0 parser + synthetic data generator + parser throughput benchmark | ✅ |
| M3 | Object pool, intrusive list, open-addressing hash map | ✅ |
| M4 | OrderBook with ITCH-driven book building + differential tests vs. a reference `std::map` book | ⬜ |
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
