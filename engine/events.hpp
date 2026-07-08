#pragma once

#include "types.hpp"

namespace liquibook::engine {

// A single match between an incoming (aggressor) order and one resting order. `price` is the
// *resting* order's price, not the aggressor's -- the standard price-time-priority
// convention: the aggressor gets price improvement, never trades worse than their own limit.
// A single submit() can emit several FillEvents (walking multiple resting orders, possibly
// across several price levels).
struct FillEvent {
    itch::OrderRef aggressor_ref;
    itch::OrderRef resting_ref;
    itch::Price4 price;
    itch::Shares quantity;
};

// A new order, or an order's unfilled remainder, added to the book as resting liquidity
// (Limit orders only -- Market/IOC/FOK never rest, see KillEvent).
struct RestEvent {
    itch::OrderRef order_ref;
    itch::Price4 price;
    itch::Shares quantity;
    bool is_buy;
};

// An order's unfilled quantity was discarded rather than resting: a Market or IOC remainder
// after the available liquidity ran out, or a FOK that could not be fully filled (in which
// case `unfilled_quantity` equals the entire original quantity -- zero fills occurred).
struct KillEvent {
    itch::OrderRef order_ref;
    itch::Shares unfilled_quantity;
};

} // namespace liquibook::engine
