#pragma once

#include <concepts>
#include <cstddef>

namespace liquibook::containers {

// T must have public `T* prev` and `T* next` members -- enforced by this concept rather than
// left as an undocumented convention, so misuse fails at the call site with a clear compiler
// error instead of a confusing template instantiation error deep in the list's internals.
template <typename T>
concept IntrusiveListNode = requires(T* node) {
    { node->prev } -> std::same_as<T*&>;
    { node->next } -> std::same_as<T*&>;
};

// A doubly-linked list that allocates no nodes of its own -- T itself is the node, linked via
// its own prev/next members. push_back/push_front/remove are all O(1) given the node's own
// pointers; this is what a per-price-level FIFO order queue is built from directly (M4).
template <IntrusiveListNode T>
class IntrusiveList {
public:
    void push_back(T* node) noexcept {
        node->prev = tail_;
        node->next = nullptr;
        if (tail_ != nullptr) {
            tail_->next = node;
        } else {
            head_ = node;
        }
        tail_ = node;
        ++size_;
    }

    void push_front(T* node) noexcept {
        node->next = head_;
        node->prev = nullptr;
        if (head_ != nullptr) {
            head_->prev = node;
        } else {
            tail_ = node;
        }
        head_ = node;
        ++size_;
    }

    // `node` must currently be a member of this list.
    void remove(T* node) noexcept {
        if (node->prev != nullptr) {
            node->prev->next = node->next;
        } else {
            head_ = node->next;
        }
        if (node->next != nullptr) {
            node->next->prev = node->prev;
        } else {
            tail_ = node->prev;
        }
        node->prev = nullptr;
        node->next = nullptr;
        --size_;
    }

    [[nodiscard]] T* front() const noexcept { return head_; }
    [[nodiscard]] T* back() const noexcept { return tail_; }
    [[nodiscard]] bool empty() const noexcept { return head_ == nullptr; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    class iterator {
    public:
        explicit iterator(T* node) noexcept : node_(node) {}
        T& operator*() const noexcept { return *node_; }
        T* operator->() const noexcept { return node_; }
        iterator& operator++() noexcept {
            node_ = node_->next;
            return *this;
        }
        [[nodiscard]] bool operator==(const iterator& other) const noexcept {
            return node_ == other.node_;
        }
        [[nodiscard]] bool operator!=(const iterator& other) const noexcept {
            return node_ != other.node_;
        }

    private:
        T* node_;
    };

    [[nodiscard]] iterator begin() const noexcept { return iterator(head_); }
    [[nodiscard]] iterator end() const noexcept { return iterator(nullptr); }

private:
    T* head_ = nullptr;
    T* tail_ = nullptr;
    std::size_t size_ = 0;
};

} // namespace liquibook::containers
