#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "types.hpp"

namespace liquibook::itch {

struct SynthConfig {
    std::uint64_t seed = 42;
    std::size_t num_orders = 10'000; // number of Add Order ('A'/'F') messages to emit
    std::vector<std::string> symbols = {"AAPL", "MSFT", "GOOGL"};
    Price4 base_price = 1'500'000; // $150.00 in 1/10000ths of a dollar
    Price4 price_spread = 50'000;  // random jitter range applied around base_price
};

// Generates a well-formed, length-prefixed ITCH 5.0 byte stream: a System Event (start),
// one Stock Directory per symbol, a randomized order-flow stream, then a System Event (end).
// Deterministic for a given seed.
//
// Self-consistency, by construction: every Order Executed / Order Cancel / Order Delete /
// Order Replace message references an order reference number that a prior Add Order in the
// same stream actually created and that hasn't yet been fully removed -- verified
// independently in test_synth_roundtrip.cpp by replaying the real decode() over the output,
// not just trusted from this function's own bookkeeping. Trade ('P') messages are the one
// deliberate exception: real ITCH Trade messages report non-displayed-liquidity executions
// that are not tied to a resting displayed order, so this generator gives each Trade its own
// fresh, never-reused order reference number rather than picking one from the live book.
void generate(const SynthConfig& config, std::ostream& out);

} // namespace liquibook::itch
