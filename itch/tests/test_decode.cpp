#include <gtest/gtest.h>

#include <string_view>
#include <vector>

#include "decode.hpp"

using namespace liquibook::itch;

namespace {

// Small builder helpers so each test constructs a raw message field-by-field, matching the
// wire layout exactly, without a wall of magic hex bytes. Field values in these tests were
// cross-checked against an independent Python `struct.unpack` decode before being written
// down (see the M2 plan) -- Stock Directory and Add Order (MPID) specifically; the rest
// follow the same, by-then-verified offset table.

void append_u16(std::vector<std::byte>& buf, std::uint16_t v) {
    buf.push_back(static_cast<std::byte>((v >> 8) & 0xFF));
    buf.push_back(static_cast<std::byte>(v & 0xFF));
}

void append_u32(std::vector<std::byte>& buf, std::uint32_t v) {
    buf.push_back(static_cast<std::byte>((v >> 24) & 0xFF));
    buf.push_back(static_cast<std::byte>((v >> 16) & 0xFF));
    buf.push_back(static_cast<std::byte>((v >> 8) & 0xFF));
    buf.push_back(static_cast<std::byte>(v & 0xFF));
}

void append_u48(std::vector<std::byte>& buf, std::uint64_t v) {
    for (int shift = 40; shift >= 0; shift -= 8) {
        buf.push_back(static_cast<std::byte>((v >> shift) & 0xFF));
    }
}

void append_u64(std::vector<std::byte>& buf, std::uint64_t v) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        buf.push_back(static_cast<std::byte>((v >> shift) & 0xFF));
    }
}

void append_str(std::vector<std::byte>& buf, std::string_view s) {
    for (char c : s) {
        buf.push_back(static_cast<std::byte>(c));
    }
}

void append_header(std::vector<std::byte>& buf,
                   char type,
                   std::uint16_t locate,
                   std::uint16_t tracking,
                   std::uint64_t timestamp) {
    buf.push_back(static_cast<std::byte>(type));
    append_u16(buf, locate);
    append_u16(buf, tracking);
    append_u48(buf, timestamp);
}

} // namespace

TEST(Decode, SystemEvent) {
    std::vector<std::byte> buf;
    append_header(buf, 'S', 10, 20, 999);
    buf.push_back(static_cast<std::byte>('O'));
    ASSERT_EQ(buf.size(), 12u);

    auto msg = decode(buf);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->header.type, MessageType::SystemEvent);
    EXPECT_EQ(msg->header.stock_locate, 10);
    EXPECT_EQ(msg->header.tracking_number, 20);
    EXPECT_EQ(msg->header.timestamp, 999u);
    EXPECT_EQ(msg->as_system_event().event_code, 'O');
}

TEST(Decode, StockDirectory) {
    // Same field values independently verified via Python struct.unpack in the M2 plan.
    std::vector<std::byte> buf;
    append_header(buf, 'R', 100, 200, 12345);
    append_str(buf, "AAPL    ");
    buf.push_back(static_cast<std::byte>('Q')); // market_category
    buf.push_back(static_cast<std::byte>('N')); // financial_status_indicator
    append_u32(buf, 100);                       // round_lot_size
    buf.push_back(static_cast<std::byte>('Y')); // round_lots_only
    buf.push_back(static_cast<std::byte>('C')); // issue_classification
    append_str(buf, "  ");                      // issue_subtype
    buf.push_back(static_cast<std::byte>('P')); // authenticity
    buf.push_back(static_cast<std::byte>('N')); // short_sale_threshold_indicator
    buf.push_back(static_cast<std::byte>('N')); // ipo_flag
    buf.push_back(static_cast<std::byte>('1')); // luld_reference_price_tier
    buf.push_back(static_cast<std::byte>('N')); // etp_flag
    append_u32(buf, 0);                         // etp_leverage_factor
    buf.push_back(static_cast<std::byte>('N')); // inverse_indicator
    ASSERT_EQ(buf.size(), 39u);

    auto msg = decode(buf);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->header.type, MessageType::StockDirectory);
    EXPECT_EQ(msg->header.stock_locate, 100);
    EXPECT_EQ(msg->header.tracking_number, 200);
    EXPECT_EQ(msg->header.timestamp, 12345u);

    const auto& sd = msg->as_stock_directory();
    EXPECT_EQ(std::string_view(sd.stock.data(), sd.stock.size()), "AAPL    ");
    EXPECT_EQ(sd.market_category, 'Q');
    EXPECT_EQ(sd.financial_status_indicator, 'N');
    EXPECT_EQ(sd.round_lot_size, 100u);
    EXPECT_EQ(sd.round_lots_only, 'Y');
    EXPECT_EQ(sd.issue_classification, 'C');
    EXPECT_EQ(sd.authenticity, 'P');
    EXPECT_EQ(sd.short_sale_threshold_indicator, 'N');
    EXPECT_EQ(sd.ipo_flag, 'N');
    EXPECT_EQ(sd.luld_reference_price_tier, '1');
    EXPECT_EQ(sd.etp_flag, 'N');
    EXPECT_EQ(sd.etp_leverage_factor, 0u);
    EXPECT_EQ(sd.inverse_indicator, 'N');
}

TEST(Decode, AddOrderNoMpid) {
    std::vector<std::byte> buf;
    append_header(buf, 'A', 1, 2, 3);
    append_u64(buf, 555); // order_reference_number
    buf.push_back(static_cast<std::byte>('B'));
    append_u32(buf, 100);        // shares
    append_str(buf, "IBM     "); // stock
    append_u32(buf, 1234500);    // price
    ASSERT_EQ(buf.size(), 36u);

    auto msg = decode(buf);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->header.type, MessageType::AddOrder);
    const auto& ao = msg->as_add_order();
    EXPECT_EQ(ao.order_reference_number, 555u);
    EXPECT_EQ(ao.buy_sell_indicator, 'B');
    EXPECT_EQ(ao.shares, 100u);
    EXPECT_EQ(std::string_view(ao.stock.data(), ao.stock.size()), "IBM     ");
    EXPECT_EQ(ao.price, 1234500u);
}

TEST(Decode, AddOrderWithMpid) {
    // Same field values independently verified via Python struct.unpack in the M2 plan.
    std::vector<std::byte> buf;
    append_header(buf, 'F', 42, 7, 999999);
    append_u64(buf, 123456789); // order_reference_number
    buf.push_back(static_cast<std::byte>('B'));
    append_u32(buf, 500);        // shares
    append_str(buf, "MSFT    "); // stock
    append_u32(buf, 3000000);    // price
    append_str(buf, "EDGX");     // attribution
    ASSERT_EQ(buf.size(), 40u);

    auto msg = decode(buf);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->header.type, MessageType::AddOrderMPID);
    const auto& ao = msg->as_add_order();
    EXPECT_EQ(ao.order_reference_number, 123456789u);
    EXPECT_EQ(ao.buy_sell_indicator, 'B');
    EXPECT_EQ(ao.shares, 500u);
    EXPECT_EQ(std::string_view(ao.stock.data(), ao.stock.size()), "MSFT    ");
    EXPECT_EQ(ao.price, 3000000u);
    EXPECT_EQ(std::string_view(ao.attribution.data(), ao.attribution.size()), "EDGX");
}

TEST(Decode, OrderExecuted) {
    std::vector<std::byte> buf;
    append_header(buf, 'E', 1, 1, 1);
    append_u64(buf, 555);  // order_reference_number
    append_u32(buf, 50);   // executed_shares
    append_u64(buf, 7777); // match_number
    ASSERT_EQ(buf.size(), 31u);

    auto msg = decode(buf);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->header.type, MessageType::OrderExecuted);
    const auto& oe = msg->as_order_executed();
    EXPECT_EQ(oe.order_reference_number, 555u);
    EXPECT_EQ(oe.executed_shares, 50u);
    EXPECT_EQ(oe.match_number, 7777u);
}

TEST(Decode, OrderExecutedWithPrice) {
    std::vector<std::byte> buf;
    append_header(buf, 'C', 1, 1, 1);
    append_u64(buf, 555);                       // order_reference_number
    append_u32(buf, 50);                        // executed_shares
    append_u64(buf, 7777);                      // match_number
    buf.push_back(static_cast<std::byte>('Y')); // printable
    append_u32(buf, 1250000);                   // execution_price
    ASSERT_EQ(buf.size(), 36u);

    auto msg = decode(buf);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->header.type, MessageType::OrderExecutedWithPrice);
    const auto& oe = msg->as_order_executed_with_price();
    EXPECT_EQ(oe.order_reference_number, 555u);
    EXPECT_EQ(oe.executed_shares, 50u);
    EXPECT_EQ(oe.match_number, 7777u);
    EXPECT_EQ(oe.printable, 'Y');
    EXPECT_EQ(oe.execution_price, 1250000u);
}

TEST(Decode, OrderCancel) {
    std::vector<std::byte> buf;
    append_header(buf, 'X', 1, 1, 1);
    append_u64(buf, 555); // order_reference_number
    append_u32(buf, 25);  // cancelled_shares
    ASSERT_EQ(buf.size(), 23u);

    auto msg = decode(buf);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->header.type, MessageType::OrderCancel);
    const auto& oc = msg->as_order_cancel();
    EXPECT_EQ(oc.order_reference_number, 555u);
    EXPECT_EQ(oc.cancelled_shares, 25u);
}

TEST(Decode, OrderDelete) {
    std::vector<std::byte> buf;
    append_header(buf, 'D', 1, 1, 1);
    append_u64(buf, 555); // order_reference_number
    ASSERT_EQ(buf.size(), 19u);

    auto msg = decode(buf);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->header.type, MessageType::OrderDelete);
    EXPECT_EQ(msg->as_order_delete().order_reference_number, 555u);
}

TEST(Decode, OrderReplace) {
    std::vector<std::byte> buf;
    append_header(buf, 'U', 1, 1, 1);
    append_u64(buf, 555);     // original_order_reference_number
    append_u64(buf, 556);     // new_order_reference_number
    append_u32(buf, 200);     // shares
    append_u32(buf, 1300000); // price
    ASSERT_EQ(buf.size(), 35u);

    auto msg = decode(buf);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->header.type, MessageType::OrderReplace);
    const auto& orep = msg->as_order_replace();
    EXPECT_EQ(orep.original_order_reference_number, 555u);
    EXPECT_EQ(orep.new_order_reference_number, 556u);
    EXPECT_EQ(orep.shares, 200u);
    EXPECT_EQ(orep.price, 1300000u);
}

TEST(Decode, Trade) {
    std::vector<std::byte> buf;
    append_header(buf, 'P', 1, 1, 1);
    append_u64(buf, 555); // order_reference_number
    buf.push_back(static_cast<std::byte>('S'));
    append_u32(buf, 300);        // shares
    append_str(buf, "TSLA    "); // stock
    append_u32(buf, 2500000);    // price
    append_u64(buf, 8888);       // match_number
    ASSERT_EQ(buf.size(), 44u);

    auto msg = decode(buf);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->header.type, MessageType::Trade);
    const auto& tr = msg->as_trade();
    EXPECT_EQ(tr.order_reference_number, 555u);
    EXPECT_EQ(tr.buy_sell_indicator, 'S');
    EXPECT_EQ(tr.shares, 300u);
    EXPECT_EQ(std::string_view(tr.stock.data(), tr.stock.size()), "TSLA    ");
    EXPECT_EQ(tr.price, 2500000u);
    EXPECT_EQ(tr.match_number, 8888u);
}

TEST(Decode, EmptySpanReturnsNullopt) {
    EXPECT_FALSE(decode(std::span<const std::byte>{}).has_value());
}

TEST(Decode, UnrecognizedTypeReturnsNulloptWithoutCrashing) {
    std::vector<std::byte> buf;
    append_header(buf, 'Z', 1, 1, 1); // 'Z' is not a supported ITCH message type
    EXPECT_FALSE(decode(buf).has_value());
}

TEST(Decode, TooShortSpanReturnsNullopt) {
    std::vector<std::byte> buf;
    append_header(buf, 'A', 1, 1, 1); // Add Order needs 36 bytes total; header is only 11
    EXPECT_FALSE(decode(buf).has_value());
}
