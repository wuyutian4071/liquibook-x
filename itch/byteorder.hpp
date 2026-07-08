#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace liquibook::itch {

// ITCH 5.0 fields are big-endian (network byte order). These helpers are branchless: pure
// shift-and-or, no conditionals, safe to call on any byte alignment.

[[nodiscard]] inline std::uint16_t read_u16_be(const std::byte* p) noexcept {
    return static_cast<std::uint16_t>((std::to_integer<std::uint16_t>(p[0]) << 8) |
                                      std::to_integer<std::uint16_t>(p[1]));
}

[[nodiscard]] inline std::uint32_t read_u32_be(const std::byte* p) noexcept {
    return (std::to_integer<std::uint32_t>(p[0]) << 24) |
           (std::to_integer<std::uint32_t>(p[1]) << 16) |
           (std::to_integer<std::uint32_t>(p[2]) << 8) | std::to_integer<std::uint32_t>(p[3]);
}

// ITCH timestamps are 6 bytes (48 bits), nanoseconds since midnight. There is no native
// 48-bit integer type, so this widens into a uint64_t with the top 16 bits always zero.
[[nodiscard]] inline std::uint64_t read_u48_be(const std::byte* p) noexcept {
    return (std::to_integer<std::uint64_t>(p[0]) << 40) |
           (std::to_integer<std::uint64_t>(p[1]) << 32) |
           (std::to_integer<std::uint64_t>(p[2]) << 24) |
           (std::to_integer<std::uint64_t>(p[3]) << 16) |
           (std::to_integer<std::uint64_t>(p[4]) << 8) | std::to_integer<std::uint64_t>(p[5]);
}

[[nodiscard]] inline std::uint64_t read_u64_be(const std::byte* p) noexcept {
    return (std::to_integer<std::uint64_t>(p[0]) << 56) |
           (std::to_integer<std::uint64_t>(p[1]) << 48) |
           (std::to_integer<std::uint64_t>(p[2]) << 40) |
           (std::to_integer<std::uint64_t>(p[3]) << 32) |
           (std::to_integer<std::uint64_t>(p[4]) << 24) |
           (std::to_integer<std::uint64_t>(p[5]) << 16) |
           (std::to_integer<std::uint64_t>(p[6]) << 8) | std::to_integer<std::uint64_t>(p[7]);
}

inline void write_u16_be(std::byte* p, std::uint16_t v) noexcept {
    p[0] = static_cast<std::byte>((v >> 8) & 0xFF);
    p[1] = static_cast<std::byte>(v & 0xFF);
}

inline void write_u32_be(std::byte* p, std::uint32_t v) noexcept {
    p[0] = static_cast<std::byte>((v >> 24) & 0xFF);
    p[1] = static_cast<std::byte>((v >> 16) & 0xFF);
    p[2] = static_cast<std::byte>((v >> 8) & 0xFF);
    p[3] = static_cast<std::byte>(v & 0xFF);
}

// Writes the low 48 bits of `v` as 6 big-endian bytes; the top 16 bits of `v` are ignored.
inline void write_u48_be(std::byte* p, std::uint64_t v) noexcept {
    p[0] = static_cast<std::byte>((v >> 40) & 0xFF);
    p[1] = static_cast<std::byte>((v >> 32) & 0xFF);
    p[2] = static_cast<std::byte>((v >> 24) & 0xFF);
    p[3] = static_cast<std::byte>((v >> 16) & 0xFF);
    p[4] = static_cast<std::byte>((v >> 8) & 0xFF);
    p[5] = static_cast<std::byte>(v & 0xFF);
}

inline void write_u64_be(std::byte* p, std::uint64_t v) noexcept {
    p[0] = static_cast<std::byte>((v >> 56) & 0xFF);
    p[1] = static_cast<std::byte>((v >> 48) & 0xFF);
    p[2] = static_cast<std::byte>((v >> 40) & 0xFF);
    p[3] = static_cast<std::byte>((v >> 32) & 0xFF);
    p[4] = static_cast<std::byte>((v >> 24) & 0xFF);
    p[5] = static_cast<std::byte>((v >> 16) & 0xFF);
    p[6] = static_cast<std::byte>((v >> 8) & 0xFF);
    p[7] = static_cast<std::byte>(v & 0xFF);
}

} // namespace liquibook::itch
