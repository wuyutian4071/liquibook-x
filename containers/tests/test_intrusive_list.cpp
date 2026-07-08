#include <gtest/gtest.h>

#include <vector>

#include "intrusive_list.hpp"

using liquibook::containers::IntrusiveList;

namespace {

struct Node {
    Node* prev = nullptr;
    Node* next = nullptr;
    int value = 0;
};

std::vector<int> collect(const IntrusiveList<Node>& list) {
    std::vector<int> values;
    for (const Node& n : list) {
        values.push_back(n.value);
    }
    return values;
}

} // namespace

TEST(IntrusiveList, StartsEmpty) {
    IntrusiveList<Node> list;
    EXPECT_TRUE(list.empty());
    EXPECT_EQ(list.size(), 0u);
    EXPECT_EQ(list.front(), nullptr);
    EXPECT_EQ(list.back(), nullptr);
}

TEST(IntrusiveList, PushBackMaintainsOrder) {
    Node a {.value = 1};
    Node b {.value = 2};
    Node c {.value = 3};
    IntrusiveList<Node> list;

    list.push_back(&a);
    list.push_back(&b);
    list.push_back(&c);

    EXPECT_EQ(list.size(), 3u);
    EXPECT_EQ(list.front(), &a);
    EXPECT_EQ(list.back(), &c);
    EXPECT_EQ(collect(list), (std::vector<int> {1, 2, 3}));
}

TEST(IntrusiveList, PushFrontMaintainsOrder) {
    Node a {.value = 1};
    Node b {.value = 2};
    Node c {.value = 3};
    IntrusiveList<Node> list;

    list.push_front(&a);
    list.push_front(&b);
    list.push_front(&c);

    EXPECT_EQ(list.size(), 3u);
    EXPECT_EQ(list.front(), &c);
    EXPECT_EQ(list.back(), &a);
    EXPECT_EQ(collect(list), (std::vector<int> {3, 2, 1}));
}

TEST(IntrusiveList, SingleElementPushAndRemove) {
    Node a {.value = 42};
    IntrusiveList<Node> list;

    list.push_back(&a);
    EXPECT_EQ(list.front(), &a);
    EXPECT_EQ(list.back(), &a);
    EXPECT_EQ(list.size(), 1u);

    list.remove(&a);
    EXPECT_TRUE(list.empty());
    EXPECT_EQ(list.front(), nullptr);
    EXPECT_EQ(list.back(), nullptr);
    EXPECT_EQ(a.prev, nullptr);
    EXPECT_EQ(a.next, nullptr);
}

TEST(IntrusiveList, RemoveHeadRelinksCorrectly) {
    Node a {.value = 1};
    Node b {.value = 2};
    Node c {.value = 3};
    IntrusiveList<Node> list;
    list.push_back(&a);
    list.push_back(&b);
    list.push_back(&c);

    list.remove(&a);
    EXPECT_EQ(list.size(), 2u);
    EXPECT_EQ(list.front(), &b);
    EXPECT_EQ(list.back(), &c);
    EXPECT_EQ(b.prev, nullptr);
    EXPECT_EQ(collect(list), (std::vector<int> {2, 3}));
}

TEST(IntrusiveList, RemoveTailRelinksCorrectly) {
    Node a {.value = 1};
    Node b {.value = 2};
    Node c {.value = 3};
    IntrusiveList<Node> list;
    list.push_back(&a);
    list.push_back(&b);
    list.push_back(&c);

    list.remove(&c);
    EXPECT_EQ(list.size(), 2u);
    EXPECT_EQ(list.front(), &a);
    EXPECT_EQ(list.back(), &b);
    EXPECT_EQ(b.next, nullptr);
    EXPECT_EQ(collect(list), (std::vector<int> {1, 2}));
}

TEST(IntrusiveList, RemoveMiddleRelinksNeighbors) {
    Node a {.value = 1};
    Node b {.value = 2};
    Node c {.value = 3};
    IntrusiveList<Node> list;
    list.push_back(&a);
    list.push_back(&b);
    list.push_back(&c);

    list.remove(&b);
    EXPECT_EQ(list.size(), 2u);
    EXPECT_EQ(list.front(), &a);
    EXPECT_EQ(list.back(), &c);
    EXPECT_EQ(a.next, &c);
    EXPECT_EQ(c.prev, &a);
    EXPECT_EQ(collect(list), (std::vector<int> {1, 3}));
}

TEST(IntrusiveList, RemovingEveryElementLeavesListEmpty) {
    Node a {.value = 1};
    Node b {.value = 2};
    Node c {.value = 3};
    IntrusiveList<Node> list;
    list.push_back(&a);
    list.push_back(&b);
    list.push_back(&c);

    list.remove(&b);
    list.remove(&a);
    list.remove(&c);

    EXPECT_TRUE(list.empty());
    EXPECT_EQ(list.size(), 0u);
    EXPECT_EQ(list.front(), nullptr);
    EXPECT_EQ(list.back(), nullptr);
}

TEST(IntrusiveList, IteratorVisitsEveryElementExactlyOnceInOrder) {
    std::vector<Node> nodes(5);
    IntrusiveList<Node> list;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        nodes[i].value = static_cast<int>(i);
        list.push_back(&nodes[i]);
    }

    std::vector<int> visited;
    for (const Node& n : list) {
        visited.push_back(n.value);
    }
    EXPECT_EQ(visited, (std::vector<int> {0, 1, 2, 3, 4}));
    EXPECT_EQ(visited.size(), list.size());
}

TEST(IntrusiveList, PushBackAfterEmptyingReusesHeadAndTailCorrectly) {
    Node a {.value = 1};
    Node b {.value = 2};
    IntrusiveList<Node> list;

    list.push_back(&a);
    list.remove(&a);
    EXPECT_TRUE(list.empty());

    list.push_back(&b);
    EXPECT_EQ(list.front(), &b);
    EXPECT_EQ(list.back(), &b);
    EXPECT_EQ(list.size(), 1u);
}
