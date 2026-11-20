#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "linker/linker.h"

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

// Writes a KR16 EXEC carrying `extra_sections` beside the text. A debug section
// (`.kdbg`) is written unallocated with the container's debug flag set; an
// allocated section keeps its address. The caller owns the section addresses,
// so a payload's absolute addresses stay correct only while the text keeps the
// load address recorded in the container.
[[nodiscard]] bool write_kr16_image(const std::vector<std::uint8_t>& text,
                                    std::uint16_t load_address,
                                    std::uint16_t entry,
                                    std::span<const LinkedSection> extra_sections,
                                    std::vector<std::uint8_t>& bytes,
                                    std::string& error);

}  // namespace krc::linker
