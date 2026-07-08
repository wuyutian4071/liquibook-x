#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <thread>

#include "decode.hpp"
#include "messages.hpp"
#include "order_book.hpp"
#include "spsc_ring_buffer.hpp"

namespace liquibook::pipeline {

// Runs a two-thread producer/consumer pipeline over a length-prefixed ITCH byte stream (M2's
// own [2-byte length][message] framing): a producer thread walks the framing and decodes each
// message with M2's real decode(), pushing it into a containers::SpscRingBuffer<DecodedMessage>
// (M6); a consumer thread pops and applies each one to `book` via M4's real
// OrderBook::apply() -- decoupling parsing from book-building onto separate threads, the
// realistic architecture a real feed handler uses so a slow matching/book-building thread
// never blocks the parser, and vice versa.
//
// Blocks until every message has been produced and consumed. Returns the number processed.
[[nodiscard]] inline std::size_t run_itch_pipeline(std::span<const std::byte> raw,
                                                   book::OrderBook& book,
                                                   std::size_t ring_buffer_capacity) {
    containers::SpscRingBuffer<itch::DecodedMessage> ring(ring_buffer_capacity);
    std::atomic<bool> producer_done {false};
    std::size_t messages_processed = 0;

    std::thread producer([&] {
        std::size_t offset = 0;
        while (offset + 2 <= raw.size()) {
            const auto len =
                static_cast<std::uint16_t>((std::to_integer<unsigned>(raw[offset]) << 8) |
                                           std::to_integer<unsigned>(raw[offset + 1]));
            const std::size_t msg_start = offset + 2;
            if (msg_start + len > raw.size()) {
                break; // truncated trailing record -- stop rather than read past the end
            }

            const auto decoded = itch::decode(raw.subspan(msg_start, len));
            if (decoded.has_value()) {
                while (!ring.push(*decoded)) {
                    std::this_thread::yield(); // ring full -- back off, don't hard-spin
                }
            }

            offset = msg_start + len;
        }
        producer_done.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        itch::DecodedMessage msg;
        for (;;) {
            if (ring.pop(msg)) {
                book.apply(msg);
                ++messages_processed;
                continue;
            }
            // Pop failed. If the producer was already finished *before* this failed pop, the
            // ring is permanently empty -- nothing more will ever be pushed. But if the
            // producer finishes concurrently with (or just after) this failed pop, its last
            // item might still be sitting unconsumed: re-check producer_done here, and if
            // now true, attempt exactly one more pop before concluding the stream is done.
            // producer_done's acquire-load synchronizes-with the producer's release-store,
            // which happens strictly after its very last push() call completes -- so a
            // second pop() performed after observing producer_done == true is guaranteed to
            // see that last item if it hasn't been consumed yet.
            if (producer_done.load(std::memory_order_acquire)) {
                if (!ring.pop(msg)) {
                    break;
                }
                book.apply(msg);
                ++messages_processed;
                continue;
            }
            std::this_thread::yield();
        }
    });

    producer.join();
    consumer.join();
    return messages_processed;
}

} // namespace liquibook::pipeline
