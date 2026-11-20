#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "linker/kro_object.h"

namespace krc::linker {

struct LinkOptions {
    std::uint64_t base = 0x00010000;
    std::string entry_symbol = "_start";
    std::map<std::string, std::uint64_t> section_addresses;
    // Drop objects the entry point cannot reach. Object-level, because one
    // section per translation unit is the finest granularity the format has.
    bool gc_sections = false;
};

struct LinkedSection {
    std::string name;
    KroSectionType type = KroSectionType::None;
    std::uint16_t flags = 0;
    std::uint64_t address = 0;
    std::uint64_t alignment = 1;
    std::vector<std::uint8_t> data;
    std::uint64_t size = 0;
};

struct LinkedSymbol {
    std::string name;
    std::uint64_t value = 0;
    std::uint64_t size = 0;
    std::uint32_t section = 0;
    KroSymbolBinding binding = KroSymbolBinding::Local;
    KroSymbolType type = KroSymbolType::NoType;
};

struct LinkedImage {
    std::uint32_t target_id = 1;
    std::uint64_t base = 0;
    std::uint64_t entry = 0;
    std::vector<LinkedSection> sections;
    std::vector<LinkedSymbol> symbols;
    // The final address of each input object's `.text`, in input order. Zero when
    // the object was collected or has no `.text`. A listing uses it to turn a
    // section-relative statement offset into an address.
    std::vector<std::uint64_t> object_text_addresses;
};

[[nodiscard]] bool link_objects(std::span<const KroObject> objects, const LinkOptions& options,
                                LinkedImage& image, std::string& error);

// Writes a KR32 `.k32` executable image from the linked model.
[[nodiscard]] bool write_kr32_executable(const LinkedImage& image,
                                         std::vector<std::uint8_t>& bytes,
                                         std::string& error);

// Builds the KDBG section from the textual `.debug_info` bridge.
[[nodiscard]] bool attach_kdbg(LinkedImage& image, std::string& error);

// Rewrites source paths under source_root to project-relative paths before
// building KDBG. An empty root keeps the recorded paths unchanged.
[[nodiscard]] bool attach_kdbg(LinkedImage& image,
                               const std::string& source_root,
                               std::string& error);

}  // namespace krc::linker
