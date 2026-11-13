#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace krc::asmkr32 {

struct AssembledSymbol {
    std::string name;
    std::uint32_t address = 0;
    bool global = false;
    bool weak = false;
    std::uint32_t section = 0;
    std::uint64_t offset = 0;
};

struct AssembledRelocation {
    std::uint32_t offset = 0;
    std::string symbol;
    // 0 for .text and 1 for .data: the section whose bytes the relocation
    // patches, in the order make_object() emits them.
    std::uint32_t section = 0;
};

struct AssemblyResult {
    std::vector<std::uint8_t> text;
    std::vector<std::uint8_t> data;
    // The `.cpp_except` exception-descriptor section (ADR 0026). It is
    // allocated read-only memory in the run image, not debug data.
    std::vector<std::uint8_t> exception;
    std::vector<AssembledSymbol> symbols;
    std::vector<AssembledRelocation> relocations;
};

// Assembles the KR32 textual subset emitted by the backend. Text and data are
// returned as separate sections with text based at address zero; data symbols
// carry the aligned address they would have in the flat test image.
[[nodiscard]] bool assemble(std::string_view source, AssemblyResult& result,
                            std::string& error);

}  // namespace krc::asmkr32
