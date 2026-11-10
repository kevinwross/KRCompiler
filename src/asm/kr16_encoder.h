#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace krc::asmkr16 {

// KR16 v1 instruction formats, ../kr16/docs/isa.md §5.
inline constexpr std::uint8_t kRegisterCount = 8;
inline constexpr std::uint8_t kFirstGeneralRegister = 0;
inline constexpr std::uint8_t kFramePointer = 4;
inline constexpr std::uint8_t kStackPointer = 5;
inline constexpr std::uint8_t kStatusRegister = 6;
inline constexpr std::uint8_t kProgramCounter = 7;

enum class RFunction : std::uint8_t {
    Add = 0,
    Adc = 1,
    Sub = 2,
    Sbc = 3,
    Cmp = 4,
    And = 5,
    Or = 6,
    Xor = 7,
    Mov = 8,
    Test = 9,
    Neg = 10,
    Not = 11,
    Inc = 12,
    Dec = 13,
    Shl = 14,
    Shr = 15,
    Sar = 16,
    Mul = 17,
    Div = 18,
    DivU = 19,
    Mod = 20,
    ModU = 21,
    Btst = 22,
    Bset = 23,
    Bclr = 24,
    Btog = 25,
    Sext = 26,
    Zxtb = 27,
    MulS = 28,
    MulU = 29,
};

enum class IFunction : std::uint8_t {
    Add = 0,
    Sub = 1,
    Cmp = 2,
    And = 3,
    Or = 4,
    Xor = 5,
    Mov = 6,
    Test = 7,
};

enum class MOpcode : std::uint8_t {
    Load = 0x2,
    Store = 0x3,
    LoadByte = 0x4,
    LoadByteSigned = 0x5,
    StoreByte = 0x6,
    Lea = 0xA,
};

enum class Condition : std::uint8_t {
    Eq = 0,
    Ne = 1,
    Cs = 2,
    Cc = 3,
    Mi = 4,
    Pl = 5,
    Vs = 6,
    Vc = 7,
    Hi = 8,
    Ls = 9,
    Ge = 10,
    Lt = 11,
    Gt = 12,
    Le = 13,
    Al = 14,
    Nv = 15,
};

enum class Shift : std::uint8_t {
    Shl = 0,
    Shr = 1,
    Sar = 2,
};

enum class BitOperation : std::uint8_t {
    Btst = 0,
    Bset = 1,
    Bclr = 2,
    Btog = 3,
};

enum class CSubop : std::uint8_t {
    JumpRelative = 0,
    CallRelative = 1,
    JumpRegister = 2,
    CallRegister = 3,
    Return = 4,
    ReturnInterrupt = 5,
    Trap = 6,
    Break = 7,
    Halt = 8,
    Nop = 9,
    EnableInterrupts = 10,
    DisableInterrupts = 11,
    Wait = 12,
    Push = 13,
    Pop = 14,
    System = 15,
};

enum class XOp : std::uint8_t {
    MoveImmediate = 0,
    AddImmediate = 1,
    SubImmediate = 2,
    CmpImmediate = 3,
    AndImmediate = 4,
    OrImmediate = 5,
    XorImmediate = 6,
    TestImmediate = 7,
    LoadAbsolute = 8,
    StoreAbsolute = 9,
    LoadByteAbsolute = 10,
    LoadByteSignedAbsolute = 11,
    StoreByteAbsolute = 12,
    LoadIndexed = 13,
    StoreIndexed = 14,
    LeaIndexed = 15,
};

struct EncodedInstruction {
    std::uint16_t words[2] = {0, 0};
    std::uint8_t count = 0;
};

[[nodiscard]] std::optional<EncodedInstruction> encode_r(std::uint8_t rd,
                                                         std::uint8_t rs,
                                                         RFunction function,
                                                         std::string& error);
[[nodiscard]] std::optional<EncodedInstruction> encode_i(std::uint8_t rd,
                                                         IFunction function,
                                                         std::int32_t immediate,
                                                         std::string& error);
[[nodiscard]] std::optional<EncodedInstruction> encode_m(MOpcode opcode,
                                                         std::uint8_t rd,
                                                         std::uint8_t rs,
                                                         std::int32_t displacement,
                                                         std::string& error);
[[nodiscard]] std::optional<EncodedInstruction> encode_m8(std::uint8_t rd,
                                                          std::uint8_t immediate,
                                                          bool sign_extend,
                                                          std::string& error);
[[nodiscard]] std::optional<EncodedInstruction> encode_b(Condition condition,
                                                         std::int32_t word_displacement,
                                                         std::string& error);
[[nodiscard]] std::optional<EncodedInstruction> encode_shift(
    std::uint8_t rd, Shift shift, std::uint8_t count, std::string& error);
[[nodiscard]] std::optional<EncodedInstruction> encode_bit(
    std::uint8_t rd, BitOperation operation, std::uint8_t bit,
    std::string& error);
[[nodiscard]] std::optional<EncodedInstruction> encode_c(CSubop subop,
                                                         std::uint8_t reg,
                                                         std::uint8_t immediate,
                                                         std::string& error);
[[nodiscard]] std::optional<EncodedInstruction> encode_c_relative(
    CSubop subop, std::int32_t word_displacement, std::string& error);
[[nodiscard]] std::optional<EncodedInstruction> encode_x(XOp operation,
                                                         std::uint8_t rd,
                                                         std::uint8_t base,
                                                         std::uint16_t immediate,
                                                         std::string& error);

}  // namespace krc::asmkr16
