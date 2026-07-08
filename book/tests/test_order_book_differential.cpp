#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <unordered_map>
#include <unordered_set>

#include "order_book.hpp"
#include "reference_order_book.hpp"

using liquibook::book::OrderBook;
using liquibook::book::testing::ReferenceOrderBook;
namespace itch = liquibook::itch;

namespace {

struct LiveOrder {
    itch::Price4 price;
    itch::Shares shares;
    bool is_buy;
};

// Compares every externally observable piece of state, not just top-of-book: best bid/ask,
// order count, and shares_at() for every price the run has ever touched (a stronger check
// than top-of-book-only comparison -- a bug corrupting a non-best level wouldn't necessarily
// show up in best_bid()/best_ask() alone). Uses EXPECT_* (not ASSERT_*) since this is a
// free function, not the TEST body itself -- the caller checks HasFailure() to stop the run
// early on the first divergence rather than flooding output with cascading failures.
void expect_books_match(const OrderBook& book,
                        const ReferenceOrderBook& reference,
                        const std::unordered_set<itch::Price4>& touched_prices) {
    EXPECT_EQ(book.best_bid(), reference.best_bid());
    EXPECT_EQ(book.best_ask(), reference.best_ask());
    EXPECT_EQ(book.order_count(), reference.order_count());
    for (itch::Price4 price : touched_prices) {
        EXPECT_EQ(book.shares_at(price, true), reference.shares_at(price, true))
            << "bid shares mismatch at price " << price;
        EXPECT_EQ(book.shares_at(price, false), reference.shares_at(price, false))
            << "ask shares mismatch at price " << price;
    }
}

} // namespace

TEST(OrderBookDifferential, RandomStreamMatchesReferenceAfterEveryOperation) {
    constexpr itch::Price4 kRef = 1'000'000;
    constexpr std::size_t kHalfWidth = 200;
    constexpr std::size_t kCapacity = 5000;
    // The reference book is deliberately O(live orders) per query (see its own doc comment)
    // -- comparing after *every* operation makes the whole run O(n^2). 400 keeps this test
    // in the "a few seconds" range while still exercising the full mix of operation types
    // and both the flat-array and fallback-map price paths many times over.
    constexpr std::size_t kNumOrders = 400;

    OrderBook book(kRef, kHalfWidth, kCapacity);
    ReferenceOrderBook reference;

    std::mt19937_64 rng(42);
    std::uniform_int_distribution<int> action_dist(0, 99);
    // Spans well past the flat array's +-200 window on both sides, so the run exercises the
    // fallback-map path too, not just the flat array.
    std::uniform_int_distribution<std::int32_t> price_jitter(-400, 400);
    std::uniform_int_distribution<itch::Shares> shares_dist(10, 500);
    std::uniform_int_distribution<int> side_dist(0, 1);

    std::unordered_map<itch::OrderRef, LiveOrder> live;
    std::unordered_set<itch::Price4> touched_prices;
    itch::OrderRef next_ref = 1;
    std::size_t orders_added = 0;

    while (orders_added < kNumOrders) {
        const int action = live.empty() ? 0 : action_dist(rng);

        if (action < 55) {
            const auto price =
                static_cast<itch::Price4>(static_cast<std::int64_t>(kRef) + price_jitter(rng));
            const itch::Shares shares = shares_dist(rng);
            const bool is_buy = side_dist(rng) == 0;
            const itch::OrderRef ref = next_ref++;

            const bool book_ok = book.add_order(ref, price, shares, is_buy);
            const bool ref_ok = reference.add_order(ref, price, shares, is_buy);
            ASSERT_EQ(book_ok, ref_ok);
            if (book_ok) {
                live[ref] = LiveOrder {price, shares, is_buy};
                touched_prices.insert(price);
                ++orders_added;
            }
        } else {
            auto it = live.begin();
            std::advance(it, static_cast<std::ptrdiff_t>(rng() % live.size()));
            const itch::OrderRef ref = it->first;
            LiveOrder& order = it->second;

            if (action < 70) {
                const itch::Shares exec = std::min(order.shares, shares_dist(rng));
                ASSERT_EQ(book.execute_order(ref, exec), reference.execute_order(ref, exec));
                order.shares -= exec;
                if (order.shares == 0) {
                    live.erase(it);
                }
            } else if (action < 80) {
                const itch::Shares cancel = std::min(order.shares, shares_dist(rng));
                ASSERT_EQ(book.cancel_order(ref, cancel), reference.cancel_order(ref, cancel));
                order.shares -= cancel;
                if (order.shares == 0) {
                    live.erase(it);
                }
            } else if (action < 90) {
                ASSERT_EQ(book.delete_order(ref), reference.delete_order(ref));
                live.erase(it);
            } else {
                const auto new_price =
                    static_cast<itch::Price4>(static_cast<std::int64_t>(kRef) + price_jitter(rng));
                const itch::Shares new_shares = shares_dist(rng);
                const bool is_buy = order.is_buy;
                const itch::OrderRef new_ref = next_ref++;
                ASSERT_EQ(book.replace_order(ref, new_ref, new_shares, new_price),
                          reference.replace_order(ref, new_ref, new_shares, new_price));
                live.erase(it);
                live[new_ref] = LiveOrder {new_price, new_shares, is_buy};
                touched_prices.insert(new_price);
            }
        }

        expect_books_match(book, reference, touched_prices);
        if (::testing::Test::HasFailure()) {
            FAIL() << "book and reference diverged after " << orders_added << " orders added";
        }
    }

    EXPECT_EQ(book.order_count(), live.size());
    EXPECT_EQ(reference.order_count(), live.size());
}
