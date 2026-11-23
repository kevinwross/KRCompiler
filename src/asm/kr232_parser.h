#pragma once

#include "kr232_encoder.h"

#include <optional>
#include <string>
#include <string_view>

namespace krc::asm_kr232 {

// Parses one kr232 assembly instruction -- no label, no directive -- into its
// bytes. Mnemonics and register names are case-insensitive; an operand is
// `rN`, `fN` where the form takes an FP register, `#imm`, a bare number, or a
// bracketed address of the ISA §9 modes this target's text uses.
//
// Branch and call displacements are given as signed numbers relative to the
// next instruction, matching how the assembler will resolve a label.
[[nodiscard]] std::optional<EncodedInstruction> parse_instruction(std::string_view text,
                                                                  std::string& error);

}  // namespace krc::asm_kr232
