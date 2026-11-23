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

// One operand of the `F0` generic form: a mode from ISA §9 and its register.
// Mode 1 is an immediate held in `value`; modes 5-7 hold their displacement there.
struct Operand {
    std::uint8_t mode = 0;
    std::uint8_t reg = 0;
    std::int32_t value = 0;
};

// `F0` generic two-operand form of ISA §8.1. At most one operand may take an
// extension, which is what keeps the encoding within eight bytes.
[[nodiscard]] std::optional<EncodedInstruction> encode_generic(unsigned operation,
                                                               const Operand& one, const Operand& two,
                                                               std::string& error);

// `F1` long integer: `F1 | OOOO RRRR | addr32`, six bytes (ISA §8.2). Ops 3 and 4
// take a signed displacement from PC or SP; the rest an absolute address.
[[nodiscard]] std::optional<EncodedInstruction> encode_long_integer(unsigned op, unsigned rd,
                                                                    std::int32_t operand,
                                                                    std::string& error);

// `F3` FP register-register (ISA §8.3): binary ops 0-3 and 7-8, unary 4-6 and 9
// whose `Fs` must be zero.
[[nodiscard]] std::optional<EncodedInstruction> encode_fp_register(unsigned op, unsigned fd,
                                                                   unsigned fs, std::string& error);

// `F4` FP load/store with an 8-bit displacement (ISA §8.4): modes 0 `[Rb]`,
// 1 `[Rb+d8]` (the only one with a byte), 2 `[Rb]+`, 3 `-[Rb]`.
[[nodiscard]] std::optional<EncodedInstruction> encode_fp_memory(unsigned op, unsigned fd,
                                                                 unsigned mode, unsigned base,
                                                                 std::int32_t displacement,
                                                                 std::string& error);

// `F5` FP conversion (ISA §8.5), `F6` FP push/pop (ISA §8.6).
[[nodiscard]] std::optional<EncodedInstruction> encode_fp_convert(unsigned op, unsigned fd,
                                                                  unsigned rb, std::string& error);
[[nodiscard]] std::optional<EncodedInstruction> encode_fp_stack(unsigned op, unsigned fd,
                                                                std::string& error);

// `F7`/`F8`/`FB` ENTER (ISA §8.7): the shortest frame form that fits. The frame
// must be a multiple of four and mask bits 13-15 must be zero.
[[nodiscard]] std::optional<EncodedInstruction> encode_enter(std::uint32_t frame, unsigned mask,
                                                             std::string& error);

// `F9` wide integer and absolute control (ISA §8.8): six bytes.
[[nodiscard]] std::optional<EncodedInstruction> encode_wide(unsigned op, unsigned rd,
                                                            std::uint32_t payload,
                                                            std::string& error);

// `FA` FP load/store with a 32-bit displacement (ISA §8.9): seven bytes.
[[nodiscard]] std::optional<EncodedInstruction> encode_fp_memory_wide(unsigned op, unsigned fd,
                                                                      unsigned mode, unsigned base,
                                                                      std::int32_t displacement,
                                                                      std::string& error);

// Normal class of ISA §7: three bytes, opcode then a payload whose layout depends
// on the opcode. `rd` is the data register (for stores, the stored register), `rb`
// the base or second register, and `immediate` the d8, imm8, rel16 or mask16 that
// the opcode calls for. Fields an opcode does not use are ignored.
[[nodiscard]] std::optional<EncodedInstruction> encode_normal(unsigned op, unsigned rd,
                                                              unsigned rb,
                                                              std::int32_t immediate,
                                                              std::string& error);

}  // namespace krc::asm_kr232
