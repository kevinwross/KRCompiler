#include "asm/kr32_encoder.h"

#include <cstdint>
#include <string>

namespace krc::asmkr32 {

namespace {

bool fits_inline(std::int32_t value) {
    return value >= -1024 && value <= 1023;
}

}  // namespace

std::optional<EncodedInstruction> encode(Opcode opcode, std::uint8_t rd, std::uint8_t rs1,
                                         std::uint8_t field, bool literal,
                                         std::int32_t immediate, bool extension,
                                         std::uint32_t extension_word, std::string& error) {
    const auto raw_opcode = static_cast<std::uint8_t>(opcode);
    if (raw_opcode > 0x7F) {
        error = "KR32 opcode out of range";
        return std::nullopt;
    }
    if (rd >= kRegisterCount || rs1 >= kRegisterCount) {
        error = "KR32 register out of range";
        return std::nullopt;
    }
    if (field > 0xF) {
        error = "KR32 instruction field out of range";
        return std::nullopt;
    }
    if (!extension && !fits_inline(immediate)) {
        error = "KR32 inline immediate does not fit in 11 signed bits";
        return std::nullopt;
    }

    std::uint32_t word = static_cast<std::uint32_t>(raw_opcode) << kOpcodeShift;
    word |= static_cast<std::uint32_t>(rd) << kRdShift;
    word |= static_cast<std::uint32_t>(rs1) << kRs1Shift;
    word |= static_cast<std::uint32_t>(field) << kFieldShift;
    if (extension) {
        word |= 1u << kExtensionShift;
    }
    if (literal) {
        word |= 1u << kLiteralShift;
    }
    word |= static_cast<std::uint32_t>(immediate) & kInlineMask;

    EncodedInstruction encoded;
    encoded.words[0] = word;
    encoded.count = 1;
    if (extension) {
        encoded.words[1] = extension_word;
        encoded.count = 2;
    }
    return encoded;
}

}  // namespace krc::asmkr32
