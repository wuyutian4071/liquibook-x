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

Built milestone by milestone. Current: **M2 — ITCH 5.0 parser**: a real, spec-accurate
NASDAQ TotalView-ITCH 5.0 binary decoder for all 10 message types this project needs, a
deterministic self-consistent synthetic order-flow generator (verified by replaying the real
decoder over its own output, not just trusted), an mmap-based zero-copy file reader, and a
measured parser throughput benchmark (~112M messages/sec, Release, this machine).

| Milestone | Scope | State |
|-----------|-------|-------|
| M1 | Repo skeleton, CMake, CI (gcc+clang, Debug+ASan/UBSan, Release), clang-format | ✅ |
| M2 | ITCH 5.0 parser + synthetic data generator + parser throughput benchmark | ✅ |
| M3 | Object pool, intrusive list, open-addressing hash map | ⬜ |
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

## Design principles

1. **Correctness first, then measured performance.** No latency claim ships without a
   benchmark in the repo backing it.
2. **Zero heap allocation on the hot path.** Verified, not assumed — object pools and
   intrusive containers throughout; a counting allocator will assert this in tests.
3. **No locks on the hot path.** Thread handoff uses a lock-free SPSC ring buffer (M6).
4. **Mechanical sympathy.** Cache-line-aware layout, branch-prediction-conscious code,
   documented and benchmarked, not just asserted in a comment.

## License

MIT — see [LICENSE](LICENSE).
