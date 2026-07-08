#pragma once

#include <atomic>
#include <bit>
#include <cstddef>
#include <memory>
#include <type_traits>

namespace liquibook::containers {

// A different inner namespace than hash_map.hpp's own `detail` (not `detail` again):
// mirrors its round_up_to_power_of_two exactly, duplicated locally rather than shared to
// avoid touching already-shipped, CI-verified M3 code for the sake of a five-line utility --
// but both headers land in the same translation unit whenever a consumer needs both
// containers (as pipeline/itch_pipeline.hpp does, via order_book.hpp -> hash_map.hpp), so an
// *identically named* `liquibook::containers::detail` function in each is a genuine
// redefinition, not just redundant.
namespace ring_buffer_detail {

[[nodiscard]] constexpr std::size_t round_up_to_power_of_two(std::size_t n) noexcept {
    if (n <= 1) {
        return 1;
    }
    return std::size_t {1} << std::bit_width(n - 1);
}

} // namespace ring_buffer_detail

// A fixed-capacity, single-producer/single-consumer circular buffer. SPSC is the simplest
// lock-free case: because exactly one thread ever writes each index, push/pop need only
// plain atomic load/store with acquire/release ordering -- no compare-exchange, no
// fetch_add, no retry loop. That machinery exists to arbitrate between multiple writers to
// the same index (MPSC/MPMC); it doesn't apply here.
//
// write_index_ and read_index_ are each padded onto their own cache line (alignas(64)):
// without that, the producer writing write_index_ and the consumer writing read_index_ would
// false-share one line, forcing coherence traffic between cores on every single push/pop even
// though the two threads never touch each other's index.
//
// Both indices increase monotonically forever (never wrap the logical count, only the
// physical slot via masking) -- the standard technique that avoids the "does read == write
// mean empty or full" ambiguity that plagues naive circular buffers using wrapped indices
// directly.
//
// T is constrained to trivially copyable: every hot-path type in this project already is
// (itch::DecodedMessage, engine::IncomingOrder, the engine:: event structs), and a queue
// whose slots are overwritten in place has no need for placement-new/destroy_at machinery.
template <typename T>
    requires std::is_trivially_copyable_v<T>
class SpscRingBuffer {
public:
    // Rounds `capacity` up to the next power of two (matching
    // containers::OpenAddressingHashMap's own established convention), so the physical slot
    // for a logical index can be computed with a mask instead of a modulo.
    explicit SpscRingBuffer(std::size_t capacity)
        : capacity_(ring_buffer_detail::round_up_to_power_of_two(capacity)), mask_(capacity_ - 1),
          buffer_(std::make_unique<T[]>(capacity_)) {}

    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;
    SpscRingBuffer(SpscRingBuffer&&) = delete;
    SpscRingBuffer& operator=(SpscRingBuffer&&) = delete;

    // Producer-only. Returns false (without blocking) if the buffer is full.
    [[nodiscard]] bool push(const T& value) noexcept {
        const std::size_t write = write_index_.value.load(std::memory_order_relaxed);
        const std::size_t read = read_index_.value.load(std::memory_order_acquire);
        if (write - read >= capacity_) {
            return false;
        }
        buffer_[write & mask_] = value;
        write_index_.value.store(write + 1, std::memory_order_release);
        return true;
    }

    // Consumer-only. Returns false (without blocking) if the buffer is empty.
    [[nodiscard]] bool pop(T& out) noexcept {
        const std::size_t read = read_index_.value.load(std::memory_order_relaxed);
        const std::size_t write = write_index_.value.load(std::memory_order_acquire);
        if (read == write) {
            return false;
        }
        out = buffer_[read & mask_];
        read_index_.value.store(read + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

private:
    struct alignas(64) PaddedIndex {
        std::atomic<std::size_t> value {0};
    };

    std::size_t capacity_;
    std::size_t mask_;
    std::unique_ptr<T[]> buffer_;
    PaddedIndex write_index_; // written only by the producer thread
    PaddedIndex read_index_;  // written only by the consumer thread
};

} // namespace liquibook::containers
