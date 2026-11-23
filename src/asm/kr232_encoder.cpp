#include "kr232_encoder.h"

namespace krc::asm_kr232 {
namespace {

constexpr std::uint8_t kMinimumRegister = 0;
constexpr std::uint8_t kMaximumRegister = 15;

bool check_register(unsigned reg, std::string& error) {
    if (reg > kMaximumRegister) {
        error = "register out of range";
        return false;
    }
    return true;
}

EncodedInstruction one_byte(std::uint8_t byte) {
    EncodedInstruction out;
    out.bytes[0] = byte;
    out.length = 1;
    return out;
}

// Register-form instructions are five bytes: opcode, then the little-endian word.
EncodedInstruction register_form(std::uint8_t base, unsigned rd, std::uint32_t value) {
    EncodedInstruction out;
    out.bytes[0] = static_cast<std::uint8_t>(base + rd);
    out.bytes[1] = static_cast<std::uint8_t>(value & 0xFFu);
    out.bytes[2] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    out.bytes[3] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
    out.bytes[4] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
    out.length = 5;
    return out;
}

}  // namespace

std::optional<EncodedInstruction> encode_short(ShortOp op, unsigned reg, std::string& error) {
    switch (op) {
        case ShortOp::Push:
        case ShortOp::Pop:
        case ShortOp::Inc:
        case ShortOp::Dec:
        case ShortOp::Call: {
            if (!check_register(reg, error)) {
                return std::nullopt;
            }
            const unsigned base = (op == ShortOp::Push)  ? 0x00u
                                  : (op == ShortOp::Pop) ? 0x10u
                                  : (op == ShortOp::Inc) ? 0x20u
                                  : (op == ShortOp::Dec) ? 0x30u
                                                         : 0x40u;
            return one_byte(static_cast<std::uint8_t>(base + reg));
        }
        case ShortOp::Ret:
            return one_byte(0x70);
        case ShortOp::Nop:
            return one_byte(0x71);
        case ShortOp::Brk:
            return one_byte(0x72);
        case ShortOp::Sys:
            return one_byte(0x73);
        case ShortOp::Ei:
            return one_byte(0x74);
        case ShortOp::Di:
            return one_byte(0x75);
        case ShortOp::Halt:
            return one_byte(0x76);
        case ShortOp::Iret:
            return one_byte(0x77);
        case ShortOp::RdCycle:
            return one_byte(0x78);
        case ShortOp::SysCall:
            return one_byte(0x79);
    }
    error = "unknown short operation";
    return std::nullopt;
}

std::optional<EncodedInstruction> encode_ld_immediate(unsigned rd, std::uint32_t value,
                                                      std::string& error) {
    if (!check_register(rd, error)) {
        return std::nullopt;
    }
    return register_form(0x50, rd, value);
}

std::optional<EncodedInstruction> encode_ld_absolute(unsigned rd, std::uint32_t address,
                                                     std::string& error) {
    if (!check_register(rd, error)) {
        return std::nullopt;
    }
    return register_form(0x60, rd, address);
}

std::optional<EncodedInstruction> encode_st_absolute(unsigned rd, std::uint32_t address,
                                                     std::string& error) {
    if (!check_register(rd, error)) {
        return std::nullopt;
    }
    return register_form(0xE0, rd, address);
}

std::optional<EncodedInstruction> encode_compact_branch(unsigned condition,
                                                        std::int32_t displacement,
                                                        std::string& error) {
    if (condition > 15) {
        error = "condition out of range";
        return std::nullopt;
    }
    if (displacement < -256 || displacement > 255) {
        error = "displacement out of the nine-bit range";
        return std::nullopt;
    }
    // ISA §6.2: 0xA0 | (cond << 1) | (disp9 >> 8), then the low byte of disp9.
    // The condition sits in bits 1-4 and bit 0 is the displacement's ninth bit,
    // which is why a negative `beq` does not encode as condition 1.
    const auto bits = static_cast<std::uint16_t>(displacement & 0x1FF);
    EncodedInstruction out;
    out.bytes[0] = static_cast<std::uint8_t>(0xA0u | ((condition & 0x0Fu) << 1) |
                                             ((bits >> 8) & 1u));
    out.bytes[1] = static_cast<std::uint8_t>(bits & 0xFFu);
    out.length = 2;
    return out;
}

std::optional<EncodedInstruction> encode_compact_register(unsigned op, unsigned ra, unsigned rb,
                                                          std::string& error) {
    if (op > 31) {
        error = "register-form op out of range";
        return std::nullopt;
    }
    if (!check_register(ra, error) || !check_register(rb, error)) {
        return std::nullopt;
    }
    // NEG, NOT and JMP are unary: their second register must be R0 (ISA §6.1).
    if ((op == 29 || op == 30 || op == 31) && rb != 0) {
        error = "this operation requires R0 as the second register";
        return std::nullopt;
    }
    EncodedInstruction out;
    out.bytes[0] = static_cast<std::uint8_t>(0x80u | op);
    out.bytes[1] = static_cast<std::uint8_t>((ra << 4) | rb);
    out.length = 2;
    return out;
}

}  // namespace krc::asm_kr232
