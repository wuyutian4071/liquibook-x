#pragma once

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

namespace liquibook::containers {

// A fixed-capacity object pool: one contiguous buffer allocated *once* at construction,
// never grows. Free slots are tracked via an intrusive free list stored in the unused slot
// memory itself (a union of T and the free-list index) -- no separate bookkeeping array, so
// acquire()/release() touch only the one slot involved, not a second cold structure.
//
// A union's own address always equals each of its members' addresses, so a slot recovered
// from a `T*` via reinterpret_cast<Slot*> and pointer-subtracted against the base gives back
// its index in O(1) -- this is what release() uses to relink the slot onto the free list
// without the caller having to remember an index alongside the pointer.
//
// Requires T to be trivially destructible: this pool is for hot-path POD-like types (mirrors
// the ITCH message payloads in itch/messages.hpp), so there's never destruction work to run,
// which sidesteps the question of what pool teardown should do about still-live objects
// rather than half-solving it.
template <typename T>
class ObjectPool {
    static_assert(std::is_trivially_destructible_v<T>,
                  "ObjectPool holds hot-path POD-like types with no destruction work; it does "
                  "not run destructors on release() or on pool teardown.");

public:
    explicit ObjectPool(std::size_t capacity)
        : slots_(capacity > 0 ? std::make_unique<Slot[]>(capacity) : nullptr), capacity_(capacity),
          free_head_(capacity > 0 ? 0 : kNone) {
        for (std::size_t i = 0; i + 1 < capacity; ++i) {
            slots_[i].next_free = i + 1;
        }
        if (capacity > 0) {
            slots_[capacity - 1].next_free = kNone;
        }
    }

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    ObjectPool(ObjectPool&&) noexcept = default;
    ObjectPool& operator=(ObjectPool&&) noexcept = default;

    // Constructs a T in the head free slot and returns a pointer to it, or nullptr if the
    // pool is exhausted (never grows -- growth would reallocate and invalidate every
    // previously-acquired pointer, which would be a much worse hot-path surprise).
    template <typename... Args>
    [[nodiscard]] T* acquire(Args&&... args) {
        if (free_head_ == kNone) {
            return nullptr;
        }
        const std::size_t index = free_head_;
        Slot& slot = slots_[index];
        free_head_ = slot.next_free;
        T* obj =
            ::new (static_cast<void*>(std::addressof(slot.value))) T(std::forward<Args>(args)...);
        ++size_;
        return obj;
    }

    // `ptr` must have been returned by a prior acquire() on this same pool and not already
    // released.
    void release(T* ptr) noexcept {
        std::destroy_at(ptr);
        auto* slot = reinterpret_cast<Slot*>(ptr);
        const auto index = static_cast<std::size_t>(slot - slots_.get());
        slot->next_free = free_head_;
        free_head_ = index;
        --size_;
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] bool full() const noexcept { return size_ == capacity_; }

private:
    static constexpr std::size_t kNone = static_cast<std::size_t>(-1);

    union Slot {
        Slot() noexcept : next_free(0) {}
        T value;
        std::size_t next_free;
    };

    std::unique_ptr<Slot[]> slots_;
    std::size_t capacity_;
    std::size_t free_head_;
    std::size_t size_ = 0;
};

} // namespace liquibook::containers
