#pragma once

#include <cstdint>

#include "types.hpp"

namespace liquibook::engine {

// 0 = "no trader" -- matches book::Order's own convention (book/order.hpp): an order with
// trader_id 0 is never subject to self-trade prevention.
using TraderId = std::uint32_t;

enum class OrderType {
    Limit,  // matches up to `price`; any unfilled remainder rests in the book
    Market, // matches at any price until filled or the book is exhausted; remainder discarded
    IOC,    // Immediate-Or-Cancel: matches up to `price` immediately; remainder discarded
    FOK,    // Fill-Or-Kill: fills completely at/better than `price`, or not at all (atomic)
};

// An order arriving at the matching engine, as distinct from book::Order (a *resting* order
// already in the book) -- this is the engine's own input type, not something that lives in
// containers::ObjectPool or IntrusiveList.
struct IncomingOrder {
    itch::OrderRef order_ref;
    itch::Price4 price; // ignored for Market
    itch::Shares quantity;
    bool is_buy;
    OrderType type;
    TraderId trader_id = 0;
};

} // namespace liquibook::engine
