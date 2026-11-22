#pragma once

#include <cstdint>
#include <string>
#include <string_view>

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

// Section 7, Normal class: three bytes, `110 OOOOO RRRR BBBB` then an optional byte payload.
// Opcodes 0xC0 LD, 0xC1 ST (disp8 forms), 0xD7 PUSH #mask, 0xD8 POP #mask.
[[nodiscard]] bool encode_normal_ld_d8(std::uint8_t rd, std::uint8_t base, std::int32_t displacement,
                                       Encoding& out, std::string& error);
[[nodiscard]] bool encode_normal_st_d8(std::uint8_t rs, std::uint8_t base, std::int32_t displacement,
                                       Encoding& out, std::string& error);
[[nodiscard]] bool encode_normal_push_mask(std::uint16_t mask, Encoding& out, std::string& error);
[[nodiscard]] bool encode_normal_pop_mask(std::uint16_t mask, Encoding& out, std::string& error);

// Further Normal-class forms: 0xC7 ADD imm8, 0xCF JMP absolute, 0xDC LEAVE #mask.
[[nodiscard]] bool encode_normal_add_imm8(std::uint8_t rd, std::int32_t value, Encoding& out,
                                          std::string& error);
[[nodiscard]] bool encode_normal_jmp_absolute(std::uint16_t address, Encoding& out);
[[nodiscard]] bool encode_normal_leave(std::uint16_t mask, Encoding& out, std::string& error);

// Remaining Normal forms: 0xCA CMP imm8, 0xDB SXTB, 0xD0 CALL absolute, 0xD4-0xD6 shifts.
[[nodiscard]] bool encode_normal_cmp_imm8(std::uint8_t rd, std::int32_t value, Encoding& out,
                                          std::string& error);
[[nodiscard]] bool encode_normal_sxtb(std::uint8_t rd, Encoding& out, std::string& error);
[[nodiscard]] bool encode_normal_call_absolute(std::uint16_t address, Encoding& out);
[[nodiscard]] bool encode_normal_shift_imm(std::uint8_t kind, std::uint8_t rd, std::uint8_t count,
                                           Encoding& out, std::string& error);

// Section 8.1, the canonical `F0` form: `F0 op spec1 spec2 [ext]`, spec = `0 MMM RRRR`.
// The caller supplies the extension bytes the two modes require, so the encoder never guesses
// an operand's size.
[[nodiscard]] bool encode_f0(std::uint8_t opcode, std::uint8_t spec1, std::uint8_t spec2,
                             const std::uint8_t* extension, std::uint8_t extension_size,
                             Encoding& out, std::string& error);

// Section 8.3, `F3`: `OOOO Fd Fs`, the FP register-register and unary forms (unary requires
// Fs = 0). Section 8.6, `F7`: `ENTER #frame8, #mask16`.
[[nodiscard]] bool encode_f3(std::uint8_t opcode, std::uint8_t fd, std::uint8_t fs, Encoding& out,
                             std::string& error);
[[nodiscard]] bool encode_f7_enter(std::uint16_t frame, std::uint16_t mask, Encoding& out,
                                   std::string& error);

// Section 8.2, `F1`: `F1 | OOOO RRRR | addr16`, four bytes. Op 0 LDBU, 1 LDBS, 2 STB (R is the
// data register); op 3 LEA Rd,[PC+d16], op 4 LEA Rd,[SP+d16] (R is the destination).
[[nodiscard]] bool encode_f1(std::uint8_t opcode, std::uint8_t reg, std::uint16_t field,
                             Encoding& out, std::string& error);

// Section 8.6, `F6`: `OOOOOO Fd` (0 FPUSH, 1 FPOP) and `F8`: `ENTER #frame16, #mask16`.
[[nodiscard]] bool encode_f6(std::uint8_t opcode, std::uint8_t fd, Encoding& out,
                             std::string& error);
[[nodiscard]] bool encode_f8_enter(std::uint16_t frame, std::uint16_t mask, Encoding& out,
                                   std::string& error);

// Section 8.5, `F5`: `OOOO Fd 00` then `RRRR 0000`. Op 0 I2F, 1 U2F, 2 F2I, 3 F2U.
[[nodiscard]] bool encode_f5(std::uint8_t opcode, std::uint8_t fd, std::uint8_t reg,
                             Encoding& out, std::string& error);

// Section 8.4, `F4`: `F4 | OOOO Fd MM | BBBB 0000 | [d8]`. Op 0 FLD, 1 FST, 2 FLDS, 3 FSTS;
// mode 0 [Rb], 1 [Rb+d8], 2 [Rb]+, 3 -[Rb]. The d8 byte appears only for mode 1.
[[nodiscard]] bool encode_f4(std::uint8_t opcode, std::uint8_t fd, std::uint8_t mode,
                             std::uint8_t base, std::int32_t displacement, Encoding& out,
                             std::string& error);

// Assembly for the forms the compiler emits first: the system mnemonics, PUSH/POP/INC/DEC,
// `MOV Rd, Rs`, `LD Rd, #imm` and `LD Rd, [addr]`. Text in, bytes out; an unknown mnemonic or a
// malformed operand is an error naming the line, never a silent guess.
[[nodiscard]] bool parse_register(std::string_view text, std::uint8_t& out);
[[nodiscard]] bool parse_number(std::string_view text, std::uint16_t& out);
[[nodiscard]] bool assemble_line(std::string_view line, Encoding& out, std::string& error);

}  // namespace krc::asmkr216
