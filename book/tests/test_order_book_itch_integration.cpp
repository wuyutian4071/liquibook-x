#include <gtest/gtest.h>

#include <map>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "decode.hpp"
#include "order_book.hpp"
#include "synth.hpp"

using liquibook::book::OrderBook;
namespace itch = liquibook::itch;

namespace {

struct ExpectedOrder {
    itch::Price4 price;
    itch::Shares shares;
    bool is_buy;
};

} // namespace

// The milestone's "ITCH-driven book building" requirement: a real synthetic ITCH stream
// (M2's itch::generate()), decoded with M2's real decode(), fed through OrderBook::apply()
// -- the actual M2 -> M4 integration path, not direct add_order()/execute_order() calls.
// Independently tracks expected live-order state by replaying the same decoded messages
// through separate, simple bookkeeping (not by trusting the generator's internal state),
// mirroring itch/tests/test_synth_roundtrip.cpp's own verification pattern.
TEST(OrderBookItchIntegration, BuildsFromARealSyntheticItchStream) {
    itch::SynthConfig config;
    config.seed = 7;
    config.num_orders = 500;
    config.symbols = {"AAPL"};
    config.base_price = 1'500'000; // $150.00
    config.price_spread = 50'000;  // $5.00 jitter range

    std::ostringstream oss(std::ios::binary);
    itch::generate(config, oss);
    const std::string data = oss.str();

    std::vector<std::byte> bytes(data.size());
    for (std::size_t i = 0; i < data.size(); ++i) {
        bytes[i] = static_cast<std::byte>(data[i]);
    }

    // Wide enough to comfortably cover base_price +- price_spread, so this run mostly stays
    // in the flat-array path -- exercising the common case through the real integration path.
    OrderBook book(config.base_price,
                   /*flat_array_half_width=*/config.price_spread + 1000,
                   /*order_capacity=*/config.num_orders + 10);

    std::unordered_map<itch::OrderRef, ExpectedOrder> expected_live;

    std::size_t offset = 0;
    std::size_t messages_applied = 0;
    while (offset + 2 <= bytes.size()) {
        const auto len =
            static_cast<std::uint16_t>((std::to_integer<unsigned>(bytes[offset]) << 8) |
                                       std::to_integer<unsigned>(bytes[offset + 1]));
        const std::size_t msg_start = offset + 2;
        ASSERT_LE(msg_start + len, bytes.size()) << "truncated record in generator output";

        const std::span<const std::byte> raw(bytes.data() + msg_start, len);
        const auto msg = itch::decode(raw);
        ASSERT_TRUE(msg.has_value()) << "generator produced an undecodable message";

        book.apply(*msg);
        ++messages_applied;

        switch (msg->header.type) {
        case itch::MessageType::AddOrder:
        case itch::MessageType::AddOrderMPID: {
            const auto& ao = msg->as_add_order();
            expected_live[ao.order_reference_number] =
                ExpectedOrder {ao.price, ao.shares, ao.buy_sell_indicator == 'B'};
            break;
        }
        case itch::MessageType::OrderExecuted: {
            const auto& oe = msg->as_order_executed();
            const auto it = expected_live.find(oe.order_reference_number);
            if (it != expected_live.end()) {
                it->second.shares -= oe.executed_shares;
                if (it->second.shares == 0) {
                    expected_live.erase(it);
                }
            }
            break;
        }
        case itch::MessageType::OrderCancel: {
            const auto& oc = msg->as_order_cancel();
            const auto it = expected_live.find(oc.order_reference_number);
            if (it != expected_live.end()) {
                it->second.shares -= oc.cancelled_shares;
                if (it->second.shares == 0) {
                    expected_live.erase(it);
                }
            }
            break;
        }
        case itch::MessageType::OrderDelete: {
            const auto& od = msg->as_order_delete();
            expected_live.erase(od.order_reference_number);
            break;
        }
        case itch::MessageType::OrderReplace: {
            const auto& orep = msg->as_order_replace();
            const auto it = expected_live.find(orep.original_order_reference_number);
            const bool is_buy = it != expected_live.end() ? it->second.is_buy : true;
            expected_live.erase(orep.original_order_reference_number);
            expected_live[orep.new_order_reference_number] =
                ExpectedOrder {orep.price, orep.shares, is_buy};
            break;
        }
        default:
            break; // System Event, Stock Directory, Trade: no book-state effect
        }

        offset = msg_start + len;
    }

    EXPECT_GT(messages_applied, 0u);
    EXPECT_EQ(offset, bytes.size());

    // The headline check: the book's order count matches independently-tracked live state.
    EXPECT_EQ(book.order_count(), expected_live.size());

    // Spot-check aggregate shares at every (price, side) actually touched -- a real,
    // end-to-end verification of the M2 -> M4 integration, not just "nothing crashed".
    std::map<std::pair<itch::Price4, bool>, itch::Shares> expected_shares_at;
    for (const auto& [ref, order] : expected_live) {
        expected_shares_at[{order.price, order.is_buy}] += order.shares;
    }
    for (const auto& [key, shares] : expected_shares_at) {
        const auto [price, is_buy] = key;
        EXPECT_EQ(book.shares_at(price, is_buy), shares)
            << "shares mismatch at price " << price << " is_buy=" << is_buy;
    }
}
