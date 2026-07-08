#include <gtest/gtest.h>

#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "decode.hpp"
#include "synth.hpp"

using namespace liquibook::itch;

namespace {

DecodedMessage require_decode(std::span<const std::byte> raw) {
    auto msg = decode(raw);
    EXPECT_TRUE(msg.has_value()) << "generated stream contained an undecodable message";
    return *msg;
}

} // namespace

// The differential/property-style test the M2 plan calls for: generate a stream, replay it
// through the real decode() (not the generator's own internal bookkeeping), and independently
// verify self-consistency -- every Order Executed/Cancel/Delete/Replace references an order
// that a prior Add Order in the SAME decoded stream actually created and that hadn't yet been
// fully removed at that point.
TEST(SynthRoundtrip, GeneratedStreamIsSelfConsistent) {
    SynthConfig config;
    config.seed = 7;
    config.num_orders = 2000;
    config.symbols = {"AAPL", "MSFT", "GOOGL"};

    std::ostringstream oss(std::ios::binary);
    generate(config, oss);
    const std::string data = oss.str();

    std::vector<std::byte> bytes(data.size());
    for (std::size_t i = 0; i < data.size(); ++i) {
        bytes[i] = static_cast<std::byte>(data[i]);
    }

    std::unordered_set<OrderRef> live_orders;
    std::size_t offset = 0;
    std::size_t add_order_count = 0;
    std::size_t executed_count = 0;
    std::size_t cancel_count = 0;
    std::size_t delete_count = 0;
    std::size_t replace_count = 0;
    std::size_t trade_count = 0;
    bool saw_start_event = false;
    bool saw_end_event = false;

    while (offset + 2 <= bytes.size()) {
        const std::uint16_t len =
            static_cast<std::uint16_t>((std::to_integer<unsigned>(bytes[offset]) << 8) |
                                       std::to_integer<unsigned>(bytes[offset + 1]));
        const std::size_t msg_start = offset + 2;
        ASSERT_LE(msg_start + len, bytes.size()) << "truncated record in generator output";

        std::span<const std::byte> raw(bytes.data() + msg_start, len);
        DecodedMessage msg = require_decode(raw);

        switch (msg.header.type) {
        case MessageType::SystemEvent: {
            const char code = msg.as_system_event().event_code;
            if (code == 'O') {
                EXPECT_FALSE(saw_start_event) << "duplicate start-of-messages event";
                saw_start_event = true;
            } else if (code == 'C') {
                EXPECT_FALSE(saw_end_event) << "duplicate end-of-messages event";
                saw_end_event = true;
            }
            break;
        }
        case MessageType::StockDirectory:
            break;
        case MessageType::AddOrder:
        case MessageType::AddOrderMPID: {
            const auto& ao = msg.as_add_order();
            EXPECT_TRUE(live_orders.insert(ao.order_reference_number).second)
                << "Add Order reused an order reference number still live: "
                << ao.order_reference_number;
            ++add_order_count;
            break;
        }
        case MessageType::OrderExecuted: {
            const auto& oe = msg.as_order_executed();
            EXPECT_TRUE(live_orders.count(oe.order_reference_number) > 0)
                << "Order Executed referenced a non-live order: " << oe.order_reference_number;
            ++executed_count;
            break;
        }
        case MessageType::OrderCancel: {
            const auto& oc = msg.as_order_cancel();
            EXPECT_TRUE(live_orders.count(oc.order_reference_number) > 0)
                << "Order Cancel referenced a non-live order: " << oc.order_reference_number;
            ++cancel_count;
            break;
        }
        case MessageType::OrderDelete: {
            const auto& od = msg.as_order_delete();
            EXPECT_TRUE(live_orders.erase(od.order_reference_number) > 0)
                << "Order Delete referenced a non-live order: " << od.order_reference_number;
            ++delete_count;
            break;
        }
        case MessageType::OrderReplace: {
            const auto& orep = msg.as_order_replace();
            EXPECT_TRUE(live_orders.erase(orep.original_order_reference_number) > 0)
                << "Order Replace referenced a non-live original order: "
                << orep.original_order_reference_number;
            EXPECT_TRUE(live_orders.insert(orep.new_order_reference_number).second)
                << "Order Replace's new order reference number was already live: "
                << orep.new_order_reference_number;
            ++replace_count;
            break;
        }
        case MessageType::Trade:
            // Deliberately independent of book state -- see synth.hpp's doc comment.
            ++trade_count;
            break;
        default:
            FAIL() << "unexpected message type in generated stream";
        }

        offset = msg_start + len;
    }

    EXPECT_EQ(offset, bytes.size()) << "generated stream was not consumed exactly";
    EXPECT_TRUE(saw_start_event);
    EXPECT_TRUE(saw_end_event);
    EXPECT_EQ(add_order_count, config.num_orders);
    // With num_orders=2000 and the generator's action-weighting, every other message type
    // should have fired at least once -- a stream that never exercises Executed/Cancel/
    // Delete/Replace/Trade would silently defeat this test's own purpose.
    EXPECT_GT(executed_count, 0u);
    EXPECT_GT(cancel_count, 0u);
    EXPECT_GT(delete_count, 0u);
    EXPECT_GT(replace_count, 0u);
    EXPECT_GT(trade_count, 0u);
}

TEST(SynthRoundtrip, DeterministicForAGivenSeed) {
    SynthConfig config;
    config.seed = 123;
    config.num_orders = 200;

    std::ostringstream first(std::ios::binary);
    generate(config, first);

    std::ostringstream second(std::ios::binary);
    generate(config, second);

    EXPECT_EQ(first.str(), second.str());
}

TEST(SynthRoundtrip, DifferentSeedsProduceDifferentStreams) {
    SynthConfig config_a;
    config_a.seed = 1;
    config_a.num_orders = 200;

    SynthConfig config_b = config_a;
    config_b.seed = 2;

    std::ostringstream a(std::ios::binary);
    generate(config_a, a);
    std::ostringstream b(std::ios::binary);
    generate(config_b, b);

    EXPECT_NE(a.str(), b.str());
}
