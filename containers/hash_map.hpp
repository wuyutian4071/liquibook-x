#pragma once

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace liquibook::containers {

namespace detail {

// splitmix64's finalizer: a well-known, fast 64-bit avalanche mix. Used instead of
// std::hash<K> because std::hash's quality for sequential integer keys (like ITCH order
// reference numbers, which increment roughly monotonically) isn't guaranteed -- a poor mix
// would cluster badly against a power-of-two table size.
[[nodiscard]] constexpr std::uint64_t mix64(std::uint64_t x) noexcept {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

[[nodiscard]] constexpr std::size_t round_up_to_power_of_two(std::size_t n) noexcept {
    if (n <= 1) {
        return 1;
    }
    return std::size_t {1} << std::bit_width(n - 1);
}

} // namespace detail

// A fixed-capacity open-addressing hash map: one contiguous buffer allocated once at
// construction (capacity rounded up to a power of two), never grows or rehashes -- size for
// a target load factor (recommend <= ~70%) when constructing. Linear probing: sequential
// memory access on collision, better cache behavior than double hashing's scattered probes.
// insert/find/erase are all O(1) average, zero heap allocation.
//
// Deletion uses the standard 3-state slot marker (Empty/Occupied/Tombstone): linear-probed
// open addressing can't simply clear a slot on erase without breaking the probe sequence for
// other keys that hashed to the same slot and probed past it. insert() remembers the first
// tombstone seen during its probe and reuses it if the key isn't found before an Empty slot,
// keeping tombstone accumulation bounded rather than letting them degrade every future probe.
template <typename K, typename V>
    requires std::equality_comparable<K> && std::default_initializable<K> &&
             std::convertible_to<K, std::uint64_t> && std::default_initializable<V> &&
             std::movable<V>
class OpenAddressingHashMap {
public:
    explicit OpenAddressingHashMap(std::size_t capacity_hint)
        : capacity_(detail::round_up_to_power_of_two(capacity_hint)), mask_(capacity_ - 1),
          slots_(std::make_unique<Slot[]>(capacity_)) {}

    OpenAddressingHashMap(const OpenAddressingHashMap&) = delete;
    OpenAddressingHashMap& operator=(const OpenAddressingHashMap&) = delete;
    OpenAddressingHashMap(OpenAddressingHashMap&&) noexcept = default;
    OpenAddressingHashMap& operator=(OpenAddressingHashMap&&) noexcept = default;

    // False if `key` is already present, or the map is completely full with no matching key
    // and no reusable tombstone (never grows/rehashes -- size for headroom up front).
    bool insert(K key, V value) noexcept {
        std::size_t index = probe_start(key);
        std::size_t first_tombstone = kNone;

        for (std::size_t probes = 0; probes < capacity_; ++probes) {
            Slot& slot = slots_[index];
            if (slot.state == SlotState::Empty) {
                place(first_tombstone != kNone ? first_tombstone : index,
                      key,
                      std::move(value),
                      first_tombstone != kNone);
                return true;
            }
            if (slot.state == SlotState::Tombstone) {
                if (first_tombstone == kNone) {
                    first_tombstone = index;
                }
            } else if (slot.key == key) {
                return false; // already present
            }
            index = (index + 1) & mask_;
        }

        if (first_tombstone != kNone) {
            place(first_tombstone, key, std::move(value), /*was_tombstone=*/true);
            return true;
        }
        return false; // genuinely full: every slot Occupied, no match, no tombstone
    }

    [[nodiscard]] V* find(K key) noexcept {
        std::size_t index = probe_start(key);
        for (std::size_t probes = 0; probes < capacity_; ++probes) {
            Slot& slot = slots_[index];
            if (slot.state == SlotState::Empty) {
                return nullptr; // an Empty slot proves the key isn't present past this point
            }
            if (slot.state == SlotState::Occupied && slot.key == key) {
                return &slot.value;
            }
            index = (index + 1) & mask_;
        }
        return nullptr;
    }

    [[nodiscard]] const V* find(K key) const noexcept {
        return const_cast<OpenAddressingHashMap*>(this)->find(key);
    }

    bool erase(K key) noexcept {
        std::size_t index = probe_start(key);
        for (std::size_t probes = 0; probes < capacity_; ++probes) {
            Slot& slot = slots_[index];
            if (slot.state == SlotState::Empty) {
                return false;
            }
            if (slot.state == SlotState::Occupied && slot.key == key) {
                slot.state = SlotState::Tombstone;
                slot.key = K {};
                slot.value = V {};
                --size_;
                ++tombstones_;
                return true;
            }
            index = (index + 1) & mask_;
        }
        return false;
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t tombstone_count() const noexcept { return tombstones_; }
    [[nodiscard]] double load_factor() const noexcept {
        return static_cast<double>(size_ + tombstones_) / static_cast<double>(capacity_);
    }

private:
    enum class SlotState : std::uint8_t { Empty, Occupied, Tombstone };

    struct Slot {
        SlotState state = SlotState::Empty;
        K key {};
        V value {};
    };

    static constexpr std::size_t kNone = static_cast<std::size_t>(-1);

    [[nodiscard]] std::size_t probe_start(const K& key) const noexcept {
        return detail::mix64(static_cast<std::uint64_t>(key)) & mask_;
    }

    void place(std::size_t index, K key, V value, bool was_tombstone) noexcept {
        Slot& slot = slots_[index];
        slot.key = std::move(key);
        slot.value = std::move(value);
        slot.state = SlotState::Occupied;
        ++size_;
        if (was_tombstone) {
            --tombstones_;
        }
    }

    std::size_t capacity_;
    std::size_t mask_;
    std::unique_ptr<Slot[]> slots_;
    std::size_t size_ = 0;
    std::size_t tombstones_ = 0;
};

} // namespace liquibook::containers
