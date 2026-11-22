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

// Section 4.1, the one-byte forms: PUSH/POP/INC/DEC/CALL Rd and the nine system encodings.
[[nodiscard]] bool encode_push_register(std::uint8_t rd, Encoding& out, std::string& error);
[[nodiscard]] bool encode_pop_register(std::uint8_t rd, Encoding& out, std::string& error);
[[nodiscard]] bool encode_inc_register(std::uint8_t rd, Encoding& out, std::string& error);
[[nodiscard]] bool encode_dec_register(std::uint8_t rd, Encoding& out, std::string& error);
[[nodiscard]] bool encode_call_register(std::uint8_t rd, Encoding& out, std::string& error);
// 0x70 RET, 0x71 NOP, 0x72 BRK, 0x73 SYS, 0x74 EI, 0x75 DI, 0x76 HALT, 0x77 IRET, 0x78 RDCYCLE.
[[nodiscard]] bool encode_system(std::uint8_t opcode, Encoding& out, std::string& error);

// Section 6, Compact class: `100 OOOOO Ra Rb` (0x80-0x9F, op 0-31) and the branch form
// `101 CCCC ddddddddd` (0xA0-0xBF), whose displacement is relative to the next instruction.
[[nodiscard]] bool encode_compact(std::uint8_t opcode, std::uint8_t ra, std::uint8_t rb,
                                  Encoding& out, std::string& error);
[[nodiscard]] bool encode_branch(std::uint8_t condition, std::int32_t displacement,
                                 Encoding& out, std::string& error);

}  // namespace krc::asmkr216
