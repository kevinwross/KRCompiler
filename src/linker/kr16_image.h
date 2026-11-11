#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace krc::linker {

// Writes a minimal KR16 EXEC container (.kex) containing one allocated text
// section and the shared string table. Symbols and debug sections are omitted
// until the KR16 linker can produce them.
[[nodiscard]] bool write_kr16_image(const std::vector<std::uint8_t>& text,
                                    std::uint16_t entry,
                                    std::vector<std::uint8_t>& bytes,
                                    std::string& error);

// Writes a KR16 EXEC with the text loaded at load_address and execution
// starting at entry.
[[nodiscard]] bool write_kr16_image(const std::vector<std::uint8_t>& text,
                                    std::uint16_t load_address,
                                    std::uint16_t entry,
                                    std::vector<std::uint8_t>& bytes,
                                    std::string& error);

}  // namespace krc::linker
