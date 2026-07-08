#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "hash_map.hpp"
#include "messages.hpp"
#include "object_pool.hpp"
#include "price_level.hpp"
#include "types.hpp"

namespace liquibook::book {

// A single-symbol limit order book. A future multi-symbol router (M5+) is responsible for
// dispatching ITCH messages to the right OrderBook by stock_locate -- this class doesn't
// filter by symbol itself.
//
// Price levels live in a flat array indexed by raw Price4-unit offset from `reference_price`
// (a "tick" here means one Price4 unit -- 1/10000 of a dollar -- not a market-specific
// minimum increment; see the M4 design notes on why: the synthetic test data doesn't
// guarantee alignment to any coarser tick size, and bucketing by a coarser tick without that
// guarantee would silently collide distinct prices). Prices outside that window fall back to
// `outlier_levels_`, a std::map. add/cancel/execute/delete are O(1) within the flat-array
// window and O(log n) for the fallback-map path -- an inherent, stated cost of "flat array +
// map for outliers," not a bug.
//
// Best bid/ask are tracked incrementally, never scanned on the fast path: a cached price
// updates in O(1) whenever a new order improves it, and only walks to find the next-best
// price when the *current* best level's side empties -- bounded by the configured flat-array
// width (a constant, independent of how many orders are live), not literally O(1) in the
// strict worst-case sense, and described that way rather than oversold.
class OrderBook {
public:
    OrderBook(itch::Price4 reference_price,
              std::size_t flat_array_half_width,
              std::size_t order_capacity);

    // ITCH-driven dispatch. No-ops for message types that don't affect displayed book state:
    // System Event, Stock Directory, and Trade (Trade reports a non-displayed-liquidity
    // execution, not tied to a resting order -- see itch/synth.hpp's doc comment).
    void apply(const itch::DecodedMessage& msg);

    bool add_order(itch::OrderRef ref,
                   itch::Price4 price,
                   itch::Shares shares,
                   bool is_buy,
                   std::uint32_t trader_id = 0);
    bool execute_order(itch::OrderRef ref, itch::Shares executed_shares);
    bool cancel_order(itch::OrderRef ref, itch::Shares cancelled_shares);
    bool delete_order(itch::OrderRef ref);
    bool replace_order(itch::OrderRef old_ref,
                       itch::OrderRef new_ref,
                       itch::Shares new_shares,
                       itch::Price4 new_price);

    [[nodiscard]] std::optional<itch::Price4> best_bid() const noexcept;
    [[nodiscard]] std::optional<itch::Price4> best_ask() const noexcept;
    [[nodiscard]] itch::Shares shares_at(itch::Price4 price, bool is_buy) const noexcept;
    [[nodiscard]] std::size_t order_count() const noexcept { return order_index_.size(); }

    // Read-only access for M5's matching engine: the level object itself (not just its
    // price), so a caller can peek at resting orders via level->bids/level->asks without
    // OrderBook needing to know anything about matching. best_*_level() are the incrementally
    // cached best (O(1)); next_level_after() is a real, bounded scan (mirrors
    // recompute_best_after_level_emptied's flat-array + outlier-map pattern) used to walk
    // multiple levels read-only -- e.g. for a Fill-Or-Kill liquidity check that must not
    // mutate the book while probing.
    [[nodiscard]] const PriceLevel* best_bid_level() const noexcept;
    [[nodiscard]] const PriceLevel* best_ask_level() const noexcept;
    [[nodiscard]] const PriceLevel* next_level_after(itch::Price4 price,
                                                     bool is_buy) const noexcept;

private:
    [[nodiscard]] bool flat_index_for(itch::Price4 price, std::size_t& out_index) const noexcept;
    [[nodiscard]] PriceLevel* level_for_mutation(itch::Price4 price);
    [[nodiscard]] const PriceLevel* level_for_query(itch::Price4 price) const noexcept;
    void update_best_on_add(itch::Price4 price, bool is_buy) noexcept;
    void remove_order_fully(Order* order);
    void recompute_best_after_level_emptied(bool is_buy) noexcept;
    bool reduce_shares(itch::OrderRef ref, itch::Shares delta);

    itch::Price4 reference_price_;
    std::size_t flat_array_half_width_;
    std::vector<PriceLevel> flat_levels_;
    std::map<itch::Price4, PriceLevel> outlier_levels_;

    containers::ObjectPool<Order> order_pool_;
    containers::OpenAddressingHashMap<itch::OrderRef, Order*> order_index_;

    bool has_best_bid_ = false;
    itch::Price4 best_bid_price_ = 0;
    bool has_best_ask_ = false;
    itch::Price4 best_ask_price_ = 0;
};

} // namespace liquibook::book
