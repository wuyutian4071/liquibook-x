#include "decode.hpp"

#include "byteorder.hpp"

namespace liquibook::itch {

namespace {

[[nodiscard]] char read_char(const std::byte* p) noexcept {
    return static_cast<char>(std::to_integer<unsigned char>(*p));
}

template <std::size_t N>
void read_chars(const std::byte* p, std::array<char, N>& out) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
        out[i] = read_char(p + i);
    }
}

[[nodiscard]] MessageHeader decode_header(std::span<const std::byte> raw,
                                          MessageType type) noexcept {
    const auto* p = raw.data();
    return MessageHeader{
        .type = type,
        .stock_locate = read_u16_be(p + 1),
        .tracking_number = read_u16_be(p + 3),
        .timestamp = read_u48_be(p + 5),
    };
}

[[nodiscard]] DecodedMessage decode_system_event(std::span<const std::byte> raw) noexcept {
    DecodedMessage msg{};
    msg.header = decode_header(raw, MessageType::SystemEvent);
    msg.payload.system_event.event_code = read_char(raw.data() + 11);
    return msg;
}

[[nodiscard]] DecodedMessage decode_stock_directory(std::span<const std::byte> raw) noexcept {
    DecodedMessage msg{};
    msg.header = decode_header(raw, MessageType::StockDirectory);
    auto& payload = msg.payload.stock_directory;
    const auto* p = raw.data();

    read_chars(p + 11, payload.stock);
    payload.market_category = read_char(p + 19);
    payload.financial_status_indicator = read_char(p + 20);
    payload.round_lot_size = read_u32_be(p + 21);
    payload.round_lots_only = read_char(p + 25);
    payload.issue_classification = read_char(p + 26);
    read_chars(p + 27, payload.issue_subtype);
    payload.authenticity = read_char(p + 29);
    payload.short_sale_threshold_indicator = read_char(p + 30);
    payload.ipo_flag = read_char(p + 31);
    payload.luld_reference_price_tier = read_char(p + 32);
    payload.etp_flag = read_char(p + 33);
    payload.etp_leverage_factor = read_u32_be(p + 34);
    payload.inverse_indicator = read_char(p + 38);
    return msg;
}

[[nodiscard]] DecodedMessage decode_add_order(std::span<const std::byte> raw,
                                              MessageType type) noexcept {
    DecodedMessage msg{};
    msg.header = decode_header(raw, type);
    auto& payload = msg.payload.add_order;
    const auto* p = raw.data();

    payload.order_reference_number = read_u64_be(p + 11);
    payload.buy_sell_indicator = read_char(p + 19);
    payload.shares = read_u32_be(p + 20);
    read_chars(p + 24, payload.stock);
    payload.price = read_u32_be(p + 32);
    if (type == MessageType::AddOrderMPID) {
        read_chars(p + 36, payload.attribution);
    } else {
        payload.attribution = {};
    }
    return msg;
}

[[nodiscard]] DecodedMessage decode_order_executed(std::span<const std::byte> raw) noexcept {
    DecodedMessage msg{};
    msg.header = decode_header(raw, MessageType::OrderExecuted);
    auto& payload = msg.payload.order_executed;
    const auto* p = raw.data();

    payload.order_reference_number = read_u64_be(p + 11);
    payload.executed_shares = read_u32_be(p + 19);
    payload.match_number = read_u64_be(p + 23);
    return msg;
}

[[nodiscard]] DecodedMessage
decode_order_executed_with_price(std::span<const std::byte> raw) noexcept {
    DecodedMessage msg{};
    msg.header = decode_header(raw, MessageType::OrderExecutedWithPrice);
    auto& payload = msg.payload.order_executed_with_price;
    const auto* p = raw.data();

    payload.order_reference_number = read_u64_be(p + 11);
    payload.executed_shares = read_u32_be(p + 19);
    payload.match_number = read_u64_be(p + 23);
    payload.printable = read_char(p + 31);
    payload.execution_price = read_u32_be(p + 32);
    return msg;
}

[[nodiscard]] DecodedMessage decode_order_cancel(std::span<const std::byte> raw) noexcept {
    DecodedMessage msg{};
    msg.header = decode_header(raw, MessageType::OrderCancel);
    auto& payload = msg.payload.order_cancel;
    const auto* p = raw.data();

    payload.order_reference_number = read_u64_be(p + 11);
    payload.cancelled_shares = read_u32_be(p + 19);
    return msg;
}

[[nodiscard]] DecodedMessage decode_order_delete(std::span<const std::byte> raw) noexcept {
    DecodedMessage msg{};
    msg.header = decode_header(raw, MessageType::OrderDelete);
    msg.payload.order_delete.order_reference_number = read_u64_be(raw.data() + 11);
    return msg;
}

[[nodiscard]] DecodedMessage decode_order_replace(std::span<const std::byte> raw) noexcept {
    DecodedMessage msg{};
    msg.header = decode_header(raw, MessageType::OrderReplace);
    auto& payload = msg.payload.order_replace;
    const auto* p = raw.data();

    payload.original_order_reference_number = read_u64_be(p + 11);
    payload.new_order_reference_number = read_u64_be(p + 19);
    payload.shares = read_u32_be(p + 27);
    payload.price = read_u32_be(p + 31);
    return msg;
}

[[nodiscard]] DecodedMessage decode_trade(std::span<const std::byte> raw) noexcept {
    DecodedMessage msg{};
    msg.header = decode_header(raw, MessageType::Trade);
    auto& payload = msg.payload.trade;
    const auto* p = raw.data();

    payload.order_reference_number = read_u64_be(p + 11);
    payload.buy_sell_indicator = read_char(p + 19);
    payload.shares = read_u32_be(p + 20);
    read_chars(p + 24, payload.stock);
    payload.price = read_u32_be(p + 32);
    payload.match_number = read_u64_be(p + 36);
    return msg;
}

} // namespace

std::optional<DecodedMessage> decode(std::span<const std::byte> raw) noexcept {
    if (raw.empty()) {
        return std::nullopt;
    }

    const auto type = static_cast<MessageType>(read_char(raw.data()));
    const std::size_t required = wire_length(type);
    if (required == 0 || raw.size() < required) {
        return std::nullopt;
    }

    switch (type) {
    case MessageType::SystemEvent:
        return decode_system_event(raw);
    case MessageType::StockDirectory:
        return decode_stock_directory(raw);
    case MessageType::AddOrder:
    case MessageType::AddOrderMPID:
        return decode_add_order(raw, type);
    case MessageType::OrderExecuted:
        return decode_order_executed(raw);
    case MessageType::OrderExecutedWithPrice:
        return decode_order_executed_with_price(raw);
    case MessageType::OrderCancel:
        return decode_order_cancel(raw);
    case MessageType::OrderDelete:
        return decode_order_delete(raw);
    case MessageType::OrderReplace:
        return decode_order_replace(raw);
    case MessageType::Trade:
        return decode_trade(raw);
    }
    return std::nullopt;
}

} // namespace liquibook::itch
