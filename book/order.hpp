#pragma once

#include <cstdint>

#include "types.hpp"

namespace liquibook::book {

struct PriceLevel; // forward declared; only a pointer is needed here (full definition in
                   // price_level.hpp, which includes this header for IntrusiveList<Order>)

// A trivially-destructible POD (required by containers::ObjectPool<Order>) representing one
// resting order. `prev`/`next` satisfy containers::IntrusiveList's node contract; `level` is
// a back-pointer to the PriceLevel this order currently rests in, so cancel/execute/delete
// never need to re-derive a price -> level mapping -- O(1) delist directly from the order
// itself, not a second lookup.
struct Order {
    itch::OrderRef order_ref = 0;
    itch::Price4 price = 0;
    itch::Shares shares = 0; // remaining (unexecuted, uncancelled) shares
    bool is_buy = false;
    // 0 = "no trader" -- an order with trader_id 0 is never subject to self-trade
    // prevention (M5's engine/matching_engine.hpp), matching every ITCH-driven order (which
    // has no participant identity of its own) never being unintentionally blocked.
    std::uint32_t trader_id = 0;
    Order* prev = nullptr;
    Order* next = nullptr;
    PriceLevel* level = nullptr;
};

} // namespace liquibook::book
