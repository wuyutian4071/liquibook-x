#pragma once

#include <cstdint>

namespace liquibook::itch {

// NASDAQ TotalView-ITCH 5.0. Every message begins with an 11-byte common header (type +
// stock_locate + tracking_number + timestamp) followed by type-specific fields. All fields
// are big-endian. This project supports the 10 message types most relevant to book-building:
// System Event, Stock Directory, Add Order (with/without MPID), Order Executed (with/without
// price), Order Cancel, Order Delete, Order Replace, and Trade (non-cross).

using Timestamp = std::uint64_t; // nanoseconds since midnight (48 bits used)
using OrderRef = std::uint64_t;
using Price4 = std::uint32_t; // price in 1/10000ths of a dollar (4 decimal places)
using Shares = std::uint32_t;
using StockLocate = std::uint16_t;
using TrackingNumber = std::uint16_t;
using MatchNumber = std::uint64_t;

enum class MessageType : char {
    SystemEvent = 'S',
    StockDirectory = 'R',
    AddOrder = 'A',
    AddOrderMPID = 'F',
    OrderExecuted = 'E',
    OrderExecutedWithPrice = 'C',
    OrderCancel = 'X',
    OrderDelete = 'D',
    OrderReplace = 'U',
    Trade = 'P',
};

struct MessageHeader {
    MessageType type;
    StockLocate stock_locate;
    TrackingNumber tracking_number;
    Timestamp timestamp;
};

// Total on-wire length in bytes (including the 1-byte type and the 11-byte common header),
// as declared by NASDAQ's ITCH 5.0 spec and used as the length-prefix value in sample files.
// Returns 0 for a type this project doesn't support.
[[nodiscard]] constexpr std::size_t wire_length(MessageType type) noexcept {
    switch (type) {
    case MessageType::SystemEvent:
        return 12;
    case MessageType::StockDirectory:
        return 39;
    case MessageType::AddOrder:
        return 36;
    case MessageType::AddOrderMPID:
        return 40;
    case MessageType::OrderExecuted:
        return 31;
    case MessageType::OrderExecutedWithPrice:
        return 36;
    case MessageType::OrderCancel:
        return 23;
    case MessageType::OrderDelete:
        return 19;
    case MessageType::OrderReplace:
        return 35;
    case MessageType::Trade:
        return 44;
    }
    return 0;
}

} // namespace liquibook::itch
