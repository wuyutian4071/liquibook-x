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

Built milestone by milestone. Current: **M6 — concurrency**: a lock-free single-producer/
single-consumer ring buffer decouples ITCH parsing from book-building onto two real OS
threads, with a dedicated ThreadSanitizer CI job verifying the memory ordering is actually
correct, not just "looks right."

| Milestone | Scope | State |
|-----------|-------|-------|
| M1 | Repo skeleton, CMake, CI (gcc+clang, Debug+ASan/UBSan, Release), clang-format | ✅ |
| M2 | ITCH 5.0 parser + synthetic data generator + parser throughput benchmark | ✅ |
| M3 | Object pool, intrusive list, open-addressing hash map | ✅ |
| M4 | OrderBook with ITCH-driven book building + differential tests vs. a reference `std::map` book | ✅ |
| M5 | MatchingEngine: limit/market/IOC/FOK, price-time priority | ✅ |
| M6 | Lock-free SPSC ring buffer + two-thread pipeline + TSan | ✅ |
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

### The matching engine (M5)

`engine/matching_engine.hpp`'s `MatchingEngine` decides how an *incoming* order interacts
with the book — the piece M4 deliberately didn't build: `OrderBook` maintains resting state,
but has no notion of "does this new order cross, and against whom." `MatchingEngine` walks
the opposite side from `OrderBook`'s (M5-added) `best_bid_level()`/`best_ask_level()`, checks
the price still crosses (skipped for Market, which crosses at any price), and takes resting
orders in FIFO order via the existing `IntrusiveList` iteration — reusing `OrderBook`'s own
`execute_order()`/`add_order()` for every state change rather than touching `Order`/
`PriceLevel` internals directly. Four order types, each with its own remainder rule: `Limit`
rests any unfilled quantity; `Market` and `IOC` discard it; `FOK` fills completely or not at
all.

**Fill-Or-Kill's atomicity** is the one that needs its own explanation: before executing
anything, a read-only `available_liquidity()` pass sums matchable resting shares across
*multiple* price levels via a new `OrderBook::next_level_after()` — deliberately not
`best_ask_level()` re-queried in a loop, which would return the same unconsumed level forever
since a dry run never actually executes. Only if that confirms enough liquidity does the real
(mutating) walk run; otherwise the order is rejected with zero fills and — verified directly
in tests, not inferred — zero change to any `OrderBook` observable at all.

**Self-trade prevention** is off by default (a resting and an incoming order from the same
`trader_id` — a plain `uint32_t`, 0 meaning "no trader," added to `Order` in M5 for this
purpose alone) can match freely. Switched on, a blocked resting order is *skipped*, not
cancelled: the incoming order matches the next eligible order in the queue, even if that
means a later-arrived order at the same price trades first — a real, standard exception to
strict FIFO, not a bug, and the skipped order is still resting, untouched, afterward.

The callback interface (`FillEvent`/`RestEvent`/`KillEvent`, in `engine/events.hpp`) is a
compile-time template parameter, not `std::function` and not CRTP inheritance: a plain object
providing `on_fill`/`on_rest`/`on_kill`, invoked directly with no vtable — simpler to use in
tests (pass a handler object, no need to inherit from the engine) while still satisfying "not
`std::function` on the hot path."

**A real bug the tests caught immediately**, the second one this project's differential/
scenario-testing discipline has caught (see M4's above): the first `available_liquidity()`
called `next_level_after(price, order.is_buy)` — but that parameter selects which *book* side
to scan (bids vs. asks), not which side the incoming order itself is on. A buy order matches
against asks, so the correct call is `next_level_after(price, !order.is_buy)`. The very first
multi-level FOK test failed immediately (0 fills instead of the expected 2, across two price
levels) because the liquidity check walked into the empty bid side after the first ask level
and gave up early. Fixed and commented with the exact reasoning.

Verification: 15 direct scenario tests (partial/exact fills, multi-level walks, FIFO priority
verified via fill order not just final state, Market/IOC partial-fill-then-discard, FOK
success/failure/price-limit-respected, self-trade prevention on/off/no-other-liquidity/zero-
trader-id, and STP's effect on the FOK liquidity check specifically) plus a conservation test
(`test_matching_engine_conservation.cpp`) — 500 random orders across all four types, asserting
every single `submit()` call's incoming quantity is accounted for exactly once as filled,
rested, or killed, a property that must hold for *any* correct matching engine regardless of
implementation strategy.

### Concurrency: the SPSC ring buffer and two-thread pipeline (M6)

`containers/spsc_ring_buffer.hpp`'s `SpscRingBuffer<T>` is this project's first genuinely
concurrent code. Single-producer/single-consumer is the simplest lock-free case: because
exactly one thread ever writes each index, `push`/`pop` need only plain atomic load/store
with acquire/release ordering — no compare-exchange, no `fetch_add`, no retry loop, since
that machinery exists specifically to arbitrate between *multiple* writers to the same index
(MPSC/MPMC), which doesn't apply here. `write_index_`/`read_index_` are each padded onto
their own 64-byte cache line — the first concrete delivery on design principle #4
("mechanical sympathy... not just asserted in a comment"), since without it the producer and
consumer would false-share one line and force coherence traffic between cores on every single
push/pop even though the two threads never touch each other's index. Both indices increase
monotonically forever rather than wrapping (only the physical slot wraps, via a mask) — the
standard technique that avoids the "does `read == write` mean empty or full?" ambiguity naive
circular buffers hit.

`pipeline/itch_pipeline.hpp`'s `run_itch_pipeline()` puts the ring buffer to real use: a
producer thread decodes a real ITCH stream with M2's `decode()` and pushes each message into
the buffer; a consumer thread pops and applies each one to an `OrderBook` via M4's real
`apply()` — decoupling parsing from book-building onto separate threads, the realistic
architecture a real feed handler uses so a slow matching/book-building thread never blocks
the parser, and vice versa.

**Verified where it counts**: a dedicated `tsan` CI job (`.github/workflows/ci.yml`) builds
the whole suite with `-fsanitize=thread` and runs it on real Linux hardware — this is what
actually proves the acquire/release reasoning above is correct, not just plausible-sounding.
The concurrent tests themselves are deliberately adversarial: `SpscRingBuffer`'s own stress
test drives 200,000 items through a capacity-64 buffer (constant wraparound, not a rare edge
case); the pipeline test uses a capacity-32 buffer so the two threads genuinely contend rather
than the producer racing ahead and finishing first.

**Two real, environment-specific bugs this process caught**, worth keeping as a reminder of
why CI-only verification exists: (1) TSan's own runtime ships its own `operator new`/`delete`
overrides, which collided at *link time* with M3's `allocation_guard.cpp` (a genuine ODR
violation, not a false alarm) — fixed by gating `allocation_guard.cpp`'s overrides behind a
`LIQUIBOOK_UNDER_TSAN` macro, with the two dependent zero-allocation tests `GTEST_SKIP()`ing
cleanly under TSan rather than silently always passing with a counter nothing feeds anymore.
(2) That macro's first version used `defined(__has_feature) && __has_feature(...)`, which
fails to *parse* on GCC (GCC doesn't know `__has_feature` at all, and short-circuiting a `&&`
applies to its *value*, not to whether the right operand needs to be syntactically valid) —
fixed with the portable `#define __has_feature(x) 0` fallback idiom several major C++
codebases use for exactly this. A third, non-environment-specific bug also surfaced while
wiring the pipeline together: `hash_map.hpp` and the ring buffer both defined
`liquibook::containers::detail::round_up_to_power_of_two` — harmless individually, but a
genuine redefinition once a consumer (the pipeline) included both in one translation unit;
fixed by giving the ring buffer's copy its own, differently-named inner namespace.

## Design principles

1. **Correctness first, then measured performance.** No latency claim ships without a
   benchmark in the repo backing it.
2. **Zero heap allocation on the hot path.** Verified, not assumed — object pools and
   intrusive containers throughout; `testing/allocation_guard.hpp`'s counting allocator
   asserts this directly in `containers/tests/` (M3).
3. **No locks on the hot path.** Thread handoff uses a lock-free SPSC ring buffer, TSan-
   verified in CI (M6).
4. **Mechanical sympathy.** Cache-line-aware layout, branch-prediction-conscious code,
   documented and benchmarked, not just asserted in a comment — the SPSC ring buffer's
   cache-line-padded indices (M6) are the first concrete instance of this, not just a claim.

## License

MIT — see [LICENSE](LICENSE).
