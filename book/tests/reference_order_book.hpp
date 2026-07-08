#pragma once

#include <optional>
#include <unordered_map>

#include "types.hpp"

namespace liquibook::book::testing {

// A deliberately simple, obviously-correct-by-inspection reference book: no object pools, no
// intrusive lists, no flat arrays, just a plain hash map and O(n) scans. Slow by design --
// this is what the real OrderBook (book/order_book.hpp) gets checked against in
// test_order_book_differential.cpp, not the other way around.
class ReferenceOrderBook {
public:
    bool add_order(itch::OrderRef ref, itch::Price4 price, itch::Shares shares, bool is_buy) {
        if (shares == 0 || orders_.count(ref) > 0) {
            return false;
        }
        orders_[ref] = RefOrder {price, shares, is_buy};
        return true;
    }

    bool execute_order(itch::OrderRef ref, itch::Shares executed_shares) {
        return reduce_shares(ref, executed_shares);
    }

    bool cancel_order(itch::OrderRef ref, itch::Shares cancelled_shares) {
        return reduce_shares(ref, cancelled_shares);
    }

    bool delete_order(itch::OrderRef ref) { return orders_.erase(ref) > 0; }

    bool replace_order(itch::OrderRef old_ref,
                       itch::OrderRef new_ref,
                       itch::Shares new_shares,
                       itch::Price4 new_price) {
        const auto it = orders_.find(old_ref);
        if (it == orders_.end() || new_shares == 0) {
            return false;
        }
        if (old_ref != new_ref && orders_.count(new_ref) > 0) {
            return false;
        }
        const bool is_buy = it->second.is_buy;
        orders_.erase(it);
        orders_[new_ref] = RefOrder {new_price, new_shares, is_buy};
        return true;
    }

    [[nodiscard]] std::optional<itch::Price4> best_bid() const { return best_price(true); }
    [[nodiscard]] std::optional<itch::Price4> best_ask() const { return best_price(false); }

    [[nodiscard]] itch::Shares shares_at(itch::Price4 price, bool is_buy) const {
        itch::Shares total = 0;
        for (const auto& [ref, order] : orders_) {
            if (order.price == price && order.is_buy == is_buy) {
                total += order.shares;
            }
        }
        return total;
    }

    [[nodiscard]] std::size_t order_count() const { return orders_.size(); }

private:
    struct RefOrder {
        itch::Price4 price;
        itch::Shares shares;
        bool is_buy;
    };

    bool reduce_shares(itch::OrderRef ref, itch::Shares delta) {
        const auto it = orders_.find(ref);
        if (it == orders_.end() || delta == 0 || delta > it->second.shares) {
            return false;
        }
        it->second.shares -= delta;
        if (it->second.shares == 0) {
            orders_.erase(it);
        }
        return true;
    }

    [[nodiscard]] std::optional<itch::Price4> best_price(bool is_buy) const {
        std::optional<itch::Price4> best;
        for (const auto& [ref, order] : orders_) {
            if (order.is_buy != is_buy) {
                continue;
            }
            if (!best || (is_buy ? order.price > *best : order.price < *best)) {
                best = order.price;
            }
        }
        return best;
    }

    std::unordered_map<itch::OrderRef, RefOrder> orders_;
};

} // namespace liquibook::book::testing
