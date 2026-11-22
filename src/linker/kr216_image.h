#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "linker/linker.h"

namespace krc::linker {

// Writes the K216 executable container fixed by ADR 0054. The linked sections
// keep their absolute addresses; the writer adds the two-byte reset-vector
// section at 0x0100, validates the allocated map and the entry, and writes a
// CRC32 over the whole file with the CRC field zeroed.
[[nodiscard]] bool write_kr216_image(const LinkedImage& image,
                                     std::vector<std::uint8_t>& bytes,
                                     std::string& error);

}  // namespace krc::linker
