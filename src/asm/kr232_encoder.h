#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace krc::asm_kr232 {

// One encoded instruction: up to eight bytes, as the ISA's longest form needs.
struct EncodedInstruction {
    std::array<std::uint8_t, 8> bytes{};
    unsigned length = 0;
};

// The Short class of ISA §4: push, pop, inc, dec, call and the system group.
enum class ShortOp : std::uint8_t {
    Push,
    Pop,
    Inc,
    Dec,
    Call,
    Ret,
    Nop,
    Brk,
    Sys,
    Ei,
    Di,
    Halt,
    Iret,
    RdCycle,
    SysCall,
};

// Short form. `reg` is ignored by the system group, which has no operand.
[[nodiscard]] std::optional<EncodedInstruction> encode_short(ShortOp op, unsigned reg,
                                                             std::string& error);

// Register form of ISA §5: `LD Rd, #imm32`, `LD Rd, [addr32]`, `ST [addr32], Rd`.
[[nodiscard]] std::optional<EncodedInstruction> encode_ld_immediate(unsigned rd,
                                                                    std::uint32_t value,
                                                                    std::string& error);
[[nodiscard]] std::optional<EncodedInstruction> encode_ld_absolute(unsigned rd,
                                                                   std::uint32_t address,
                                                                   std::string& error);
[[nodiscard]] std::optional<EncodedInstruction> encode_st_absolute(unsigned rd,
                                                                   std::uint32_t address,
                                                                   std::string& error);

// Compact branch of ISA §6.2: a nine-bit displacement from the next instruction,
// so the range is -256..+255. Conditions 0-13 are the named branches, 14 is
// `call`, 15 is `bra`.
[[nodiscard]] std::optional<EncodedInstruction> encode_compact_branch(unsigned condition,
                                                                      std::int32_t displacement,
                                                                      std::string& error);

// Compact register form of ISA §6.1: first byte `0x80 + op`, second
// `Ra << 4 | Rb`. `Rb` must be R0 for NEG, NOT and JMP (ISA §6.1).
[[nodiscard]] std::optional<EncodedInstruction> encode_compact_register(unsigned op,
                                                                        unsigned ra,
                                                                        unsigned rb,
                                                                        std::string& error);

}  // namespace krc::asm_kr232
