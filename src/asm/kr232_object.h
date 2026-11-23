#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "krc/target/target.h"
#include "linker/kro_object.h"

namespace krc::asm_kr232 {

// A kr232 assembly unit before linking: sections of bytes and the symbols they
// name. This is the input the object writer needs; the text parser fills it.
struct Section {
    std::string name = ".text";
    std::vector<std::uint8_t> bytes;
    std::uint64_t alignment = 4;
    // Container flags: 1 alloc, 2 write, 4 execute, 8 debug. Text is the default.
    std::uint16_t flags = 5;
};

struct Symbol {
    std::string name;
    std::uint32_t section = 0;
    std::uint64_t offset = 0;
    bool global = false;
};

struct Program {
    std::vector<Section> sections;
    std::vector<Symbol> symbols;
    // A reference the assembler could not resolve: a branch to a label in another
    // object. `offset` is the displacement field's offset in its section; the
    // linker patches it with a PC-relative 16-bit value.
    struct Relocation {
        std::uint32_t section = 0;
        std::uint32_t offset = 0;
        std::string symbol;
        std::int64_t addend = 0;
        // A branch displacement is PC-relative and two bytes; a symbol's address
        // in an instruction is absolute and four.
        bool absolute = false;
    };
    std::vector<Relocation> relocations;
};

// Writes a KRO object for a kr232 program: target id 5 through `kROTargetId`, the
// profile's pointer width, little-endian, and one section per `Program` section.
// An empty program, or a symbol whose section does not exist, is an error.
[[nodiscard]] bool make_object(const Program& program, const target::TargetProfile& profile,
                               linker::KroObject& object, std::string& error);

}  // namespace krc::asm_kr232
