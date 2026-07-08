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

Built milestone by milestone. Current: **M1 — repo skeleton**: CMake build system, sanitizer
and warnings-as-errors infrastructure, GoogleTest wired in via `FetchContent`, and a CI matrix
(gcc + clang, Debug+ASan/UBSan + Release) — verified end to end with a minimal smoke target
before any real module exists.

| Milestone | Scope | State |
|-----------|-------|-------|
| M1 | Repo skeleton, CMake, CI (gcc+clang, Debug+ASan/UBSan, Release), clang-format | ✅ |
| M2 | ITCH 5.0 parser + synthetic data generator + parser throughput benchmark | ⬜ |
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
```

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
