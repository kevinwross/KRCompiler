#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace krc::image {

// Reads one section's payload out of a `.k232` container (ADR 0058) by name. A
// tool that only needs the debug payload — `krc --dump-symbols` — uses this
// rather than a full image loader. False when the magic is not K232, when the
// section table runs past the buffer, or when the section is absent; each error
// names what failed.
[[nodiscard]] bool read_k232_section(std::span<const std::uint8_t> container, std::string_view name,
                                     std::span<const std::uint8_t>& payload, std::string& error);

}  // namespace krc::image
