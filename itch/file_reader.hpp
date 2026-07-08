#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>

#include "decode.hpp"

namespace liquibook::itch {

// Memory-maps an ITCH 5.0 sample/historical file and walks its length-prefixed framing
// ([2-byte big-endian length][message bytes], repeated to EOF) with zero copies. The
// constructor is the only part of this class that may throw (std::system_error, on
// open/fstat/mmap failure) -- it runs once at startup, not on the hot path. Non-copyable
// (owns the mapping), movable.
class ItchFileReader {
public:
    explicit ItchFileReader(const std::filesystem::path& path);
    ~ItchFileReader();

    ItchFileReader(const ItchFileReader&) = delete;
    ItchFileReader& operator=(const ItchFileReader&) = delete;
    ItchFileReader(ItchFileReader&& other) noexcept;
    ItchFileReader& operator=(ItchFileReader&& other) noexcept;

    // Returns the next raw message's bytes (the type byte plus every field, NOT including
    // the 2-byte length prefix that framed it), or std::nullopt once the file is exhausted
    // or a truncated trailing record is hit. The returned span points directly into the
    // memory-mapped file -- valid only as long as this reader is alive and not moved-from.
    [[nodiscard]] std::optional<std::span<const std::byte>> next_raw_message() noexcept;

    // Convenience: next_raw_message() followed by decode().
    [[nodiscard]] std::optional<DecodedMessage> next_message() noexcept;

private:
    void reset() noexcept;

    const std::byte* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t offset_ = 0;
};

} // namespace liquibook::itch
