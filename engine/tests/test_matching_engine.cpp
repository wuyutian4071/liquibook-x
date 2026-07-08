#include <gtest/gtest.h>

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

constexpr itch::Price4 kRef = 1'000'000;
constexpr std::size_t kHalfWidth = 200;
constexpr std::size_t kCapacity = 256;

OrderBook make_book() {
    return OrderBook(kRef, kHalfWidth, kCapacity);
}

} // namespace

TEST(MatchingEngine, LimitOrderPartiallyFillsAndRestsRemainder) {
    OrderBook book = make_book();
    ASSERT_TRUE(book.add_order(1, kRef, 60, /*is_buy=*/false)); // resting ask

    RecordingHandler handler;
    MatchingEngine engine(book, handler);
    engine.submit(IncomingOrder {2, kRef, 100, /*is_buy=*/true, OrderType::Limit});

    ASSERT_EQ(handler.fills.size(), 1u);
    EXPECT_EQ(handler.fills[0].aggressor_ref, 2u);
    EXPECT_EQ(handler.fills[0].resting_ref, 1u);
    EXPECT_EQ(handler.fills[0].price, kRef);
    EXPECT_EQ(handler.fills[0].quantity, 60u);

    ASSERT_EQ(handler.rests.size(), 1u);
    EXPECT_EQ(handler.rests[0].order_ref, 2u);
    EXPECT_EQ(handler.rests[0].quantity, 40u);

    EXPECT_EQ(book.shares_at(kRef, /*is_buy=*/false), 0u);
    EXPECT_EQ(book.shares_at(kRef, /*is_buy=*/true), 40u);
}

TEST(MatchingEngine, LimitOrderExactQuantityMatchLeavesNoRemainder) {
    OrderBook book = make_book();
    ASSERT_TRUE(book.add_order(1, kRef, 100, false));

    RecordingHandler handler;
    MatchingEngine engine(book, handler);
    engine.submit(IncomingOrder {2, kRef, 100, true, OrderType::Limit});

    ASSERT_EQ(handler.fills.size(), 1u);
    EXPECT_EQ(handler.fills[0].quantity, 100u);
    EXPECT_TRUE(handler.rests.empty());
    EXPECT_EQ(book.order_count(), 0u);
}

TEST(MatchingEngine, LimitOrderWalksMultiplePriceLevels) {
    OrderBook book = make_book();
    ASSERT_TRUE(book.add_order(1, kRef, 30, false));
    ASSERT_TRUE(book.add_order(2, kRef + 5, 30, false));
    ASSERT_TRUE(book.add_order(3, kRef + 10, 30, false));

    RecordingHandler handler;
    MatchingEngine engine(book, handler);
    engine.submit(IncomingOrder {4, kRef + 10, 70, true, OrderType::Limit});

    ASSERT_EQ(handler.fills.size(), 3u);
    EXPECT_EQ(handler.fills[0].price, kRef); // best price first
    EXPECT_EQ(handler.fills[1].price, kRef + 5);
    EXPECT_EQ(handler.fills[2].price, kRef + 10);
    EXPECT_EQ(handler.fills[2].quantity, 10u); // last level only partially consumed

    ASSERT_EQ(handler.rests.size(), 0u);
    EXPECT_EQ(book.shares_at(kRef + 10, false), 20u); // remainder of the last resting order
}

TEST(MatchingEngine, PriceTimePriorityFillsEarliestOrderFirstWithinALevel) {
    OrderBook book = make_book();
    ASSERT_TRUE(book.add_order(1, kRef, 30, false)); // arrives first
    ASSERT_TRUE(book.add_order(2, kRef, 30, false)); // arrives second

    RecordingHandler handler;
    MatchingEngine engine(book, handler);
    engine.submit(IncomingOrder {3, kRef, 30, true, OrderType::Limit});

    ASSERT_EQ(handler.fills.size(), 1u);
    EXPECT_EQ(handler.fills[0].resting_ref, 1u); // earliest-arrived order fills first
    EXPECT_EQ(book.shares_at(kRef, false), 30u); // order 2 untouched
}

TEST(MatchingEngine, LimitOrderWithNoCrossingLiquidityRestsEntirely) {
    OrderBook book = make_book();
    ASSERT_TRUE(book.add_order(1, kRef + 10, 50, false)); // ask above the incoming bid's limit

    RecordingHandler handler;
    MatchingEngine engine(book, handler);
    engine.submit(IncomingOrder {2, kRef, 100, true, OrderType::Limit});

    EXPECT_TRUE(handler.fills.empty());
    ASSERT_EQ(handler.rests.size(), 1u);
    EXPECT_EQ(handler.rests[0].quantity, 100u);
    EXPECT_EQ(book.shares_at(kRef, true), 100u);
}

TEST(MatchingEngine, MarketOrderFullyFilledAcrossLevelsIgnoringPrice) {
    OrderBook book = make_book();
    ASSERT_TRUE(book.add_order(1, kRef, 40, false));
    ASSERT_TRUE(book.add_order(2, kRef + 50, 60, false)); // far outside a Limit's usual range

    RecordingHandler handler;
    MatchingEngine engine(book, handler);
    engine.submit(IncomingOrder {3, /*price=*/0, 100, true, OrderType::Market});

    ASSERT_EQ(handler.fills.size(), 2u);
    EXPECT_EQ(handler.fills[0].price, kRef);
    EXPECT_EQ(handler.fills[1].price, kRef + 50);
    EXPECT_TRUE(handler.kills.empty());
    EXPECT_EQ(book.order_count(), 0u);
}

TEST(MatchingEngine, MarketOrderPartialFillDiscardsRemainderWithoutResting) {
    OrderBook book = make_book();
    ASSERT_TRUE(book.add_order(1, kRef, 40, false)); // book exhausted after this

    RecordingHandler handler;
    MatchingEngine engine(book, handler);
    engine.submit(IncomingOrder {2, 0, 100, true, OrderType::Market});

    ASSERT_EQ(handler.fills.size(), 1u);
    EXPECT_EQ(handler.fills[0].quantity, 40u);
    ASSERT_EQ(handler.kills.size(), 1u);
    EXPECT_EQ(handler.kills[0].unfilled_quantity, 60u);
    EXPECT_EQ(book.order_count(), 0u); // remainder never rests
}

TEST(MatchingEngine, IocPartialFillCancelsRemainder) {
    OrderBook book = make_book();
    ASSERT_TRUE(book.add_order(1, kRef, 30, false));

    RecordingHandler handler;
    MatchingEngine engine(book, handler);
    engine.submit(IncomingOrder {2, kRef, 100, true, OrderType::IOC});

    ASSERT_EQ(handler.fills.size(), 1u);
    EXPECT_EQ(handler.fills[0].quantity, 30u);
    ASSERT_EQ(handler.kills.size(), 1u);
    EXPECT_EQ(handler.kills[0].unfilled_quantity, 70u);
    EXPECT_EQ(book.order_count(), 0u);
}

TEST(MatchingEngine, IocWithNoLiquidityIsFullyKilled) {
    OrderBook book = make_book();

    RecordingHandler handler;
    MatchingEngine engine(book, handler);
    engine.submit(IncomingOrder {1, kRef, 50, true, OrderType::IOC});

    EXPECT_TRUE(handler.fills.empty());
    ASSERT_EQ(handler.kills.size(), 1u);
    EXPECT_EQ(handler.kills[0].unfilled_quantity, 50u);
    EXPECT_EQ(book.order_count(), 0u);
}
