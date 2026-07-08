#pragma once

#include <cstddef>
#include <optional>
#include <span>

#include "messages.hpp"

namespace liquibook::itch {

// Decodes one raw ITCH message (the type byte plus every field, WITHOUT the 2-byte
// length-prefix used to frame it in a file -- see ItchFileReader for that layer). Returns
// std::nullopt for a type this project doesn't support or a span shorter than that type's
// wire_length(), never undefined behavior and never throws.
[[nodiscard]] std::optional<DecodedMessage> decode(std::span<const std::byte> raw) noexcept;

} // namespace liquibook::itch
