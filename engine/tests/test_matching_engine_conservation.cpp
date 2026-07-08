#include <gtest/gtest.h>

#include <random>
#include <vector>

#include "matching_engine.hpp"
#include "order_book.hpp"

using liquibook::book::OrderBook;
using liquibook::engine::FillEvent;
using liquibook::engine::IncomingOrder;
using liquibook::engine::KillEvent;
using liquibook::engine::MatchingEngine;
using liquibook::engine::OrderType;
using liquibook::engine::RestEvent;
namespace itch = liquibook::itch;

namespace {

struct RecordingHandler {
    std::vector<FillEvent> fills;
    std::vector<RestEvent> rests;
    std::vector<KillEvent> kills;

    void on_fill(const FillEvent& e) { fills.push_back(e); }
    void on_rest(const RestEvent& e) { rests.push_back(e); }
    void on_kill(const KillEvent& e) { kills.push_back(e); }
};

} // namespace

// Not a differential test against a second implementation (M5 has no independent reference
// matching engine) -- instead a *conservation* check: for every single submit() call, no
// matter the order type, side, price, or self-trade-prevention outcome, the incoming
// quantity must be accounted for exactly once as filled, rested, or killed. This is a
// property that must hold for ANY correct matching engine, regardless of implementation
// strategy -- if it's ever violated, shares have been created or destroyed, a much more
// fundamental bug than any single scenario test would catch.
TEST(MatchingEngineConservation, EveryOrderIsFullyAccountedForAcrossARandomStream) {
    constexpr itch::Price4 kRef = 1'000'000;
    constexpr std::size_t kHalfWidth = 200;
    constexpr std::size_t kCapacity = 5000;
    constexpr std::size_t kNumOrders = 500;

    OrderBook book(kRef, kHalfWidth, kCapacity);
    RecordingHandler handler;
    MatchingEngine engine(book, handler, /*prevent_self_trade=*/true);

    std::mt19937_64 rng(123);
    // A wide spread relative to kHalfWidth so the stream exercises both the flat-array and
    // fallback-map price paths.
    std::uniform_int_distribution<std::int32_t> price_jitter(-400, 400);
    std::uniform_int_distribution<itch::Shares> shares_dist(10, 200);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int> type_dist(0, 3);
    // A small trader pool (including 0 = "no trader") so self-trade-prevention scenarios
    // come up often, not just as a rare edge case.
    std::uniform_int_distribution<std::uint32_t> trader_dist(0, 3);

    for (std::size_t i = 0; i < kNumOrders; ++i) {
        const auto price =
            static_cast<itch::Price4>(static_cast<std::int64_t>(kRef) + price_jitter(rng));
        const OrderType type = [&] {
            switch (type_dist(rng)) {
            case 0:
                return OrderType::Limit;
            case 1:
                return OrderType::Market;
            case 2:
                return OrderType::IOC;
            default:
                return OrderType::FOK;
            }
        }();

        IncomingOrder order;
        order.order_ref = i + 1;
        order.price = price;
        order.quantity = shares_dist(rng);
        order.is_buy = side_dist(rng) == 0;
        order.type = type;
        order.trader_id = trader_dist(rng);

        const auto fills_before = handler.fills.size();
        const auto rests_before = handler.rests.size();
        const auto kills_before = handler.kills.size();

        engine.submit(order);

        itch::Shares filled = 0;
        for (std::size_t j = fills_before; j < handler.fills.size(); ++j) {
            ASSERT_EQ(handler.fills[j].aggressor_ref, order.order_ref);
            filled += handler.fills[j].quantity;
        }
        itch::Shares rested = 0;
        for (std::size_t j = rests_before; j < handler.rests.size(); ++j) {
            ASSERT_EQ(handler.rests[j].order_ref, order.order_ref);
            rested += handler.rests[j].quantity;
        }
        itch::Shares killed = 0;
        for (std::size_t j = kills_before; j < handler.kills.size(); ++j) {
            ASSERT_EQ(handler.kills[j].order_ref, order.order_ref);
            killed += handler.kills[j].unfilled_quantity;
        }

        ASSERT_EQ(filled + rested + killed, order.quantity)
            << "conservation violated for order " << order.order_ref << " (type "
            << static_cast<int>(order.type) << ")";

        // Only a Limit order's remainder ever rests; every other type discards it.
        if (order.type != OrderType::Limit) {
            ASSERT_EQ(rested, 0u);
        }
        // A single submit() never both rests AND kills a remainder -- exactly one applies.
        ASSERT_TRUE(rested == 0 || killed == 0);
    }
}
