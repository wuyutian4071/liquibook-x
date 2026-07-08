#include <gtest/gtest.h>

#include <array>

#include "byteorder.hpp"

using namespace liquibook::itch;

namespace {

std::array<std::byte, 8> bytes(std::initializer_list<unsigned char> vals) {
    std::array<std::byte, 8> out{};
    std::size_t i = 0;
    for (auto v : vals) {
        out[i++] = static_cast<std::byte>(v);
    }
    return out;
}

} // namespace

TEST(ByteOrder, ReadU16Be) {
    auto b = bytes({0x01, 0x02});
    EXPECT_EQ(read_u16_be(b.data()), 0x0102);
}

TEST(ByteOrder, ReadU32Be) {
    auto b = bytes({0x01, 0x02, 0x03, 0x04});
    EXPECT_EQ(read_u32_be(b.data()), 0x01020304u);
}

TEST(ByteOrder, ReadU48Be) {
    auto b = bytes({0x01, 0x02, 0x03, 0x04, 0x05, 0x06});
    EXPECT_EQ(read_u48_be(b.data()), 0x010203040506ULL);
}

TEST(ByteOrder, ReadU64Be) {
    auto b = bytes({0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08});
    EXPECT_EQ(read_u64_be(b.data()), 0x0102030405060708ULL);
}

TEST(ByteOrder, WriteU16BeRoundTrips) {
    std::array<std::byte, 2> b{};
    write_u16_be(b.data(), 0xABCD);
    EXPECT_EQ(read_u16_be(b.data()), 0xABCDu);
}

TEST(ByteOrder, WriteU32BeRoundTrips) {
    std::array<std::byte, 4> b{};
    write_u32_be(b.data(), 0xDEADBEEFu);
    EXPECT_EQ(read_u32_be(b.data()), 0xDEADBEEFu);
}

TEST(ByteOrder, WriteU48BeRoundTrips) {
    std::array<std::byte, 6> b{};
    write_u48_be(b.data(), 0x0102030405ABULL);
    EXPECT_EQ(read_u48_be(b.data()), 0x0102030405ABULL);
}

TEST(ByteOrder, WriteU64BeRoundTrips) {
    std::array<std::byte, 8> b{};
    write_u64_be(b.data(), 0x0102030405060708ULL);
    EXPECT_EQ(read_u64_be(b.data()), 0x0102030405060708ULL);
}
