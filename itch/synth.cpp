#include "synth.hpp"

#include <algorithm>
#include <array>
#include <random>
#include <string_view>
#include <unordered_map>

#include "byteorder.hpp"

namespace liquibook::itch {

namespace {

struct LiveOrder {
    std::size_t symbol_index;
    Shares remaining_shares;
    Price4 price;
    char side;
};

void append_u16(std::vector<std::byte>& buf, std::uint16_t v) {
    buf.push_back(static_cast<std::byte>((v >> 8) & 0xFF));
    buf.push_back(static_cast<std::byte>(v & 0xFF));
}

void append_u32(std::vector<std::byte>& buf, std::uint32_t v) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        buf.push_back(static_cast<std::byte>((v >> shift) & 0xFF));
    }
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

void append_char(std::vector<std::byte>& buf, char c) {
    buf.push_back(static_cast<std::byte>(c));
}

void append_str_padded(std::vector<std::byte>& buf, std::string_view s, std::size_t width) {
    for (std::size_t i = 0; i < width; ++i) {
        buf.push_back(static_cast<std::byte>(i < s.size() ? s[i] : ' '));
    }
}

void append_header(std::vector<std::byte>& buf,
                   char type,
                   StockLocate locate,
                   TrackingNumber tracking,
                   Timestamp ts) {
    append_char(buf, type);
    append_u16(buf, locate);
    append_u16(buf, tracking);
    append_u48(buf, ts);
}

void write_framed(std::ostream& out, const std::vector<std::byte>& msg) {
    std::array<std::byte, 2> len {};
    write_u16_be(len.data(), static_cast<std::uint16_t>(msg.size()));
    out.write(reinterpret_cast<const char*>(len.data()), 2);
    out.write(reinterpret_cast<const char*>(msg.data()), static_cast<std::streamsize>(msg.size()));
}

} // namespace

void generate(const SynthConfig& config, std::ostream& out) {
    std::mt19937_64 rng(config.seed);
    std::uniform_int_distribution<int> action_dist(0, 99);
    std::uniform_int_distribution<std::size_t> symbol_dist(
        0, config.symbols.empty() ? 0 : config.symbols.size() - 1);
    std::uniform_int_distribution<std::uint32_t> shares_dist(10, 1000);
    std::uniform_int_distribution<std::int32_t> jitter_dist(
        -static_cast<std::int32_t>(config.price_spread),
        static_cast<std::int32_t>(config.price_spread));
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int> mpid_dist(0, 2);

    TrackingNumber tracking = 1;
    Timestamp ts = 0;
    OrderRef next_order_ref = 1;
    MatchNumber next_match_number = 1;
    std::unordered_map<OrderRef, LiveOrder> live_orders;

    auto next_ts = [&ts]() -> Timestamp {
        ts += 1000;
        return ts;
    };

    // 1. System Event: start of messages.
    {
        std::vector<std::byte> msg;
        append_header(msg, 'S', 0, tracking++, next_ts());
        append_char(msg, 'O');
        write_framed(out, msg);
    }

    // 2. Stock Directory, one per symbol.
    for (std::size_t i = 0; i < config.symbols.size(); ++i) {
        std::vector<std::byte> msg;
        append_header(msg, 'R', static_cast<StockLocate>(i + 1), tracking++, next_ts());
        append_str_padded(msg, config.symbols[i], 8);
        append_char(msg, 'Q');         // market_category
        append_char(msg, 'N');         // financial_status_indicator
        append_u32(msg, 100);          // round_lot_size
        append_char(msg, 'Y');         // round_lots_only
        append_char(msg, 'C');         // issue_classification
        append_str_padded(msg, "", 2); // issue_subtype
        append_char(msg, 'P');         // authenticity
        append_char(msg, 'N');         // short_sale_threshold_indicator
        append_char(msg, 'N');         // ipo_flag
        append_char(msg, '1');         // luld_reference_price_tier
        append_char(msg, 'N');         // etp_flag
        append_u32(msg, 0);            // etp_leverage_factor
        append_char(msg, 'N');         // inverse_indicator
        write_framed(out, msg);
    }

    // 3. Randomized order flow: interleave Add Order with Executed/Cancel/Delete/Replace
    // (always against a currently-live order) and independent Trade reports, until
    // config.num_orders Add Order messages have been emitted.
    std::size_t orders_added = 0;
    while (orders_added < config.num_orders) {
        const std::size_t symbol_index = symbol_dist(rng);
        const StockLocate locate = static_cast<StockLocate>(symbol_index + 1);
        const int action = live_orders.empty() ? 0 : action_dist(rng);

        if (action < 55) {
            const char side = side_dist(rng) == 0 ? 'B' : 'S';
            const Shares shares = shares_dist(rng);
            const auto price = static_cast<Price4>(static_cast<std::int64_t>(config.base_price) +
                                                   jitter_dist(rng));
            const bool with_mpid = mpid_dist(rng) == 0;
            const std::string_view symbol =
                config.symbols.empty() ? "TEST" : std::string_view(config.symbols[symbol_index]);

            std::vector<std::byte> msg;
            append_header(msg, with_mpid ? 'F' : 'A', locate, tracking++, next_ts());
            append_u64(msg, next_order_ref);
            append_char(msg, side);
            append_u32(msg, shares);
            append_str_padded(msg, symbol, 8);
            append_u32(msg, price);
            if (with_mpid) {
                append_str_padded(msg, "EDGX", 4);
            }
            write_framed(out, msg);

            live_orders[next_order_ref] = LiveOrder {symbol_index, shares, price, side};
            ++next_order_ref;
            ++orders_added;
            continue;
        }

        auto it = live_orders.begin();
        std::advance(it, static_cast<std::ptrdiff_t>(rng() % live_orders.size()));
        const OrderRef ref = it->first;
        LiveOrder& order = it->second;

        if (action < 70) {
            const Shares exec_shares = std::min(order.remaining_shares, shares_dist(rng));
            std::vector<std::byte> msg;
            append_header(msg, 'E', locate, tracking++, next_ts());
            append_u64(msg, ref);
            append_u32(msg, exec_shares);
            append_u64(msg, next_match_number++);
            write_framed(out, msg);

            order.remaining_shares -= exec_shares;
            if (order.remaining_shares == 0) {
                live_orders.erase(it);
            }
        } else if (action < 80) {
            const Shares cancel_shares = std::min(order.remaining_shares, shares_dist(rng));
            std::vector<std::byte> msg;
            append_header(msg, 'X', locate, tracking++, next_ts());
            append_u64(msg, ref);
            append_u32(msg, cancel_shares);
            write_framed(out, msg);

            order.remaining_shares -= cancel_shares;
            if (order.remaining_shares == 0) {
                live_orders.erase(it);
            }
        } else if (action < 90) {
            std::vector<std::byte> msg;
            append_header(msg, 'D', locate, tracking++, next_ts());
            append_u64(msg, ref);
            write_framed(out, msg);

            live_orders.erase(it);
        } else if (action < 97) {
            const Shares new_shares = shares_dist(rng);
            const auto new_price =
                static_cast<Price4>(static_cast<std::int64_t>(order.price) + jitter_dist(rng));
            const OrderRef new_ref = next_order_ref++;

            std::vector<std::byte> msg;
            append_header(msg, 'U', locate, tracking++, next_ts());
            append_u64(msg, ref);
            append_u64(msg, new_ref);
            append_u32(msg, new_shares);
            append_u32(msg, new_price);
            write_framed(out, msg);

            LiveOrder replaced = order;
            replaced.remaining_shares = new_shares;
            replaced.price = new_price;
            live_orders.erase(it);
            live_orders[new_ref] = replaced;
        } else {
            // Trade: an independent, non-displayed execution report -- deliberately not tied
            // to book state, gets its own fresh, never-reused order reference number (see the
            // header comment in synth.hpp).
            const std::string_view symbol =
                config.symbols.empty() ? "TEST" : std::string_view(config.symbols[symbol_index]);
            std::vector<std::byte> msg;
            append_header(msg, 'P', locate, tracking++, next_ts());
            append_u64(msg, next_order_ref++);
            append_char(msg, side_dist(rng) == 0 ? 'B' : 'S');
            append_u32(msg, shares_dist(rng));
            append_str_padded(msg, symbol, 8);
            append_u32(msg, order.price);
            append_u64(msg, next_match_number++);
            write_framed(out, msg);
        }
    }

    // 4. System Event: end of messages.
    {
        std::vector<std::byte> msg;
        append_header(msg, 'S', 0, tracking++, next_ts());
        append_char(msg, 'C');
        write_framed(out, msg);
    }
}

} // namespace liquibook::itch
