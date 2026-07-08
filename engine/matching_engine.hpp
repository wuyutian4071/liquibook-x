#pragma once

#include <algorithm>

#include "events.hpp"
#include "order.hpp"
#include "order_book.hpp"
#include "order_types.hpp"
#include "price_level.hpp"

namespace liquibook::engine {

// Matches an IncomingOrder against a book::OrderBook's resting liquidity in price-time
// priority, then applies each order type's own remainder rule. Reuses book::OrderBook's
// existing mutation methods (execute_order, add_order) for all actual state changes rather
// than touching book::Order/book::PriceLevel internals directly -- this class only ever
// *reads* through const PriceLevel pointers to decide what to match.
//
// EventHandler is a compile-time template parameter, not std::function and not CRTP
// inheritance -- a plain object providing on_fill(FillEvent)/on_rest(RestEvent)/
// on_kill(KillEvent), invoked directly (fully inlinable, no vtable). Simpler to use in tests
// than CRTP (pass a handler object; no need to inherit from the engine), and satisfies the
// milestone's "CRTP or function-ref, not std::function" requirement via the function-ref
// option: the concrete handler type is resolved at compile time.
template <typename EventHandler>
class MatchingEngine {
public:
    MatchingEngine(book::OrderBook& book, EventHandler& handler, bool prevent_self_trade = false)
        : book_(book), handler_(handler), prevent_self_trade_(prevent_self_trade) {}

    void submit(const IncomingOrder& order) {
        if (order.quantity == 0) {
            return;
        }

        if (order.type == OrderType::FOK) {
            if (available_liquidity(order) < order.quantity) {
                handler_.on_kill(KillEvent {order.order_ref, order.quantity});
                return; // atomic: zero mutation, zero fills
            }
        }

        const itch::Shares filled = execute_match(order);
        const itch::Shares remaining = order.quantity - filled;
        if (remaining == 0) {
            return;
        }

        if (order.type == OrderType::Limit) {
            book_.add_order(order.order_ref, order.price, remaining, order.is_buy, order.trader_id);
            handler_.on_rest(RestEvent {order.order_ref, order.price, remaining, order.is_buy});
        } else {
            // Market, IOC, and FOK (already confirmed fully fillable above, so `remaining`
            // should be 0 here in practice -- handled defensively) never rest a remainder.
            handler_.on_kill(KillEvent {order.order_ref, remaining});
        }
    }

private:
    // True if `candidate` is eligible to match against `order` under self-trade prevention:
    // always eligible when STP is off, or when either side has no trader identity (0).
    [[nodiscard]] bool eligible(const IncomingOrder& order, const book::Order& candidate) const {
        if (!prevent_self_trade_ || order.trader_id == 0) {
            return true;
        }
        return candidate.trader_id != order.trader_id;
    }

    // Whether the best remaining opposite-side price still crosses the incoming order's
    // limit. Market orders cross at any price.
    [[nodiscard]] bool crosses(const IncomingOrder& order, itch::Price4 level_price) const {
        if (order.type == OrderType::Market) {
            return true;
        }
        return order.is_buy ? (level_price <= order.price) : (level_price >= order.price);
    }

    // Read-only: how much of `order` could be filled right now, without mutating the book.
    // Walks levels via OrderBook::next_level_after rather than re-querying best_*_level (which
    // would return the same unconsumed level forever, since nothing here actually executes).
    // Early-exits once the running total already covers the requested quantity.
    [[nodiscard]] itch::Shares available_liquidity(const IncomingOrder& order) const {
        itch::Shares total = 0;
        const book::PriceLevel* level =
            order.is_buy ? book_.best_ask_level() : book_.best_bid_level();

        while (level != nullptr && total < order.quantity) {
            if (!crosses(order, level->price)) {
                break;
            }

            if (!prevent_self_trade_ || order.trader_id == 0) {
                total += order.is_buy ? level->total_ask_shares : level->total_bid_shares;
            } else {
                const auto& queue = order.is_buy ? level->asks : level->bids;
                for (const auto& candidate : queue) {
                    if (eligible(order, candidate)) {
                        total += candidate.shares;
                    }
                }
            }

            level = book_.next_level_after(level->price, order.is_buy);
        }

        return total;
    }

    // Mutating: walks the opposite side in price-time priority, executing against eligible
    // resting orders via book_.execute_order() (which also advances the book's own cached
    // best, so re-querying best_*_level() each iteration naturally reflects consumption).
    // Returns the total quantity filled.
    [[nodiscard]] itch::Shares execute_match(const IncomingOrder& order) {
        itch::Shares remaining = order.quantity;

        while (remaining > 0) {
            const book::PriceLevel* level =
                order.is_buy ? book_.best_ask_level() : book_.best_bid_level();
            if (level == nullptr || !crosses(order, level->price)) {
                break;
            }

            const auto& queue = order.is_buy ? level->asks : level->bids;
            const book::Order* resting = nullptr;
            for (const auto& candidate : queue) {
                if (eligible(order, candidate)) {
                    resting = &candidate;
                    break;
                }
            }
            if (resting == nullptr) {
                break; // every resting order at this level is blocked by self-trade prevention
            }

            const itch::Shares fill_qty = std::min(remaining, resting->shares);
            const itch::OrderRef resting_ref = resting->order_ref;
            const itch::Price4 fill_price = resting->price;

            book_.execute_order(resting_ref, fill_qty);
            remaining -= fill_qty;

            handler_.on_fill(FillEvent {order.order_ref, resting_ref, fill_price, fill_qty});
        }

        return order.quantity - remaining;
    }

    book::OrderBook& book_;
    EventHandler& handler_;
    bool prevent_self_trade_;
};

} // namespace liquibook::engine
