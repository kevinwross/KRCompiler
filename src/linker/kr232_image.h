#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace krc::linker {

// The `.kr232` container of ADR 0058: a 64-byte header, the section payloads, a
// table of 32-byte section records, and a string table.
struct Kr232Section {
    std::string name;
    std::uint16_t type = 1;   // 1 progbits, 8 nobits
    std::uint16_t flags = 0;  // 1 alloc, 2 write, 4 execute, 8 debug
    std::uint32_t address = 0;
    std::uint16_t alignment = 4;
    std::vector<std::uint8_t> data;
};

struct Kr232Image {
    std::uint32_t entry = 0;
    std::uint32_t image_base = 0;  // 0 for a kernel image, 0x08000000 for a user one
    bool debug_present = false;
    std::vector<Kr232Section> sections;
};

// Serializes the image, computing the CRC with its own field zeroed. Refused:
// an entry outside every executable section, a section outside the image's half
// of the map, overlapping allocated sections, and an alignment that is not a
// power of two -- each named in `error` rather than written as a bad image.
[[nodiscard]] bool write_kr232_image(const Kr232Image& image, std::vector<std::uint8_t>& out,
                                     std::string& error);

struct LinkedImage;

// The linker's entry to this writer: maps a `LinkedImage` onto the container,
// taking the section flags from the section's type and the entry and base from
// the linked model, then calls write_kr232_image.
[[nodiscard]] bool write_kr232_container(const LinkedImage& image, std::vector<std::uint8_t>& out,
                                         std::string& error);



}  // namespace krc::linker
