#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "asm/kr232_object.h"
#include "krc/toolchain.h"

namespace krc::asm_kr232 {

// One statement's source line and the offset of its instruction in `.text`.
// `source` is the id of the S record the line belongs to: 1 for the translation
// unit itself (the assembler's `file_name`), higher for a file introduced by a
// `.debug_line <path> <line>` directive, such as a header whose inline code this
// unit emits. Hand-written assembly has no directives, so its records stay on
// source 1 and use the assembler's physical line numbers.
struct DebugLine {
    std::uint32_t offset = 0;
    std::uint32_t line = 0;
    std::uint32_t source = 1;
    std::string file;
};

struct AssembledProgram {
    Program program;
    std::vector<DebugLine> lines;
    // One row per statement, so `-l` can render a listing without re-reading the
    // assembly text. Filled from `lines` and the source, which is why it lives here
    // rather than in the object writer: the source is not available there.
    std::vector<toolchain::ListingRow> listing;
};

// Assembles the kr232 text subset the backend will emit: `.text`, `.data`,
// `.globl`, `.word`, `.byte`, labels, and the instructions `parse_instruction`
// accepts. A label operand on `bra`, a `bcc`, `call` or `jmp` becomes a two-byte
// compact branch whose displacement is patched once every label is placed; a
// label out of the nine-bit range is an error rather than a truncation.
//
// `far_calls` emits every call in the indirect form (`ld r8, #symbol; call r8`).
// A call that crosses an object boundary cannot be widened afterwards -- the
// linker patches a field, it cannot grow one -- so an object whose calls all
// leave it asks for the wide form here.
[[nodiscard]] bool assemble(std::string_view source, std::string_view file_name,
                            AssembledProgram& out, std::string& error,
                            bool far_calls = false);

}  // namespace krc::asm_kr232
