#include <gtest/gtest.h>

#include "order_book.hpp"

using liquibook::book::OrderBook;

namespace {

// $100.00 reference, a small flat-array window (+-50 raw Price4 units = +-$0.005) so tests
// can exercise both the flat-array path and the fallback-map path deliberately.
constexpr std::uint32_t kRef = 1'000'000;
constexpr std::size_t kHalfWidth = 50;
constexpr std::size_t kCapacity = 64;

OrderBook make_book() {
    return OrderBook(kRef, kHalfWidth, kCapacity);
}

} // namespace

TEST(OrderBook, StartsWithNoBestBidOrAsk) {
    OrderBook book = make_book();
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_EQ(book.order_count(), 0u);
}

TEST(OrderBook, AddOrderSetsBestBidAndAsk) {
    OrderBook book = make_book();
    ASSERT_TRUE(book.add_order(1, kRef, 100, /*is_buy=*/true));
    ASSERT_TRUE(book.add_order(2, kRef + 10, 200, /*is_buy=*/false));

    ASSERT_TRUE(book.best_bid().has_value());
    EXPECT_EQ(*book.best_bid(), kRef);
    ASSERT_TRUE(book.best_ask().has_value());
    EXPECT_EQ(*book.best_ask(), kRef + 10);
    EXPECT_EQ(book.order_count(), 2u);
}

TEST(OrderBook, BestBidTracksTheHighestPrice) {
    OrderBook book = make_book();
    ASSERT_TRUE(book.add_order(1, kRef - 5, 100, true));
    ASSERT_TRUE(book.add_order(2, kRef + 5, 100, true)); // better bid (higher)
    ASSERT_TRUE(book.add_order(3, kRef, 100, true));     // not better

    ASSERT_TRUE(book.best_bid().has_value());
    EXPECT_EQ(*book.best_bid(), kRef + 5);
}

TEST(OrderBook, BestAskTracksTheLowestPrice) {
    OrderBook book = make_book();
    ASSERT_TRUE(book.add_order(1, kRef + 5, 100, false));
    ASSERT_TRUE(book.add_order(2, kRef - 5, 100, false)); // better ask (lower)
    ASSERT_TRUE(book.add_order(3, kRef, 100, false));     // not better

    ASSERT_TRUE(book.best_ask().has_value());
    EXPECT_EQ(*book.best_ask(), kRef - 5);
}

TEST(OrderBook, ExecutePartialReducesSharesWithoutRemovingOrder) {
    OrderBook book = make_book();
    ASSERT_TRUE(book.add_order(1, kRef, 100, true));
    ASSERT_TRUE(book.execute_order(1, 40));

    EXPECT_EQ(book.shares_at(kRef, true), 60u);
    EXPECT_EQ(book.order_count(), 1u);
    ASSERT_TRUE(book.best_bid().has_value());
    EXPECT_EQ(*book.best_bid(), kRef);
}

TEST(OrderBook, ExecuteFullRemovesOrderAndUpdatesBest) {
    OrderBook book = make_book();
    ASSERT_TRUE(book.add_order(1, kRef, 100, true));
    ASSERT_TRUE(book.execute_order(1, 100));

    EXPECT_EQ(book.order_count(), 0u);
    EXPECT_EQ(book.shares_at(kRef, true), 0u);
    EXPECT_FALSE(book.best_bid().has_value());
}

TEST(OrderBook, CancelPartialReducesShares) {
    OrderBook book = make_book();
    ASSERT_TRUE(book.add_order(1, kRef, 100, false));
    ASSERT_TRUE(book.cancel_order(1, 30));
    EXPECT_EQ(book.shares_at(kRef, false), 70u);
}

TEST(OrderBook, DeleteOrderRemovesItEntirely) {
    OrderBook book = make_book();
    ASSERT_TRUE(book.add_order(1, kRef, 100, true));
    ASSERT_TRUE(book.delete_order(1));
    EXPECT_EQ(book.order_count(), 0u);
    EXPECT_FALSE(book.best_bid().has_value());
}

TEST(OrderBook, BestBidWalksToNextLevelWhenTopEmpties) {
    OrderBook book = make_book();
    ASSERT_TRUE(book.add_order(1, kRef + 10, 100, true)); // best
    ASSERT_TRUE(book.add_order(2, kRef + 5, 100, true));  // second best
    ASSERT_TRUE(book.add_order(3, kRef, 100, true));      // third

    ASSERT_TRUE(book.best_bid().has_value());
    EXPECT_EQ(*book.best_bid(), kRef + 10);

    ASSERT_TRUE(book.delete_order(1)); // empty the current best level
    ASSERT_TRUE(book.best_bid().has_value());
    EXPECT_EQ(*book.best_bid(), kRef + 5);

    ASSERT_TRUE(book.delete_order(2));
    ASSERT_TRUE(book.best_bid().has_value());
    EXPECT_EQ(*book.best_bid(), kRef);

    ASSERT_TRUE(book.delete_order(3));
    EXPECT_FALSE(book.best_bid().has_value());
}

TEST(OrderBook, BestAskWalksToNextLevelWhenTopEmpties) {
    OrderBook book = make_book();
    ASSERT_TRUE(book.add_order(1, kRef - 10, 100, false)); // best (lowest)
    ASSERT_TRUE(book.add_order(2, kRef - 5, 100, false));
    ASSERT_TRUE(book.add_order(3, kRef, 100, false));

    ASSERT_TRUE(book.delete_order(1));
    ASSERT_TRUE(book.best_ask().has_value());
    EXPECT_EQ(*book.best_ask(), kRef - 5);
}

TEST(OrderBook, ReplaceOrderMovesPriceAndShares) {
    OrderBook book = make_book();
    ASSERT_TRUE(book.add_order(1, kRef, 100, true));
    ASSERT_TRUE(book.replace_order(1, 2, 250, kRef + 5));

    EXPECT_EQ(book.shares_at(kRef, true), 0u);       // old price/order gone
    EXPECT_EQ(book.shares_at(kRef + 5, true), 250u); // new price/order live
    EXPECT_EQ(book.order_count(), 1u);
    ASSERT_TRUE(book.best_bid().has_value());
    EXPECT_EQ(*book.best_bid(), kRef + 5);
}

TEST(OrderBook, PricesOutsideFlatArrayUseTheFallbackMap) {
    OrderBook book = make_book();
    const std::uint32_t outlier_price = kRef + kHalfWidth + 1000; // well outside the window
    ASSERT_TRUE(book.add_order(1, outlier_price, 100, true));

    EXPECT_EQ(book.shares_at(outlier_price, true), 100u);
    ASSERT_TRUE(book.best_bid().has_value());
    EXPECT_EQ(*book.best_bid(), outlier_price);
}

TEST(OrderBook, PriceExactlyAtTheFlatArrayBoundaryWorks) {
    OrderBook book = make_book();
    const std::uint32_t edge_price =
        kRef + static_cast<std::uint32_t>(kHalfWidth); // last valid flat slot
    ASSERT_TRUE(book.add_order(1, edge_price, 100, true));
    EXPECT_EQ(book.shares_at(edge_price, true), 100u);
}

TEST(OrderBook, OperatingOnAnUnknownOrderRefFailsCleanly) {
    OrderBook book = make_book();
    EXPECT_FALSE(book.execute_order(999, 10));
    EXPECT_FALSE(book.cancel_order(999, 10));
    EXPECT_FALSE(book.delete_order(999));
    EXPECT_FALSE(book.replace_order(999, 1000, 10, kRef));
}

TEST(OrderBook, OperatingOnAnAlreadyRemovedOrderFailsCleanly) {
    OrderBook book = make_book();
    ASSERT_TRUE(book.add_order(1, kRef, 100, true));
    ASSERT_TRUE(book.delete_order(1));

    EXPECT_FALSE(book.execute_order(1, 10));
    EXPECT_FALSE(book.cancel_order(1, 10));
    EXPECT_FALSE(book.delete_order(1));
}

TEST(OrderBook, DuplicateOrderRefIsRejected) {
    OrderBook book = make_book();
    ASSERT_TRUE(book.add_order(1, kRef, 100, true));
    EXPECT_FALSE(book.add_order(1, kRef + 1, 50, true));
    EXPECT_EQ(book.order_count(), 1u);
}

TEST(OrderBook, ExecutingMoreSharesThanRemainingFailsCleanly) {
    OrderBook book = make_book();
    ASSERT_TRUE(book.add_order(1, kRef, 100, true));
    EXPECT_FALSE(book.execute_order(1, 101));
    EXPECT_EQ(book.shares_at(kRef, true), 100u);
}

TEST(OrderBook, BidsAndAsksAtTheSamePriceAreTrackedIndependently) {
    OrderBook book = make_book();
    ASSERT_TRUE(book.add_order(1, kRef, 100, true));
    ASSERT_TRUE(book.add_order(2, kRef, 200, false));

    EXPECT_EQ(book.shares_at(kRef, true), 100u);
    EXPECT_EQ(book.shares_at(kRef, false), 200u);
    ASSERT_TRUE(book.best_bid().has_value());
    EXPECT_EQ(*book.best_bid(), kRef);
    ASSERT_TRUE(book.best_ask().has_value());
    EXPECT_EQ(*book.best_ask(), kRef);
}

TEST(OrderBook, AddOrderPropagatesTraderId) {
    OrderBook book = make_book();
    ASSERT_TRUE(book.add_order(1, kRef, 100, true, /*trader_id=*/7));

    const auto* level = book.best_bid_level();
    ASSERT_NE(level, nullptr);
    ASSERT_FALSE(level->bids.empty());
    EXPECT_EQ(level->bids.front()->trader_id, 7u);
}

TEST(OrderBook, AddOrderDefaultsTraderIdToZero) {
    OrderBook book = make_book();
    ASSERT_TRUE(book.add_order(1, kRef, 100, true));

    const auto* level = book.best_bid_level();
    ASSERT_NE(level, nullptr);
    EXPECT_EQ(level->bids.front()->trader_id, 0u);
}

TEST(OrderBook, BestBidAskLevelIsNullWhenEmpty) {
    OrderBook book = make_book();
    EXPECT_EQ(book.best_bid_level(), nullptr);
    EXPECT_EQ(book.best_ask_level(), nullptr);
}

TEST(OrderBook, BestBidAskLevelReturnsTheCachedBest) {
    OrderBook book = make_book();
    ASSERT_TRUE(book.add_order(1, kRef - 5, 100, true));
    ASSERT_TRUE(book.add_order(2, kRef + 5, 100, true));
    ASSERT_TRUE(book.add_order(3, kRef + 10, 50, false));

    const auto* bid_level = book.best_bid_level();
    ASSERT_NE(bid_level, nullptr);
    EXPECT_EQ(bid_level->price, kRef + 5);

    const auto* ask_level = book.best_ask_level();
    ASSERT_NE(ask_level, nullptr);
    EXPECT_EQ(ask_level->price, kRef + 10);
}

TEST(OrderBook, NextLevelAfterWalksTheFlatArrayInPriorityOrder) {
    OrderBook book = make_book();
    ASSERT_TRUE(book.add_order(1, kRef + 10, 100, true));
    ASSERT_TRUE(book.add_order(2, kRef + 5, 100, true));
    ASSERT_TRUE(book.add_order(3, kRef, 100, true));

    const auto* best = book.best_bid_level();
    ASSERT_NE(best, nullptr);
    EXPECT_EQ(best->price, kRef + 10);

    const auto* second = book.next_level_after(best->price, /*is_buy=*/true);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->price, kRef + 5);

    const auto* third = book.next_level_after(second->price, /*is_buy=*/true);
    ASSERT_NE(third, nullptr);
    EXPECT_EQ(third->price, kRef);

    EXPECT_EQ(book.next_level_after(third->price, /*is_buy=*/true), nullptr);
}

TEST(OrderBook, NextLevelAfterCrossesFromFlatArrayIntoOutlierMap) {
    OrderBook book = make_book();
    const auto outlier_price = static_cast<std::uint32_t>(kRef - kHalfWidth - 1000);
    ASSERT_TRUE(book.add_order(1, kRef - static_cast<std::uint32_t>(kHalfWidth), 100, true));
    ASSERT_TRUE(book.add_order(2, outlier_price, 100, true));

    const auto* flat_edge = book.best_bid_level();
    ASSERT_NE(flat_edge, nullptr);
    EXPECT_EQ(flat_edge->price, kRef - kHalfWidth);

    const auto* next = book.next_level_after(flat_edge->price, /*is_buy=*/true);
    ASSERT_NE(next, nullptr);
    EXPECT_EQ(next->price, outlier_price);
    EXPECT_EQ(book.next_level_after(next->price, /*is_buy=*/true), nullptr);
}

TEST(OrderBook, NextLevelAfterSkipsEmptiedLevels) {
    OrderBook book = make_book();
    ASSERT_TRUE(book.add_order(1, kRef + 10, 100, false));
    ASSERT_TRUE(book.add_order(2, kRef + 5, 100, false)); // will be fully cancelled
    ASSERT_TRUE(book.add_order(3, kRef, 100, false));
    ASSERT_TRUE(book.cancel_order(2, 100));

    const auto* best = book.best_ask_level();
    ASSERT_NE(best, nullptr);
    EXPECT_EQ(best->price, kRef);

    const auto* next = book.next_level_after(best->price, /*is_buy=*/false);
    ASSERT_NE(next, nullptr);
    EXPECT_EQ(next->price, kRef + 10); // kRef+5 is empty now, skipped entirely
}
