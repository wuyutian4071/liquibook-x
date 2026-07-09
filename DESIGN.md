# liquibook-x — Design Decisions

`README.md` covers what each milestone built and how it's verified. This document covers
*why* — organized by decision, not by milestone, for anyone who wants the reasoning without
reading the build history in order. Performance numbers backing several of these decisions
live in [`BENCHMARKS.md`](BENCHMARKS.md).

## Table of contents

- [ITCH messages: a raw union, not `std::variant`](#itch-messages-a-raw-union-not-stdvariant)
- [Price levels: flat array + fallback map, and what "tick" means here](#price-levels-flat-array--fallback-map-and-what-tick-means-here)
- [Zero-allocation containers: intrusive lists and object pools](#zero-allocation-containers-intrusive-lists-and-object-pools)
- [The hash map: open addressing, not chaining](#the-hash-map-open-addressing-not-chaining)
- [Best bid/ask: an incremental cache with a bounded rescan](#best-bidask-an-incremental-cache-with-a-bounded-rescan)
- [MatchingEngine's callback: a compile-time template, not `std::function`/CRTP](#matchingengines-callback-a-compile-time-template-not-stdfunctioncrtp)
- [Self-trade prevention: skip, not cancel](#self-trade-prevention-skip-not-cancel)
- [Fill-Or-Kill: a read-only dry run before any mutation](#fill-or-kill-a-read-only-dry-run-before-any-mutation)
- [The SPSC ring buffer: no CAS, cache-line padding, monotonic indices](#the-spsc-ring-buffer-no-cas-cache-line-padding-monotonic-indices)
- [Bugs the process caught](#bugs-the-process-caught)

## ITCH messages: a raw union, not `std::variant`

`itch/messages.hpp`'s `DecodedMessage` holds one of ten payload structs in a raw `union`,
not a `std::variant`. A `std::variant` would add a discriminant-check-and-visit overhead and
generally sits at `sizeof(largest_alternative) + alignment`, but the bigger reason is that
this codebase's own header (`MessageHeader`) already carries the type tag — a `variant`
would mean paying for a *second* discriminant. `decode()` returns `std::optional<DecodedMessage>`
and is `noexcept` throughout: an unrecognized type or a too-short span produces `std::nullopt`,
never undefined behavior, so the raw union's lack of `variant`'s safety net (no
`std::bad_variant_access`, no compiler-enforced exhaustive visitation) is never actually a
gap in practice — every accessor (`as_add_order()`, etc.) is only ever called after `decode()`
has already confirmed the type via the header.

## Price levels: flat array + fallback map, and what "tick" means here

`book/order_book.hpp`'s `OrderBook` stores price levels in a flat `std::vector<PriceLevel>`
indexed by raw `Price4`-unit offset from a configured reference price, with a `std::map`
fallback for prices outside that window. "Tick" here means one raw `Price4` unit — 1/10000
of a dollar — not a market-specific minimum price increment. That's a deliberate choice, not
an oversight: `itch/synth.hpp`'s synthetic generator doesn't guarantee prices align to any
coarser tick size, and bucketing by a market tick without that guarantee would silently
collide distinct prices into the same bucket. The flat array gives O(1) access for the
common case (prices clustered near the reference price, which is how real order flow
behaves); the map fallback means an outlier price never breaks correctness, only degrades
that one lookup to O(log n) — an explicit, bounded cost, not a hidden one.

## Zero-allocation containers: intrusive lists and object pools

`containers/object_pool.hpp`'s `ObjectPool<T>` allocates one contiguous buffer at
construction and never grows; free slots are tracked via an intrusive free list stored *in
the unused slot memory itself* (a `union` of `T` and the free-list index), so there's no
separate bookkeeping array to touch on the hot path. It requires `T` to be trivially
destructible — every type it's used for in this project (`book::Order`) is POD-like, so
there's never destruction work to skip.

`containers/intrusive_list.hpp`'s `IntrusiveList<T>` allocates no nodes at all: a C++20
`concept` requires `T` to have public `prev`/`next` members, enforced with a clear compiler
error rather than left as an undocumented convention a caller could get wrong silently. This
is what each price level's per-side FIFO order queue is built from directly.

Both exist because `std::list`/`std::unordered_map` would allocate per-node on the hot path
that `testing/allocation_guard.hpp` (below) exists specifically to catch.

**Zero heap allocation, actually verified, not assumed**: `testing/allocation_guard.hpp` is a
scoped allocation counter backed by a global `operator new`/`operator delete` override, used
to directly prove `ObjectPool` and `OpenAddressingHashMap`'s hot-path operations (acquire/
release, insert/find/erase) allocate nothing. This is design principle #2 (see `README.md`)
turned into a real, running test rather than a comment.

## The hash map: open addressing, not chaining

`containers/hash_map.hpp`'s `OpenAddressingHashMap<K, V>` is fixed-capacity (rounded to a
power of two), linear-probed, and hashed with a splitmix64-style avalanche mix rather than
`std::hash` — whose quality for sequential integer keys like ITCH order-reference numbers
(which increment roughly monotonically) isn't guaranteed, and a poor mix would cluster badly
against a power-of-two table size specifically. Linear probing was chosen over double hashing
for its sequential memory access pattern on collision (better cache behavior than double
hashing's scattered probes) and over chaining because chaining means a heap allocation per
node — exactly what this project's "zero heap allocation on the hot path" principle rules
out. Deletion uses the standard 3-state slot marker (empty/occupied/tombstone) with
first-tombstone-reuse on insert, so repeated erase/reinsert cycles on the same key don't
cause tombstone accumulation to grow without bound.

Verified with a differential stress test against `std::unordered_map` (2,000 random keys,
insert/find/erase, checked at every step) — the reference implementation is deliberately the
*simple*, obviously-correct one; the optimized implementation is what gets checked against it,
never the reverse.

## Best bid/ask: an incremental cache with a bounded rescan

`OrderBook` never scans for the best bid/ask on the fast path: a cached price updates in
O(1) whenever a new order improves it, and only walks to find the next-best price when the
*current* best level's side empties. That walk is bounded by the configured flat-array
width (a constant, independent of how many orders are live) — described that way rather
than claimed as strict worst-case O(1), because it isn't: `BENCHMARKS.md`'s `MatchingEngine`
results empirically quantify exactly this cost (a ~140x latency gap between a match that
empties a level and one that doesn't), turning what was previously only a qualitative caveat
("bounded... not literally O(1)") into a measured number.

## MatchingEngine's callback: a compile-time template, not `std::function`/CRTP

`engine/matching_engine.hpp`'s `MatchingEngine<EventHandler>` takes its event handler as a
template parameter — a plain object providing `on_fill`/`on_rest`/`on_kill`, invoked
directly. Not `std::function`, which carries type erasure, a possible heap allocation for
large callables, and indirect-call overhead on a hot path explicitly meant to avoid all
three. Not CRTP inheritance either, which was the spec's other suggested option — a template
parameter is simpler to use in tests (pass a handler object; no need to inherit from the
engine itself) while delivering the same compile-time resolution and full inlinability CRTP
would.

## Self-trade prevention: skip, not cancel

When self-trade prevention is enabled (off by default) and an incoming order would match a
resting order sharing its `trader_id`, the resting order is *skipped*, not cancelled: the
incoming order matches the next eligible order in the queue instead, even if that means a
later-arrived order at the same price trades ahead of an earlier one. This is a real,
standard exception to strict FIFO price-time priority in production matching engines, not a
bug — the alternative (cancelling the blocked resting order) would have side effects on a
third party's order that self-trade prevention was never meant to cause. The skipped order
is left exactly where it was, untouched, still resting.

## Fill-Or-Kill: a read-only dry run before any mutation

FOK's atomicity — fill completely or not at all, with zero side effects on rejection — is
implemented as two passes. First, a read-only `available_liquidity()` walk sums matchable
resting shares across *multiple* price levels using `OrderBook::next_level_after()`, added
specifically for this purpose: naively re-querying `best_ask_level()` in a loop wouldn't work
for a dry run, since it would return the same unconsumed level forever — nothing has actually
executed to make the book's own cached "best" advance. Only if that confirms enough liquidity
does the second, real (mutating) pass run; otherwise the order is rejected with zero fills and
zero change to any `OrderBook` observable — verified directly in tests by snapshotting every
observable before and after a rejected FOK and asserting equality, not inferred from "no
`FillEvent` fired."

## The SPSC ring buffer: no CAS, cache-line padding, monotonic indices

`containers/spsc_ring_buffer.hpp`'s `SpscRingBuffer<T>` is single-producer/single-consumer,
the simplest lock-free case: because exactly one thread ever writes each index, `push`/`pop`
need only plain atomic load/store with acquire/release ordering — no compare-exchange, no
`fetch_add`, no retry loop. That machinery exists specifically to arbitrate between
*multiple* writers to the same index (MPSC/MPMC), which doesn't apply here, so including it
would just be unneeded complexity and overhead.

`write_index_` and `read_index_` are each padded onto their own 64-byte cache line. Without
that, the producer writing one and the consumer writing the other would false-share a single
cache line, forcing coherence traffic between cores on every single push/pop even though the
two threads never actually touch each other's index — the first concrete delivery on this
project's "mechanical sympathy" principle, not just a comment claiming cache-awareness.

Both indices increase monotonically forever rather than wrapping (only the physical slot
wraps, via a mask) — the standard technique that avoids the "does `read == write` mean empty
or full?" ambiguity that plagues circular buffers using wrapped indices directly.

A dedicated `tsan` CI job builds the whole suite with `-fsanitize=thread` on real Linux
hardware specifically to verify this reasoning is actually correct, not just plausible —
concurrency bugs are exactly the class of error that "looks right on inspection" fails to
catch reliably.

## Bugs the process caught

The single most concrete evidence behind this project's own claim that "every correctness
claim [is backed] by a test that would actually fail if it were wrong" — collected here in
one place rather than left scattered across seven milestones' worth of commit messages.

| # | Where | What broke | How it was caught | Fix |
|---|---|---|---|---|
| 1 | M2 | Local clang-format (Homebrew 22.x) formatted code differently than CI's pinned `clang-format-18`, specifically `SpaceBeforeCpp11BracedList`'s version-dependent default | CI's `format` job failed on code that passed locally | Set `SpaceBeforeCpp11BracedList: true` explicitly in `.clang-format` rather than relying on an inherited default; pinned CI to the exact `clang-format-18` package |
| 2 | M3 | `testing/allocation_guard.cpp`'s operator overrides were missing the `nothrow` variants; libstdc++'s internal `std::stable_sort` (used by GoogleTest) allocates via `operator new(size, nothrow)`, which went through ASan's own default nothrow-new instead of this file's malloc-backed one, then got freed via the (overridden) sized `operator delete` | ASan's `alloc-dealloc-mismatch` detector, on CI's Linux gcc/clang builds only — never reproduced on macOS/libc++ locally, since its `stable_sort` doesn't use this code path | Added all four missing nothrow overloads |
| 3 | M4 | `OrderBook::remove_order_fully()` forgot to decrement a price level's aggregate share counters when an order was removed via `delete_order`/`replace_order` — the decrement only happened on the execute/cancel path | 4 of 18 new `OrderBook` unit tests failed immediately, pinpointing exactly which best-bid/ask transitions were wrong | Decrement unconditionally in `remove_order_fully()`, regardless of how an order reaches full removal |
| 4 | M5 | `MatchingEngine`'s `available_liquidity()` called `next_level_after(price, order.is_buy)` — but that parameter selects which *book* side to scan (bids vs. asks), not which side the incoming order itself is on | The very first multi-level FOK test failed immediately (0 fills instead of the expected 2) — the liquidity check walked into the empty bid side after the first ask level and gave up early | Corrected to `next_level_after(price, !order.is_buy)` |
| 5 | M6 | TSan's own runtime (`libclang_rt.tsan_cxx`) ships its own `operator new`/`delete` overrides, which it needs for its own allocation tracking — linking M3's `allocation_guard.cpp` overrides alongside it is a genuine link-time ODR violation ("multiple definition of operator new"), not a false alarm | The first `tsan` CI job run failed to link | A `LIQUIBOOK_UNDER_TSAN` macro omits `allocation_guard.cpp`'s overrides entirely under TSan; the two dependent zero-allocation tests `GTEST_SKIP()` cleanly under TSan instead of silently always passing with a counter nothing feeds anymore |
| 6 | M6 | That macro's first version used `defined(__has_feature) && __has_feature(...)`, which fails to *parse* on GCC — GCC doesn't define `__has_feature` at all, and `&&`'s short-circuiting applies to the expression's *value*, not to whether the compiler needs to tokenize the right operand as valid syntax | Both gcc CI legs failed with "missing binary operator before token (" | The portable `#define __has_feature(x) 0` fallback idiom (used by several major C++ codebases) when the macro isn't already defined |
| 7 | M6 | `hash_map.hpp` and the new `spsc_ring_buffer.hpp` both independently defined `liquibook::containers::detail::round_up_to_power_of_two` — harmless individually, but a genuine redefinition once `pipeline/itch_pipeline.hpp` included both in one translation unit | Build failure the moment a consumer needed both containers together | Gave the ring buffer's copy its own, differently-named inner namespace |
| 8 | M7 | `pin_current_thread_to_cpu()`'s `CPU_SET(cpu_id, &cpu_set)` implicitly converts `cpu_id` (`int`) to `size_t` internally — flagged by `-Wsign-conversion`, part of this project's warnings-as-errors set | `gcc / Release` CI failed at build — never caught locally since macOS never compiles this function's `__linux__` branch at all | An explicit `static_cast<std::size_t>(cpu_id)` at the call site |

A pattern worth naming: five of these eight (#1, #2, #5, #6, #8) were **only ever catchable
by CI** — differences between macOS/clang locally and Linux/gcc or Linux/clang-with-TSan in
CI, invisible on the one development machine this project was built on. That's the concrete
argument for why "the CI matrix runs... across both gcc and clang" (and, since M6, under
TSan) is a real correctness practice here, not a checkbox.
