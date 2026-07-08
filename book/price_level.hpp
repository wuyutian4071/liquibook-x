#pragma once

#include "intrusive_list.hpp"
#include "order.hpp"
#include "types.hpp"

namespace liquibook::book {

// All resting interest at one exact price. Not inherently one-sided -- as the raw ITCH
// protocol models it, a price could (transiently, before any matching logic exists -- that's
// M5's job, not M4's) hold both a bids and an asks queue, so this holds two independent
// IntrusiveList<Order> FIFOs. Each Order lives in exactly one of the two (never both), so
// there's no conflict with IntrusiveList's one-node-one-list constraint.
struct PriceLevel {
    itch::Price4 price = 0;
    containers::IntrusiveList<Order> bids;
    containers::IntrusiveList<Order> asks;
    itch::Shares total_bid_shares = 0;
    itch::Shares total_ask_shares = 0;

    [[nodiscard]] bool empty() const noexcept { return bids.empty() && asks.empty(); }
};

} // namespace liquibook::book
