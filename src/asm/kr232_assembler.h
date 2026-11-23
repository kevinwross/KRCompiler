#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "asm/kr232_object.h"

namespace krc::asm_kr232 {

// One statement's source line and the offset of its instruction in `.text`.
struct DebugLine {
    std::uint32_t offset = 0;
    std::uint32_t line = 0;
    std::string file;
};

struct AssembledProgram {
    Program program;
    std::vector<DebugLine> lines;
};

// Assembles the kr232 text subset the backend will emit: `.text`, `.data`,
// `.globl`, `.word`, `.byte`, labels, and the instructions `parse_instruction`
// accepts. A label operand on `bra`, a `bcc`, `call` or `jmp` becomes a two-byte
// compact branch whose displacement is patched once every label is placed; a
// label out of the nine-bit range is an error rather than a truncation.
[[nodiscard]] bool assemble(std::string_view source, std::string_view file_name,
                            AssembledProgram& out, std::string& error);

}  // namespace krc::asm_kr232
