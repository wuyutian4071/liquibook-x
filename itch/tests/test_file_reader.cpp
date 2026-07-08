#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

#include <unistd.h>

#include "file_reader.hpp"

using namespace liquibook::itch;

namespace {

// Guarantees the temp file is removed even if an assertion fails mid-test.
class TempItchFile {
public:
    explicit TempItchFile(const std::vector<std::byte>& bytes) {
        path_ = std::filesystem::temp_directory_path() /
                ("liquibook_test_file_reader_" + std::to_string(::getpid()) + ".itch");
        std::ofstream out(path_, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }

    ~TempItchFile() { std::filesystem::remove(path_); }

    TempItchFile(const TempItchFile&) = delete;
    TempItchFile& operator=(const TempItchFile&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void append_length_prefixed(std::vector<std::byte>& buf, std::initializer_list<unsigned char> msg) {
    const auto len = static_cast<std::uint16_t>(msg.size());
    buf.push_back(static_cast<std::byte>((len >> 8) & 0xFF));
    buf.push_back(static_cast<std::byte>(len & 0xFF));
    for (auto b : msg) {
        buf.push_back(static_cast<std::byte>(b));
    }
}

} // namespace

TEST(FileReader, ReadsExactMessageBoundaries) {
    std::vector<std::byte> file_bytes;
    // A 3-byte fake message, then a 5-byte fake message. Content doesn't need to be a real
    // ITCH message here -- this test is purely about the length-prefix framing/boundaries.
    append_length_prefixed(file_bytes, {0xAA, 0xBB, 0xCC});
    append_length_prefixed(file_bytes, {0x01, 0x02, 0x03, 0x04, 0x05});

    TempItchFile file(file_bytes);
    ItchFileReader reader(file.path());

    auto first = reader.next_raw_message();
    ASSERT_TRUE(first.has_value());
    ASSERT_EQ(first->size(), 3u);
    EXPECT_EQ(std::to_integer<unsigned char>((*first)[0]), 0xAA);
    EXPECT_EQ(std::to_integer<unsigned char>((*first)[1]), 0xBB);
    EXPECT_EQ(std::to_integer<unsigned char>((*first)[2]), 0xCC);

    auto second = reader.next_raw_message();
    ASSERT_TRUE(second.has_value());
    ASSERT_EQ(second->size(), 5u);
    EXPECT_EQ(std::to_integer<unsigned char>((*second)[4]), 0x05);

    EXPECT_FALSE(reader.next_raw_message().has_value());
}

TEST(FileReader, EmptyFileReturnsNulloptImmediately) {
    TempItchFile file({});
    ItchFileReader reader(file.path());
    EXPECT_FALSE(reader.next_raw_message().has_value());
}

TEST(FileReader, TruncatedTrailingRecordStopsCleanly) {
    std::vector<std::byte> file_bytes;
    append_length_prefixed(file_bytes, {0x01, 0x02});
    // Declare a length of 10 but only supply 3 trailing bytes -- truncated/corrupt record.
    file_bytes.push_back(static_cast<std::byte>(0x00));
    file_bytes.push_back(static_cast<std::byte>(0x0A));
    file_bytes.push_back(static_cast<std::byte>(0xFF));
    file_bytes.push_back(static_cast<std::byte>(0xFF));
    file_bytes.push_back(static_cast<std::byte>(0xFF));

    TempItchFile file(file_bytes);
    ItchFileReader reader(file.path());

    auto first = reader.next_raw_message();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->size(), 2u);

    EXPECT_FALSE(reader.next_raw_message().has_value());
}

TEST(FileReader, NextMessageDecodesARealItchMessage) {
    // A real 19-byte Order Delete ('D') message: type + locate(2) + tracking(2) + ts(6) +
    // order_ref(8).
    std::vector<std::byte> msg;
    msg.push_back(static_cast<std::byte>('D'));
    msg.push_back(static_cast<std::byte>(0x00));
    msg.push_back(static_cast<std::byte>(0x01));
    msg.push_back(static_cast<std::byte>(0x00));
    msg.push_back(static_cast<std::byte>(0x02));
    for (int i = 0; i < 6; ++i) {
        msg.push_back(static_cast<std::byte>(0x00));
    }
    for (int i = 0; i < 7; ++i) {
        msg.push_back(static_cast<std::byte>(0x00));
    }
    msg.push_back(static_cast<std::byte>(0x2A)); // order_reference_number = 42
    ASSERT_EQ(msg.size(), 19u);

    std::vector<std::byte> file_bytes;
    const auto len = static_cast<std::uint16_t>(msg.size());
    file_bytes.push_back(static_cast<std::byte>((len >> 8) & 0xFF));
    file_bytes.push_back(static_cast<std::byte>(len & 0xFF));
    file_bytes.insert(file_bytes.end(), msg.begin(), msg.end());

    TempItchFile file(file_bytes);
    ItchFileReader reader(file.path());

    auto decoded = reader.next_message();
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->header.type, MessageType::OrderDelete);
    EXPECT_EQ(decoded->as_order_delete().order_reference_number, 42u);
}
