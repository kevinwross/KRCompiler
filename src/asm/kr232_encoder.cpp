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

std::optional<EncodedInstruction> encode_normal(unsigned op, unsigned rd, unsigned rb,
                                                std::int32_t immediate, std::string& error) {
    if (op > 31) {
        error = "normal op out of range";
        return std::nullopt;
    }
    if (!check_register(rd, error)) {
        return std::nullopt;
    }
    if (!check_register(rb, error)) {
        return std::nullopt;
    }
    EncodedInstruction out;
    out.bytes[0] = static_cast<std::uint8_t>(0xC0u | op);
    out.length = 3;

    const auto payload = static_cast<std::uint8_t>((rd << 4) | rb);
    const auto d8 = [&](std::int32_t value) {
        if (value < -128 || value > 127) {
            error = "displacement out of the signed byte range";
            return false;
        }
        out.bytes[2] = static_cast<std::uint8_t>(static_cast<std::int8_t>(value));
        return true;
    };
    const auto imm8 = [&](std::int32_t value, std::int32_t low, std::int32_t high) {
        if (value < low || value > high) {
            error = "immediate out of range for this opcode";
            return false;
        }
        out.bytes[2] = static_cast<std::uint8_t>(value & 0xFF);
        return true;
    };
    const auto word16 = [&](std::int32_t value, std::int32_t low, std::int32_t high) {
        if (value < low || value > high) {
            error = "16-bit operand out of range for this opcode";
            return false;
        }
        const auto bits = static_cast<std::uint16_t>(value & 0xFFFF);
        out.bytes[1] = static_cast<std::uint8_t>(bits & 0xFF);
        out.bytes[2] = static_cast<std::uint8_t>((bits >> 8) & 0xFF);
        return true;
    };

    switch (op) {
        case 0:  // LD Rd, [Rb+d8]
        case 1:  // ST [Rb+d8], Rs
        case 2:  // LDBU
        case 3:  // LDBS
        case 4:  // STB
        case 14:  // LEA
        case 29:  // LDHU
        case 30:  // STH
        case 31:  // LDHS
            out.bytes[1] = payload;
            if (!d8(immediate)) {
                return std::nullopt;
            }
            return out;
        case 5:  // LD Rd, #s8
        case 7:  // ADD
        case 8:  // SUB
        case 9:  // CMP
            out.bytes[1] = static_cast<std::uint8_t>(rd << 4);
            if (!imm8(immediate, -128, 127)) {
                return std::nullopt;
            }
            return out;
        case 6:   // LD Rd, #u8
        case 10:  // CMP Rd, #u8
        case 11:  // AND
        case 12:  // OR
        case 13:  // XOR
            out.bytes[1] = static_cast<std::uint8_t>(rd << 4);
            if (!imm8(immediate, 0, 255)) {
                return std::nullopt;
            }
            return out;
        case 20:  // SHL
        case 21:  // SHR
        case 22:  // SAR
            out.bytes[1] = static_cast<std::uint8_t>(rd << 4);
            if (!imm8(immediate, 0, 31)) {
                return std::nullopt;
            }
            return out;
        case 15:  // JMP rel16
        case 16:  // CALL rel16
            out.bytes[1] = 0;
            return word16(immediate, -32768, 32767) ? std::optional<EncodedInstruction>(out)
                                                    : std::nullopt;
        case 19:  // SETcc Rd
            if (rb > 13) {
                error = "setcc condition 14 or 15 is illegal";
                return std::nullopt;
            }
            out.bytes[1] = payload;
            out.bytes[2] = 0;
            return out;
        case 23:  // PUSH #mask16
        case 24:  // POP #mask16
        case 28:  // LEAVE #mask16
            out.bytes[1] = 0;
            return word16(immediate, 0, 0xFFFF) ? std::optional<EncodedInstruction>(out)
                                                : std::nullopt;
        case 25:  // DIV Rd, Rb
        case 26:  // DIVS Rd, Rb
            out.bytes[1] = payload;
            out.bytes[2] = 0;
            return out;
        case 17:  // LDBU Rd, [PC+d8]
        case 18:  // LDBS Rd, [PC+d8]
        case 27:  // SXTB Rd
            out.bytes[1] = static_cast<std::uint8_t>(rd << 4);
            if (!d8(op == 27 ? 0 : immediate)) {
                return std::nullopt;
            }
            return out;
    }
    error = "unknown normal opcode";
    return std::nullopt;
}

std::optional<EncodedInstruction> encode_generic(unsigned operation, const Operand& one,
                                                 const Operand& two, std::string& error) {
    if (operation > 0x1E) {
        error = "generic operation out of range";
        return std::nullopt;
    }
    for (const Operand* operand : {&one, &two}) {
        if (operand->reg > 15) {
            error = "register out of range";
            return std::nullopt;
        }
        if (operand->mode > 7) {
            error = "addressing mode out of range";
            return std::nullopt;
        }
        if (operand->mode == 1 && operand->reg != 0) {
            error = "an immediate specifier must have a zero register field";
            return std::nullopt;
        }
    }
    const bool one_extends = one.mode == 1 || one.mode >= 5;
    const bool two_extends = two.mode == 1 || two.mode >= 5;
    if (one_extends && two_extends) {
        error = "only one operand may take an extension";
        return std::nullopt;
    }

    EncodedInstruction out;
    out.bytes[0] = 0xF0;
    out.bytes[1] = static_cast<std::uint8_t>(operation);
    out.bytes[2] = static_cast<std::uint8_t>((one.mode << 4) | one.reg);
    out.bytes[3] = static_cast<std::uint8_t>((two.mode << 4) | two.reg);
    out.length = 4;

    const Operand* extending = one_extends ? &one : (two_extends ? &two : nullptr);
    if (extending == nullptr) {
        return out;
    }
    const auto append = [&](std::uint32_t bits, unsigned bytes) {
        for (unsigned i = 0; i < bytes; ++i) {
            out.bytes[out.length++] = static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFF);
        }
    };
    switch (extending->mode) {
        case 1: {  // imm16, sign-extended to 32 bits
            if (extending->value < -32768 || extending->value > 32767) {
                error = "immediate out of the signed 16-bit range";
                return std::nullopt;
            }
            append(static_cast<std::uint16_t>(extending->value & 0xFFFF), 2);
            return out;
        }
        case 5:
            if (extending->value < -128 || extending->value > 127) {
                error = "displacement out of the signed byte range";
                return std::nullopt;
            }
            append(static_cast<std::uint8_t>(extending->value & 0xFF), 1);
            return out;
        case 6:
            if (extending->value < -32768 || extending->value > 32767) {
                error = "displacement out of the signed 16-bit range";
                return std::nullopt;
            }
            append(static_cast<std::uint16_t>(extending->value & 0xFFFF), 2);
            return out;
        default:  // mode 7: a full 32-bit displacement
            append(static_cast<std::uint32_t>(extending->value), 4);
            return out;
    }
}

std::optional<EncodedInstruction> encode_long_integer(unsigned op, unsigned rd, std::int32_t operand,
                                                      std::string& error) {
    if (op > 7) {
        error = "long-integer op out of range";
        return std::nullopt;
    }
    if (!check_register(rd, error)) {
        return std::nullopt;
    }
    EncodedInstruction out;
    out.bytes[0] = 0xF1;
    out.bytes[1] = static_cast<std::uint8_t>((op << 4) | rd);
    const auto bits = static_cast<std::uint32_t>(operand);
    out.bytes[2] = static_cast<std::uint8_t>(bits & 0xFF);
    out.bytes[3] = static_cast<std::uint8_t>((bits >> 8) & 0xFF);
    out.bytes[4] = static_cast<std::uint8_t>((bits >> 16) & 0xFF);
    out.bytes[5] = static_cast<std::uint8_t>((bits >> 24) & 0xFF);
    out.length = 6;
    return out;
}

std::optional<EncodedInstruction> encode_fp_register(unsigned op, unsigned fd, unsigned fs,
                                                     std::string& error) {
    if (op > 9) {
        error = "fp register op out of range";
        return std::nullopt;
    }
    if (fd > 3 || fs > 3) {
        error = "fp register out of range";
        return std::nullopt;
    }
    // FNEG, FABS, FSQRT and FRND are unary: Fs is reserved and must be zero.
    if ((op == 4 || op == 5 || op == 6 || op == 9) && fs != 0) {
        error = "this fp operation takes no second register";
        return std::nullopt;
    }
    EncodedInstruction out;
    out.bytes[0] = 0xF3;
    out.bytes[1] = static_cast<std::uint8_t>((op << 4) | (fd << 2) | fs);
    out.length = 2;
    return out;
}

std::optional<EncodedInstruction> encode_fp_memory(unsigned op, unsigned fd, unsigned mode,
                                                   unsigned base, std::int32_t displacement,
                                                   std::string& error) {
    if (op > 3) {
        error = "fp memory op out of range";
        return std::nullopt;
    }
    if (fd > 3) {
        error = "fp register out of range";
        return std::nullopt;
    }
    if (mode > 3) {
        error = "fp addressing mode out of range";
        return std::nullopt;
    }
    if (!check_register(base, error)) {
        return std::nullopt;
    }
    EncodedInstruction out;
    out.bytes[0] = 0xF4;
    out.bytes[1] = static_cast<std::uint8_t>((op << 4) | (fd << 2) | mode);
    out.bytes[2] = static_cast<std::uint8_t>(base << 4);  // the low nibble is reserved
    out.length = 3;
    if (mode == 1) {
        if (displacement < -128 || displacement > 127) {
            error = "displacement out of the signed byte range";
            return std::nullopt;
        }
        out.bytes[3] = static_cast<std::uint8_t>(displacement & 0xFF);
        out.length = 4;
    }
    return out;
}

std::optional<EncodedInstruction> encode_fp_convert(unsigned op, unsigned fd, unsigned rb,
                                                    std::string& error) {
    if (op > 3) {
        error = "fp conversion op out of range";
        return std::nullopt;
    }
    if (fd > 3) {
        error = "fp register out of range";
        return std::nullopt;
    }
    if (!check_register(rb, error)) {
        return std::nullopt;
    }
    EncodedInstruction out;
    out.bytes[0] = 0xF5;
    out.bytes[1] = static_cast<std::uint8_t>((op << 4) | (fd << 2));
    out.bytes[2] = static_cast<std::uint8_t>(rb << 4);
    out.length = 3;
    return out;
}

std::optional<EncodedInstruction> encode_fp_stack(unsigned op, unsigned fd, std::string& error) {
    if (op > 1) {
        error = "fp stack op out of range";
        return std::nullopt;
    }
    if (fd > 3) {
        error = "fp register out of range";
        return std::nullopt;
    }
    EncodedInstruction out;
    out.bytes[0] = 0xF6;
    out.bytes[1] = static_cast<std::uint8_t>((op << 2) | fd);
    out.length = 2;
    return out;
}

std::optional<EncodedInstruction> encode_enter(std::uint32_t frame, unsigned mask,
                                               std::string& error) {
    if (frame % 4u != 0) {
        error = "the frame size must be a multiple of four";
        return std::nullopt;
    }
    if (mask > 0x1FFFu) {
        error = "mask bits 13, 14 and 15 must be zero";
        return std::nullopt;
    }
    EncodedInstruction out;
    const auto low = static_cast<std::uint8_t>(mask & 0xFF);
    const auto high = static_cast<std::uint8_t>((mask >> 8) & 0xFF);
    if (frame <= 0xFF) {
        out.bytes[0] = 0xF7;
        out.bytes[1] = static_cast<std::uint8_t>(frame);
        out.bytes[2] = low;
        out.bytes[3] = high;
        out.length = 4;
    } else if (frame <= 0xFFF) {
        // Appendix A encodes a 300-byte frame with F8 and a 4096-byte one with FB;
        // the ISA fixes no threshold, so this picks F8 up to 4095 and M2's log
        // records the choice as a convention rather than compliance.
        out.bytes[0] = 0xF8;
        out.bytes[1] = static_cast<std::uint8_t>(frame & 0xFF);
        out.bytes[2] = static_cast<std::uint8_t>((frame >> 8) & 0xFF);
        out.bytes[3] = low;
        out.bytes[4] = high;
        out.length = 5;
    } else {
        out.bytes[0] = 0xFB;
        out.bytes[1] = static_cast<std::uint8_t>(frame & 0xFF);
        out.bytes[2] = static_cast<std::uint8_t>((frame >> 8) & 0xFF);
        out.bytes[3] = static_cast<std::uint8_t>((frame >> 16) & 0xFF);
        out.bytes[4] = static_cast<std::uint8_t>((frame >> 24) & 0xFF);
        out.bytes[5] = low;
        out.bytes[6] = high;
        out.length = 7;
    }
    return out;
}

std::optional<EncodedInstruction> encode_wide(unsigned op, unsigned rd, std::uint32_t payload,
                                              std::string& error) {
    if (op > 15) {
        error = "wide op out of range";
        return std::nullopt;
    }
    if (!check_register(rd, error)) {
        return std::nullopt;
    }
    EncodedInstruction out;
    out.bytes[0] = 0xF9;
    out.bytes[1] = static_cast<std::uint8_t>((op << 4) | rd);
    out.bytes[2] = static_cast<std::uint8_t>(payload & 0xFF);
    out.bytes[3] = static_cast<std::uint8_t>((payload >> 8) & 0xFF);
    out.bytes[4] = static_cast<std::uint8_t>((payload >> 16) & 0xFF);
    out.bytes[5] = static_cast<std::uint8_t>((payload >> 24) & 0xFF);
    out.length = 6;
    return out;
}

std::optional<EncodedInstruction> encode_fp_memory_wide(unsigned op, unsigned fd, unsigned mode,
                                                        unsigned base, std::int32_t displacement,
                                                        std::string& error) {
    if (op > 3) {
        error = "fp memory op out of range";
        return std::nullopt;
    }
    if (fd > 3 || mode > 3) {
        error = "fp register or addressing mode out of range";
        return std::nullopt;
    }
    if (!check_register(base, error)) {
        return std::nullopt;
    }
    EncodedInstruction out;
    out.bytes[0] = 0xFA;
    out.bytes[1] = static_cast<std::uint8_t>((op << 4) | (fd << 2) | mode);
    out.bytes[2] = static_cast<std::uint8_t>(base << 4);
    const auto bits = static_cast<std::uint32_t>(displacement);
    out.bytes[3] = static_cast<std::uint8_t>(bits & 0xFF);
    out.bytes[4] = static_cast<std::uint8_t>((bits >> 8) & 0xFF);
    out.bytes[5] = static_cast<std::uint8_t>((bits >> 16) & 0xFF);
    out.bytes[6] = static_cast<std::uint8_t>((bits >> 24) & 0xFF);
    out.length = 7;
    return out;
}

}  // namespace krc::asm_kr232
