#pragma once

#include <cstdint>
#include <string>

namespace krc::asmkr216 {

// kr216 instruction encodings, ../kr216/kr216-isa.md (frozen 1.0). The encoder emits bytes;
// each function validates its operands and reports why it refused rather than guessing.
inline constexpr std::uint8_t kRegisterCount = 16;
inline constexpr std::uint8_t kStackPointer = 15;

struct Encoding {
    std::uint8_t bytes[6] = {};
    std::uint8_t size = 0;
};

// Section 5, Register class: `LD Rd, #imm16` (0x50+R), `LD Rd, [addr16]` (0x60+R),
// `ST [addr16], Rd` (0xE0+R). Three bytes each, low byte first.
[[nodiscard]] bool encode_ld_immediate(std::uint8_t rd, std::uint16_t value, Encoding& out,
                                       std::string& error);
[[nodiscard]] bool encode_ld_absolute(std::uint8_t rd, std::uint16_t address, Encoding& out,
                                      std::string& error);
[[nodiscard]] bool encode_st_absolute(std::uint8_t rd, std::uint16_t address, Encoding& out,
                                      std::string& error);

}  // namespace krc::asmkr216
