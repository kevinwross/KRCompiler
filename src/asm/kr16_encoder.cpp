#include "asm/kr16_encoder.h"

#include <cstdint>
#include <string>

namespace krc::asmkr16 {

namespace {

bool valid_register(std::uint8_t reg) { return reg < kRegisterCount; }

bool valid_signed(std::int32_t value, int bits) {
    const auto minimum = -(1 << (bits - 1));
    const auto maximum = (1 << (bits - 1)) - 1;
    return value >= minimum && value <= maximum;
}

std::uint16_t encode_r_raw(std::uint8_t rd, std::uint8_t rs,
                           std::uint8_t function) {
    return static_cast<std::uint16_t>((rd & 0x7u) << 9 |
                                      (rs & 0x7u) << 6 | (function & 0x3Fu));
}

std::uint16_t encode_m_raw(std::uint8_t opcode, std::uint8_t rd,
                           std::uint8_t rs, std::int32_t displacement) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(opcode & 0xFu) << 12 |
        (rd & 0x7u) << 9 | (rs & 0x7u) << 6 |
        (static_cast<std::uint16_t>(displacement) & 0x3Fu));
}

}  // namespace

std::optional<EncodedInstruction> encode_r(std::uint8_t rd, std::uint8_t rs,
                                           RFunction function,
                                           std::string& error) {
    if (!valid_register(rd) || !valid_register(rs)) {
        error = "KR16 register out of range";
        return std::nullopt;
    }
    const auto raw = static_cast<std::uint8_t>(function);
    if (raw > 29) {
        error = "KR16 R-type function out of range";
        return std::nullopt;
    }
    if ((raw == 28 || raw == 29) && rd != 0 && rd != 2) {
        error = "KR16 multiply-pair base must be R0 or R2";
        return std::nullopt;
    }
    EncodedInstruction encoded;
    encoded.words[0] = encode_r_raw(rd, rs, raw);
    encoded.count = 1;
    return encoded;
}

std::optional<EncodedInstruction> encode_i(std::uint8_t rd,
                                           IFunction function,
                                           std::int32_t immediate,
                                           std::string& error) {
    if (!valid_register(rd)) {
        error = "KR16 register out of range";
        return std::nullopt;
    }
    if (static_cast<std::uint8_t>(function) > 7) {
        error = "KR16 I-type function out of range";
        return std::nullopt;
    }
    if (!valid_signed(immediate, 6)) {
        error = "KR16 immediate does not fit in 6 signed bits";
        return std::nullopt;
    }
    EncodedInstruction encoded;
    encoded.words[0] = static_cast<std::uint16_t>(
        0x1000u | (rd & 0x7u) << 9 |
        (static_cast<std::uint8_t>(function) & 0x7u) << 6 |
        (static_cast<std::uint16_t>(immediate) & 0x3Fu));
    encoded.count = 1;
    return encoded;
}

std::optional<EncodedInstruction> encode_m(MOpcode opcode, std::uint8_t rd,
                                           std::uint8_t rs,
                                           std::int32_t displacement,
                                           std::string& error) {
    if (!valid_register(rd) || !valid_register(rs)) {
        error = "KR16 register out of range";
        return std::nullopt;
    }
    const auto raw = static_cast<std::uint8_t>(opcode);
    if (raw != 0x2 && raw != 0x3 && raw != 0x4 && raw != 0x5 && raw != 0x6 &&
        raw != 0xA) {
        error = "KR16 M-type opcode out of range";
        return std::nullopt;
    }
    if (!valid_signed(displacement, 6)) {
        error = "KR16 displacement does not fit in 6 signed bits";
        return std::nullopt;
    }
    EncodedInstruction encoded;
    encoded.words[0] = encode_m_raw(raw, rd, rs, displacement);
    encoded.count = 1;
    return encoded;
}

std::optional<EncodedInstruction> encode_m8(std::uint8_t rd,
                                            std::uint8_t immediate,
                                            bool sign_extend,
                                            std::string& error) {
    if (!valid_register(rd)) {
        error = "KR16 register out of range";
        return std::nullopt;
    }
    EncodedInstruction encoded;
    encoded.words[0] = static_cast<std::uint16_t>(
        0x9000u | (rd & 0x7u) << 9 | (sign_extend ? 0x100u : 0u) |
        static_cast<std::uint16_t>(immediate));
    encoded.count = 1;
    return encoded;
}

std::optional<EncodedInstruction> encode_b(Condition condition,
                                           std::int32_t word_displacement,
                                           std::string& error) {
    if (static_cast<std::uint8_t>(condition) > 15) {
        error = "KR16 branch condition out of range";
        return std::nullopt;
    }
    if (!valid_signed(word_displacement, 8)) {
        error = "KR16 branch displacement does not fit in 8 signed bits";
        return std::nullopt;
    }
    EncodedInstruction encoded;
    encoded.words[0] = static_cast<std::uint16_t>(
        0x7000u | (static_cast<std::uint8_t>(condition) & 0xFu) << 8 |
        (static_cast<std::uint16_t>(word_displacement) & 0xFFu));
    encoded.count = 1;
    return encoded;
}

std::optional<EncodedInstruction> encode_shift(std::uint8_t rd, Shift shift,
                                               std::uint8_t count,
                                               std::string& error) {
    if (!valid_register(rd)) {
        error = "KR16 register out of range";
        return std::nullopt;
    }
    if (static_cast<std::uint8_t>(shift) > 2) {
        error = "KR16 shift function out of range";
        return std::nullopt;
    }
    if (count > 15) {
        error = "KR16 shift count out of range";
        return std::nullopt;
    }
    EncodedInstruction encoded;
    encoded.words[0] = static_cast<std::uint16_t>(
        0xB000u | (rd & 0x7u) << 9 |
        (static_cast<std::uint8_t>(shift) & 0x7u) << 6 | (count & 0xFu));
    encoded.count = 1;
    return encoded;
}

std::optional<EncodedInstruction> encode_bit(std::uint8_t rd,
                                             BitOperation operation,
                                             std::uint8_t bit,
                                             std::string& error) {
    if (!valid_register(rd)) {
        error = "KR16 register out of range";
        return std::nullopt;
    }
    if (static_cast<std::uint8_t>(operation) > 3) {
        error = "KR16 bit operation out of range";
        return std::nullopt;
    }
    if (bit > 15) {
        error = "KR16 bit index out of range";
        return std::nullopt;
    }
    EncodedInstruction encoded;
    encoded.words[0] = static_cast<std::uint16_t>(
        0xC000u | (rd & 0x7u) << 9 |
        (static_cast<std::uint8_t>(operation) & 0x7u) << 6 | (bit & 0xFu));
    encoded.count = 1;
    return encoded;
}

std::optional<EncodedInstruction> encode_c(CSubop subop, std::uint8_t reg,
                                           std::uint8_t immediate,
                                           std::string& error) {
    const auto raw = static_cast<std::uint8_t>(subop);
    if (raw > 15) {
        error = "KR16 control subop out of range";
        return std::nullopt;
    }
    if (raw == 0 || raw == 1) {
        error = "KR16 relative control transfers need encode_c_relative";
        return std::nullopt;
    }
    if (reg != 0 && !valid_register(reg)) {
        error = "KR16 register out of range";
        return std::nullopt;
    }
    if (raw == 6 && immediate > 119) {
        error = "KR16 trap number out of range";
        return std::nullopt;
    }
    EncodedInstruction encoded;
    encoded.words[0] = static_cast<std::uint16_t>(
        0x8000u | (raw & 0xFu) << 8 | (reg & 0x7u) << 5);
    if (raw == 6 || raw == 15) {
        encoded.words[0] = static_cast<std::uint16_t>(
            (encoded.words[0] & 0xFF00u) | immediate);
    }
    encoded.count = 1;
    return encoded;
}

std::optional<EncodedInstruction> encode_c_relative(
    CSubop subop, std::int32_t word_displacement, std::string& error) {
    const auto raw = static_cast<std::uint8_t>(subop);
    if (raw != 0 && raw != 1) {
        error = "KR16 relative control transfer requires JMP or CALL";
        return std::nullopt;
    }
    if (!valid_signed(word_displacement, 16)) {
        error = "KR16 relative displacement does not fit in 16 signed bits";
        return std::nullopt;
    }
    EncodedInstruction encoded;
    encoded.words[0] = static_cast<std::uint16_t>(0x8000u | raw << 8);
    encoded.words[1] = static_cast<std::uint16_t>(word_displacement);
    encoded.count = 2;
    return encoded;
}

std::optional<EncodedInstruction> encode_x(XOp operation, std::uint8_t rd,
                                           std::uint8_t base,
                                           std::uint16_t immediate,
                                           std::string& error) {
    if (!valid_register(rd)) {
        error = "KR16 register out of range";
        return std::nullopt;
    }
    const auto raw = static_cast<std::uint8_t>(operation);
    if (raw > 15) {
        error = "KR16 extended op out of range";
        return std::nullopt;
    }
    if (raw < 13 && base != 0) {
        error = "KR16 extended op requires a zero aux field";
        return std::nullopt;
    }
    if (raw >= 13 && !valid_register(base)) {
        error = "KR16 register out of range";
        return std::nullopt;
    }
    EncodedInstruction encoded;
    encoded.words[0] = static_cast<std::uint16_t>(
        0xF000u | (raw & 0xFu) << 8 | (rd & 0x7u) << 5 | (base & 0x7u));
    encoded.words[1] = immediate;
    encoded.count = 2;
    return encoded;
}

}  // namespace krc::asmkr16
