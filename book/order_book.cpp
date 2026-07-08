#include "order_book.hpp"

#include <algorithm>

namespace liquibook::book {

OrderBook::OrderBook(itch::Price4 reference_price,
                     std::size_t flat_array_half_width,
                     std::size_t order_capacity)
    : reference_price_(reference_price), flat_array_half_width_(flat_array_half_width),
      flat_levels_(2 * flat_array_half_width + 1), order_pool_(order_capacity),
      order_index_(order_capacity) {
    // Every flat-array slot's price is fixed for the book's lifetime -- set once here rather
    // than lazily, so level_for_mutation/level_for_query never need an "is this slot
    // initialized yet" check.
    for (std::size_t i = 0; i < flat_levels_.size(); ++i) {
        const auto offset =
            static_cast<std::int64_t>(i) - static_cast<std::int64_t>(flat_array_half_width_);
        flat_levels_[i].price =
            static_cast<itch::Price4>(static_cast<std::int64_t>(reference_price_) + offset);
    }
}

bool OrderBook::flat_index_for(itch::Price4 price, std::size_t& out_index) const noexcept {
    const auto offset =
        static_cast<std::int64_t>(price) - static_cast<std::int64_t>(reference_price_);
    const auto half_width = static_cast<std::int64_t>(flat_array_half_width_);
    if (offset < -half_width || offset > half_width) {
        return false;
    }
    out_index = static_cast<std::size_t>(offset + half_width);
    return true;
}

PriceLevel* OrderBook::level_for_mutation(itch::Price4 price) {
    std::size_t index = 0;
    if (flat_index_for(price, index)) {
        return &flat_levels_[index];
    }
    auto [it, inserted] = outlier_levels_.try_emplace(price);
    if (inserted) {
        it->second.price = price;
    }
    return &it->second;
}

const PriceLevel* OrderBook::level_for_query(itch::Price4 price) const noexcept {
    std::size_t index = 0;
    if (flat_index_for(price, index)) {
        return &flat_levels_[index];
    }
    const auto it = outlier_levels_.find(price);
    return it != outlier_levels_.end() ? &it->second : nullptr;
}

void OrderBook::update_best_on_add(itch::Price4 price, bool is_buy) noexcept {
    if (is_buy) {
        if (!has_best_bid_ || price > best_bid_price_) {
            best_bid_price_ = price;
            has_best_bid_ = true;
        }
    } else {
        if (!has_best_ask_ || price < best_ask_price_) {
            best_ask_price_ = price;
            has_best_ask_ = true;
        }
    }
}

void OrderBook::recompute_best_after_level_emptied(bool is_buy) noexcept {
    std::optional<itch::Price4> best_in_flat;
    if (is_buy) {
        for (std::size_t i = flat_levels_.size(); i-- > 0;) {
            if (flat_levels_[i].total_bid_shares > 0) {
                best_in_flat = flat_levels_[i].price;
                break;
            }
        }
    } else {
        for (const auto& level : flat_levels_) {
            if (level.total_ask_shares > 0) {
                best_in_flat = level.price;
                break;
            }
        }
    }

    std::optional<itch::Price4> best_in_outliers;
    for (const auto& [price, level] : outlier_levels_) {
        const itch::Shares shares = is_buy ? level.total_bid_shares : level.total_ask_shares;
        if (shares == 0) {
            continue;
        }
        if (!best_in_outliers || (is_buy ? price > *best_in_outliers : price < *best_in_outliers)) {
            best_in_outliers = price;
        }
    }

    std::optional<itch::Price4> new_best;
    if (best_in_flat && best_in_outliers) {
        new_best = is_buy ? std::max(*best_in_flat, *best_in_outliers)
                          : std::min(*best_in_flat, *best_in_outliers);
    } else {
        new_best = best_in_flat ? best_in_flat : best_in_outliers;
    }

    if (is_buy) {
        has_best_bid_ = new_best.has_value();
        if (new_best) {
            best_bid_price_ = *new_best;
        }
    } else {
        has_best_ask_ = new_best.has_value();
        if (new_best) {
            best_ask_price_ = *new_best;
        }
    }
}

void OrderBook::remove_order_fully(Order* order) {
    PriceLevel* level = order->level;
    const bool is_buy = order->is_buy;
    const itch::Price4 price = order->price;
    const itch::Shares remaining = order->shares;

    if (is_buy) {
        level->bids.remove(order);
        level->total_bid_shares -= remaining;
    } else {
        level->asks.remove(order);
        level->total_ask_shares -= remaining;
    }
    order_index_.erase(order->order_ref);
    order_pool_.release(order);

    const bool side_now_empty = is_buy ? level->bids.empty() : level->asks.empty();
    if (!side_now_empty) {
        return;
    }
    const bool was_best = is_buy ? (has_best_bid_ && price == best_bid_price_)
                                 : (has_best_ask_ && price == best_ask_price_);
    if (was_best) {
        recompute_best_after_level_emptied(is_buy);
    }
}

bool OrderBook::add_order(itch::OrderRef ref,
                          itch::Price4 price,
                          itch::Shares shares,
                          bool is_buy,
                          std::uint32_t trader_id) {
    if (shares == 0) {
        return false;
    }
    if (order_index_.find(ref) != nullptr) {
        return false; // duplicate order reference
    }

    Order* order = order_pool_.acquire();
    if (order == nullptr) {
        return false; // pool exhausted
    }

    PriceLevel* level = level_for_mutation(price);
    order->order_ref = ref;
    order->price = price;
    order->shares = shares;
    order->is_buy = is_buy;
    order->trader_id = trader_id;
    order->level = level;

    if (is_buy) {
        level->bids.push_back(order);
        level->total_bid_shares += shares;
    } else {
        level->asks.push_back(order);
        level->total_ask_shares += shares;
    }

    if (!order_index_.insert(ref, order)) {
        // Hash map full: roll back rather than leave the book in an inconsistent state.
        if (is_buy) {
            level->bids.remove(order);
            level->total_bid_shares -= shares;
        } else {
            level->asks.remove(order);
            level->total_ask_shares -= shares;
        }
        order_pool_.release(order);
        return false;
    }

    update_best_on_add(price, is_buy);
    return true;
}

bool OrderBook::reduce_shares(itch::OrderRef ref, itch::Shares delta) {
    Order** slot = order_index_.find(ref);
    if (slot == nullptr) {
        return false;
    }
    Order* order = *slot;
    if (delta == 0 || delta > order->shares) {
        return false;
    }

    order->shares -= delta;
    PriceLevel* level = order->level;
    if (order->is_buy) {
        level->total_bid_shares -= delta;
    } else {
        level->total_ask_shares -= delta;
    }

    if (order->shares == 0) {
        remove_order_fully(order);
    }
    return true;
}

bool OrderBook::execute_order(itch::OrderRef ref, itch::Shares executed_shares) {
    return reduce_shares(ref, executed_shares);
}

bool OrderBook::cancel_order(itch::OrderRef ref, itch::Shares cancelled_shares) {
    return reduce_shares(ref, cancelled_shares);
}

bool OrderBook::delete_order(itch::OrderRef ref) {
    Order** slot = order_index_.find(ref);
    if (slot == nullptr) {
        return false;
    }
    remove_order_fully(*slot);
    return true;
}

bool OrderBook::replace_order(itch::OrderRef old_ref,
                              itch::OrderRef new_ref,
                              itch::Shares new_shares,
                              itch::Price4 new_price) {
    Order** slot = order_index_.find(old_ref);
    if (slot == nullptr) {
        return false;
    }
    if (new_shares == 0) {
        return false;
    }
    if (old_ref != new_ref && order_index_.find(new_ref) != nullptr) {
        return false; // new_ref collides with a different live order
    }

    Order* old_order = *slot;
    const bool is_buy = old_order->is_buy;
    remove_order_fully(old_order);
    return add_order(new_ref, new_price, new_shares, is_buy);
}

void OrderBook::apply(const itch::DecodedMessage& msg) {
    switch (msg.header.type) {
    case itch::MessageType::AddOrder:
    case itch::MessageType::AddOrderMPID: {
        const auto& ao = msg.as_add_order();
        add_order(ao.order_reference_number, ao.price, ao.shares, ao.buy_sell_indicator == 'B');
        break;
    }
    case itch::MessageType::OrderExecuted: {
        const auto& oe = msg.as_order_executed();
        execute_order(oe.order_reference_number, oe.executed_shares);
        break;
    }
    case itch::MessageType::OrderExecutedWithPrice: {
        const auto& oe = msg.as_order_executed_with_price();
        execute_order(oe.order_reference_number, oe.executed_shares);
        break;
    }
    case itch::MessageType::OrderCancel: {
        const auto& oc = msg.as_order_cancel();
        cancel_order(oc.order_reference_number, oc.cancelled_shares);
        break;
    }
    case itch::MessageType::OrderDelete: {
        const auto& od = msg.as_order_delete();
        delete_order(od.order_reference_number);
        break;
    }
    case itch::MessageType::OrderReplace: {
        const auto& orep = msg.as_order_replace();
        replace_order(orep.original_order_reference_number,
                      orep.new_order_reference_number,
                      orep.shares,
                      orep.price);
        break;
    }
    case itch::MessageType::SystemEvent:
    case itch::MessageType::StockDirectory:
    case itch::MessageType::Trade:
        break; // no-ops: don't affect displayed book state
    }
}

std::optional<itch::Price4> OrderBook::best_bid() const noexcept {
    return has_best_bid_ ? std::optional<itch::Price4>(best_bid_price_) : std::nullopt;
}

std::optional<itch::Price4> OrderBook::best_ask() const noexcept {
    return has_best_ask_ ? std::optional<itch::Price4>(best_ask_price_) : std::nullopt;
}

itch::Shares OrderBook::shares_at(itch::Price4 price, bool is_buy) const noexcept {
    const PriceLevel* level = level_for_query(price);
    if (level == nullptr) {
        return 0;
    }
    return is_buy ? level->total_bid_shares : level->total_ask_shares;
}

const PriceLevel* OrderBook::best_bid_level() const noexcept {
    return has_best_bid_ ? level_for_query(best_bid_price_) : nullptr;
}

const PriceLevel* OrderBook::best_ask_level() const noexcept {
    return has_best_ask_ ? level_for_query(best_ask_price_) : nullptr;
}

const PriceLevel* OrderBook::next_level_after(itch::Price4 price, bool is_buy) const noexcept {
    // Mirrors recompute_best_after_level_emptied's flat-array-scan + outlier-map-scan +
    // pick-the-nearer pattern, parameterized by an exclusion price instead of "no exclusion"
    // -- a read-only traversal that lets a caller (M5's matching engine) walk multiple levels
    // without mutating the book, which naive repeated best_bid_level()/best_ask_level() calls
    // cannot do (those only ever report the single incrementally-cached best).
    std::optional<itch::Price4> best_in_flat;
    if (is_buy) {
        for (std::size_t i = flat_levels_.size(); i-- > 0;) {
            if (flat_levels_[i].price < price && flat_levels_[i].total_bid_shares > 0) {
                best_in_flat = flat_levels_[i].price;
                break;
            }
        }
    } else {
        for (const auto& level : flat_levels_) {
            if (level.price > price && level.total_ask_shares > 0) {
                best_in_flat = level.price;
                break;
            }
        }
    }

    std::optional<itch::Price4> best_in_outliers;
    for (const auto& [outlier_price, level] : outlier_levels_) {
        if (is_buy ? (outlier_price >= price) : (outlier_price <= price)) {
            continue; // must be strictly worse than `price`
        }
        const itch::Shares shares = is_buy ? level.total_bid_shares : level.total_ask_shares;
        if (shares == 0) {
            continue;
        }
        if (!best_in_outliers ||
            (is_buy ? outlier_price > *best_in_outliers : outlier_price < *best_in_outliers)) {
            best_in_outliers = outlier_price;
        }
    }

    std::optional<itch::Price4> result;
    if (best_in_flat && best_in_outliers) {
        result = is_buy ? std::max(*best_in_flat, *best_in_outliers)
                        : std::min(*best_in_flat, *best_in_outliers);
    } else {
        result = best_in_flat ? best_in_flat : best_in_outliers;
    }

    return result ? level_for_query(*result) : nullptr;
}

} // namespace liquibook::book
