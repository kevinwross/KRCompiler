#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "krc/toolchain.h"

namespace krc::asmkr16 {

struct AssembledSymbol {
    std::string name;
    std::uint32_t section = 0;
    std::uint64_t offset = 0;
    bool global = false;
    bool external = false;
    // A weak definition is visible outside its object but may be overridden by a
    // strong one, and duplicated across objects without being an error.
    bool weak = false;
};

struct AssembledRelocation {
    std::string symbol;
    std::uint32_t section = 0;
    std::uint64_t offset = 0;
};

struct AssembledSection {
    std::string name;
    // 0 = text (exec), 1 = data, 2 = bss.
    std::uint32_t type = 0;
    std::vector<std::uint8_t> data;
    std::uint64_t size = 0;
    bool present = false;
};

struct AssemblyResult {
    std::vector<std::uint8_t> text;
    std::vector<std::uint8_t> data;
    // The size of the `.bss` section: allocated and zero-filled at load, with no
    // bytes in the object or the image.
    std::uint64_t bss_size = 0;
    // Whether the source entered each section, even if it emitted no bytes. A
    // label can live in an otherwise empty section.
    bool has_data = false;
    bool has_bss = false;
    // Every section, in id order: `.text`, `.data`, `.bss`, then the
    // `.section .text.<name>` sections in first-use order. `text` and `data`
    // above are the first two entries, kept for callers that predate named
    // sections.
    std::vector<AssembledSection> sections;
    std::vector<AssembledSymbol> symbols;
    std::vector<AssembledRelocation> relocations;
    // One row per emitted statement, in source order. Addresses are relative to
    // the start of the statement's section.
    std::vector<toolchain::ListingRow> listing;
};

// Assembles the KR16 textual subset emitted by the backend. Text and data are
// separate sections; text is based at address zero.
[[nodiscard]] bool assemble(std::string_view source, AssemblyResult& result,
                            std::string& error);

}  // namespace krc::asmkr16
