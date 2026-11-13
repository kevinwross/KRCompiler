#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace krc::asmkr32 {

// KR32 v1 base instruction word fields, docs/isa-v1.md §2.
inline constexpr unsigned kOpcodeShift = 25;
inline constexpr unsigned kRdShift = 21;
inline constexpr unsigned kRs1Shift = 17;
inline constexpr unsigned kFieldShift = 13;
inline constexpr unsigned kExtensionShift = 12;
inline constexpr unsigned kLiteralShift = 11;
inline constexpr std::uint32_t kInlineMask = 0x7FF;
inline constexpr std::uint8_t kRegisterCount = 16;

enum class Opcode : std::uint8_t {
    Mov = 0x00,
    Load = 0x01,
    Store = 0x02,
    Load8U = 0x03,
    Load8S = 0x04,
    Load16U = 0x05,
    Load16S = 0x06,
    Store8 = 0x07,
    Store16 = 0x08,
    Lea = 0x09,
    LoadX = 0x10,
    StoreX = 0x11,
    Slt = 0x18,
    Sltu = 0x19,
    Add = 0x20,
    Sub = 0x21,
    Mul = 0x22,
    MulH = 0x23,
    MulHu = 0x24,
    Div = 0x25,
    DivS = 0x26,
    Mod = 0x27,
    ModS = 0x28,
    Cmp = 0x29,
    And = 0x2A,
    Or = 0x2B,
    Xor = 0x2C,
    Not = 0x2D,
    Neg = 0x2E,
    Shl = 0x30,
    Shr = 0x31,
    Sar = 0x32,
    Jmp = 0x38,
    Jcond = 0x39,
    Call = 0x3A,
    Ret = 0x3B,
    Push = 0x3C,
    Pop = 0x3D,
    Adjsp = 0x3E,
    Setsp = 0x3F,
    Syscall = 0x41,
    Halt = 0x43,
    Nop = 0x45,
    PushR = 0x4D,
    PopR = 0x4E,
    PushFP = 0x4F,
    PopFP = 0x0A,
    Wrsp = 0x4C,
    MLoad = 0x52,
    MStore = 0x53,
    MEscape = 0x61,
};

// Function codes carried in the immediate field of the 0x61 M escape,
// docs/isa-v1.md §7. Tile loads and stores do not use this group.
enum class MatrixFunction : std::uint8_t {
    Mul = 0x000,
    MulAdd = 0x001,
    Vector = 0x002,
    Transpose = 0x003,
    Dot = 0x004,
    Push = 0x005,
    Pop = 0x006,
    Drop = 0x007,
};

// Addressing modes from docs/isa-v1.md §3.
enum class AddressingMode : std::uint8_t {
    RegisterDirect = 0,
    Immediate = 1,
    RegisterIndirect = 2,
    Indexed = 3,
    StackRelative = 4,
    Absolute = 5,
    PcRelative = 6,
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

struct EncodedInstruction {
    std::uint32_t words[2] = {0, 0};
    std::uint8_t count = 0;
};

// Encodes one already-parsed KR32 instruction word. `field` carries the
// register source, addressing mode, or condition selected by the instruction
// class. When `extension` is true, `extension_word` is emitted and the inline
// immediate is ignored.
[[nodiscard]] std::optional<EncodedInstruction> encode(Opcode opcode, std::uint8_t rd,
                                                       std::uint8_t rs1, std::uint8_t field,
                                                       bool literal, std::int32_t immediate,
                                                       bool extension,
                                                       std::uint32_t extension_word,
                                                       std::string& error);

}  // namespace krc::asmkr32
