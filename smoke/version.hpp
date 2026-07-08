#pragma once

#include <string_view>

namespace liquibook {

// Proves the whole toolchain (compile, link, GoogleTest via FetchContent, ctest discovery,
// sanitizer builds, the CI matrix) actually works end to end before any real module exists.
// Real modules (itch/, book/, engine/, transport/) replace this milestone by milestone.
[[nodiscard]] std::string_view version() noexcept;

} // namespace liquibook
