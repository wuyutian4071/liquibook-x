#pragma once

#include <array>
#include <cassert>

#include "types.hpp"

namespace liquibook::itch {

// One plain-data payload struct per message type -- fields only, the common header lives
// once in DecodedMessage, not duplicated here. All trivially copyable.

struct SystemEventPayload {
    char event_code;
};

struct StockDirectoryPayload {
    std::array<char, 8> stock;
    char market_category;
    char financial_status_indicator;
    std::uint32_t round_lot_size;
    char round_lots_only;
    char issue_classification;
    std::array<char, 2> issue_subtype;
    char authenticity;
    char short_sale_threshold_indicator;
    char ipo_flag;
    char luld_reference_price_tier;
    char etp_flag;
    std::uint32_t etp_leverage_factor;
    char inverse_indicator;
};

// Covers both 'A' (Add Order, no MPID) and 'F' (Add Order with MPID). `attribution` is
// unused/zero-filled for a plain 'A' message -- check DecodedMessage::header.type to know
// whether it's meaningful.
struct AddOrderPayload {
    OrderRef order_reference_number;
    char buy_sell_indicator;
    Shares shares;
    std::array<char, 8> stock;
    Price4 price;
    std::array<char, 4> attribution;
};

struct OrderExecutedPayload {
    OrderRef order_reference_number;
    Shares executed_shares;
    MatchNumber match_number;
};

// Covers 'C' (Order Executed With Price). 'E' (plain Order Executed) uses
// OrderExecutedPayload instead; the two are kept separate rather than one struct with unused
// fields, since 'E' is by far the more common message and this keeps it minimal.
struct OrderExecutedWithPricePayload {
    OrderRef order_reference_number;
    Shares executed_shares;
    MatchNumber match_number;
    char printable;
    Price4 execution_price;
};

struct OrderCancelPayload {
    OrderRef order_reference_number;
    Shares cancelled_shares;
};

struct OrderDeletePayload {
    OrderRef order_reference_number;
};

struct OrderReplacePayload {
    OrderRef original_order_reference_number;
    OrderRef new_order_reference_number;
    Shares shares;
    Price4 price;
};

struct TradePayload {
    OrderRef order_reference_number;
    char buy_sell_indicator;
    Shares shares;
    std::array<char, 8> stock;
    Price4 price;
    MatchNumber match_number;
};

// A raw union, not std::variant: every payload above is a small trivially-copyable POD, so a
// union costs nothing beyond max-member-size storage -- no vtable, no allocation, no
// visitation overhead. The safety contract is simple: only read the member matching
// `header.type`; the as_*() accessors below assert this in debug builds. Hot-path code that
// already knows the type from its own switch can read `.payload.xxx` directly.
struct DecodedMessage {
    MessageHeader header;

    union Payload {
        SystemEventPayload system_event;
        StockDirectoryPayload stock_directory;
        AddOrderPayload add_order;
        OrderExecutedPayload order_executed;
        OrderExecutedWithPricePayload order_executed_with_price;
        OrderCancelPayload order_cancel;
        OrderDeletePayload order_delete;
        OrderReplacePayload order_replace;
        TradePayload trade;
    } payload;

    [[nodiscard]] const SystemEventPayload& as_system_event() const noexcept {
        assert(header.type == MessageType::SystemEvent);
        return payload.system_event;
    }

    [[nodiscard]] const StockDirectoryPayload& as_stock_directory() const noexcept {
        assert(header.type == MessageType::StockDirectory);
        return payload.stock_directory;
    }

    [[nodiscard]] const AddOrderPayload& as_add_order() const noexcept {
        assert(header.type == MessageType::AddOrder || header.type == MessageType::AddOrderMPID);
        return payload.add_order;
    }

    [[nodiscard]] const OrderExecutedPayload& as_order_executed() const noexcept {
        assert(header.type == MessageType::OrderExecuted);
        return payload.order_executed;
    }

    [[nodiscard]] const OrderExecutedWithPricePayload&
    as_order_executed_with_price() const noexcept {
        assert(header.type == MessageType::OrderExecutedWithPrice);
        return payload.order_executed_with_price;
    }

    [[nodiscard]] const OrderCancelPayload& as_order_cancel() const noexcept {
        assert(header.type == MessageType::OrderCancel);
        return payload.order_cancel;
    }

    [[nodiscard]] const OrderDeletePayload& as_order_delete() const noexcept {
        assert(header.type == MessageType::OrderDelete);
        return payload.order_delete;
    }

    [[nodiscard]] const OrderReplacePayload& as_order_replace() const noexcept {
        assert(header.type == MessageType::OrderReplace);
        return payload.order_replace;
    }

    [[nodiscard]] const TradePayload& as_trade() const noexcept {
        assert(header.type == MessageType::Trade);
        return payload.trade;
    }
};

} // namespace liquibook::itch
