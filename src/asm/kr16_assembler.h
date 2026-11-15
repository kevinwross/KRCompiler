#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

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

struct AssemblyResult {
    std::vector<std::uint8_t> text;
    std::vector<std::uint8_t> data;
    std::vector<AssembledSymbol> symbols;
    std::vector<AssembledRelocation> relocations;
};

// Assembles the KR16 textual subset emitted by the backend. Text and data are
// separate sections; text is based at address zero.
[[nodiscard]] bool assemble(std::string_view source, AssemblyResult& result,
                            std::string& error);

}  // namespace krc::asmkr16
