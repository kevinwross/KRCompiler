#include "asm/kr216_encoder.h"

namespace krc::asmkr216 {
namespace {

bool check_register(std::uint8_t rd, std::string& error) {
    if (rd >= kRegisterCount) {
        error = "kr216 register out of range: r" + std::to_string(rd);
        return false;
    }
    return true;
}

bool encode_three(std::uint8_t rd, std::uint8_t base, std::uint16_t payload, Encoding& out,
                  std::string& error) {
    if (!check_register(rd, error)) return false;
    out.bytes[0] = static_cast<std::uint8_t>(base | rd);
    out.bytes[1] = static_cast<std::uint8_t>(payload & 0xFF);
    out.bytes[2] = static_cast<std::uint8_t>(payload >> 8);
    out.size = 3;
    return true;
}

bool encode_short_register(std::uint8_t rd, std::uint8_t base, Encoding& out,
                           std::string& error) {
    if (!check_register(rd, error)) return false;
    out.bytes[0] = static_cast<std::uint8_t>(base | rd);
    out.size = 1;
    return true;
}

}  // namespace

bool encode_ld_immediate(std::uint8_t rd, std::uint16_t value, Encoding& out,
                         std::string& error) {
    return encode_three(rd, 0x50, value, out, error);
}

bool encode_ld_absolute(std::uint8_t rd, std::uint16_t address, Encoding& out,
                        std::string& error) {
    return encode_three(rd, 0x60, address, out, error);
}

bool encode_st_absolute(std::uint8_t rd, std::uint16_t address, Encoding& out,
                        std::string& error) {
    return encode_three(rd, 0xE0, address, out, error);
}

bool encode_push_register(std::uint8_t rd, Encoding& out, std::string& error) {
    return encode_short_register(rd, 0x00, out, error);
}

bool encode_pop_register(std::uint8_t rd, Encoding& out, std::string& error) {
    return encode_short_register(rd, 0x10, out, error);
}

bool encode_inc_register(std::uint8_t rd, Encoding& out, std::string& error) {
    return encode_short_register(rd, 0x20, out, error);
}

bool encode_dec_register(std::uint8_t rd, Encoding& out, std::string& error) {
    return encode_short_register(rd, 0x30, out, error);
}

bool encode_call_register(std::uint8_t rd, Encoding& out, std::string& error) {
    return encode_short_register(rd, 0x40, out, error);
}

bool encode_system(std::uint8_t opcode, Encoding& out, std::string& error) {
    if (opcode < 0x70 || opcode > 0x78) {
        error = "kr216 system opcode out of range: 0x" + std::to_string(opcode);
        return false;
    }
    out.bytes[0] = opcode;
    out.size = 1;
    return true;
}

bool encode_compact(std::uint8_t opcode, std::uint8_t ra, std::uint8_t rb, Encoding& out,
                    std::string& error) {
    if (opcode > 0x1F) {
        error = "kr216 compact opcode out of range";
        return false;
    }
    if (!check_register(ra, error) || !check_register(rb, error)) return false;
    out.bytes[0] = static_cast<std::uint8_t>(0x80 | opcode);
    out.bytes[1] = static_cast<std::uint8_t>((ra << 4) | rb);
    out.size = 2;
    return true;
}

bool encode_branch(std::uint8_t condition, std::int32_t displacement, Encoding& out,
                   std::string& error) {
    if (condition > 15) {
        error = "kr216 branch condition must be 0-15 (14 is AL, 15 is BRA)";
        return false;
    }
    if (displacement < -256 || displacement > 255) {
        error = "kr216 branch displacement out of the disp9 range";
        return false;
    }
    // The 16-bit value packs from the most significant bit, so the bytes are big-endian.
    const auto packed = static_cast<std::uint16_t>(0xA000 | (condition << 9) |
                                                   (static_cast<std::uint16_t>(displacement) & 0x1FF));
    out.bytes[0] = static_cast<std::uint8_t>(packed >> 8);
    out.bytes[1] = static_cast<std::uint8_t>(packed & 0xFF);
    out.size = 2;
    return true;
}

bool encode_normal_ld_d8(std::uint8_t rd, std::uint8_t base, std::int32_t displacement,
                         Encoding& out, std::string& error) {
    if (!check_register(rd, error) || !check_register(base, error)) return false;
    if (displacement < -128 || displacement > 127) {
        error = "kr216 disp8 out of range";
        return false;
    }
    out.bytes[0] = 0xC0;
    out.bytes[1] = static_cast<std::uint8_t>((rd << 4) | base);
    out.bytes[2] = static_cast<std::uint8_t>(displacement & 0xFF);
    out.size = 3;
    return true;
}

bool encode_normal_st_d8(std::uint8_t rs, std::uint8_t base, std::int32_t displacement,
                         Encoding& out, std::string& error) {
    if (!check_register(rs, error) || !check_register(base, error)) return false;
    if (displacement < -128 || displacement > 127) {
        error = "kr216 disp8 out of range";
        return false;
    }
    // The store names the data register in specifier 1 and the base in specifier 2.
    out.bytes[0] = 0xC1;
    out.bytes[1] = static_cast<std::uint8_t>((rs << 4) | base);
    out.bytes[2] = static_cast<std::uint8_t>(displacement & 0xFF);
    out.size = 3;
    return true;
}

bool encode_normal_push_mask(std::uint16_t mask, Encoding& out, std::string& error) {
    if (mask == 0) {
        error = "kr216 PUSH mask may not be empty";
        return false;
    }
    out.bytes[0] = 0xD7;
    out.bytes[1] = static_cast<std::uint8_t>(mask & 0xFF);
    out.bytes[2] = static_cast<std::uint8_t>(mask >> 8);
    out.size = 3;
    return true;
}

bool encode_normal_pop_mask(std::uint16_t mask, Encoding& out, std::string& error) {
    if (mask == 0) {
        error = "kr216 POP mask may not be empty";
        return false;
    }
    out.bytes[0] = 0xD8;
    out.bytes[1] = static_cast<std::uint8_t>(mask & 0xFF);
    out.bytes[2] = static_cast<std::uint8_t>(mask >> 8);
    out.size = 3;
    return true;
}

bool encode_normal_add_imm8(std::uint8_t rd, std::int32_t value, Encoding& out,
                            std::string& error) {
    if (!check_register(rd, error)) return false;
    if (value < -128 || value > 127) {
        error = "kr216 imm8 out of range";
        return false;
    }
    out.bytes[0] = 0xC7;
    out.bytes[1] = static_cast<std::uint8_t>((rd << 4) | 0);
    out.bytes[2] = static_cast<std::uint8_t>(value & 0xFF);
    out.size = 3;
    return true;
}

bool encode_normal_jmp_absolute(std::uint16_t address, Encoding& out) {
    out.bytes[0] = 0xCF;
    out.bytes[1] = static_cast<std::uint8_t>(address & 0xFF);
    out.bytes[2] = static_cast<std::uint8_t>(address >> 8);
    out.size = 3;
    return true;
}

bool encode_normal_leave(std::uint16_t mask, Encoding& out, std::string& error) {
    if (mask == 0) {
        error = "kr216 LEAVE mask may not be empty";
        return false;
    }
    out.bytes[0] = 0xDC;
    out.bytes[1] = static_cast<std::uint8_t>(mask & 0xFF);
    out.bytes[2] = static_cast<std::uint8_t>(mask >> 8);
    out.size = 3;
    return true;
}

bool encode_normal_cmp_imm8(std::uint8_t rd, std::int32_t value, Encoding& out,
                            std::string& error) {
    if (!check_register(rd, error)) return false;
    if (value < -128 || value > 127) {
        error = "kr216 imm8 out of range";
        return false;
    }
    out.bytes[0] = 0xCA;
    out.bytes[1] = static_cast<std::uint8_t>(rd << 4);
    out.bytes[2] = static_cast<std::uint8_t>(value & 0xFF);
    out.size = 3;
    return true;
}

bool encode_normal_sxtb(std::uint8_t rd, Encoding& out, std::string& error) {
    if (!check_register(rd, error)) return false;
    out.bytes[0] = 0xDB;
    out.bytes[1] = static_cast<std::uint8_t>(rd << 4);
    out.bytes[2] = 0;
    out.size = 3;
    return true;
}

bool encode_normal_call_absolute(std::uint16_t address, Encoding& out) {
    out.bytes[0] = 0xD0;
    out.bytes[1] = static_cast<std::uint8_t>(address & 0xFF);
    out.bytes[2] = static_cast<std::uint8_t>(address >> 8);
    out.size = 3;
    return true;
}

// kind 0 = SHL (0xD4), 1 = SHR (0xD5), 2 = SAR (0xD6); the count occupies the low five bits.
bool encode_normal_shift_imm(std::uint8_t kind, std::uint8_t rd, std::uint8_t count, Encoding& out,
                             std::string& error) {
    if (kind > 2) {
        error = "kr216 shift kind must be 0-2";
        return false;
    }
    if (!check_register(rd, error)) return false;
    if (count > 31) {
        error = "kr216 shift count out of range";
        return false;
    }
    out.bytes[0] = static_cast<std::uint8_t>(0xD4 + kind);
    out.bytes[1] = static_cast<std::uint8_t>(rd << 4);
    out.bytes[2] = count;
    out.size = 3;
    return true;
}

bool encode_f0(std::uint8_t opcode, std::uint8_t spec1, std::uint8_t spec2,
               const std::uint8_t* extension, std::uint8_t extension_size, Encoding& out,
               std::string& error) {
    if (opcode > 0x1A) {
        error = "kr216 F0 opcode out of range";
        return false;
    }
    const auto check_spec = [&error](std::uint8_t spec, const char* which) {
        const std::uint8_t mode = static_cast<std::uint8_t>(spec >> 4);
        if (mode > 6) {
            error = std::string("kr216 F0 ") + which + " mode out of range";
            return false;
        }
        return true;
    };
    if (!check_spec(spec1, "operand 1") || !check_spec(spec2, "operand 2")) return false;
    if (extension_size > 2) {
        error = "kr216 F0 extension must be at most two bytes";
        return false;
    }
    out.bytes[0] = 0xF0;
    out.bytes[1] = opcode;
    out.bytes[2] = spec1;
    out.bytes[3] = spec2;
    std::uint8_t size = 4;
    for (std::uint8_t index = 0; index < extension_size; ++index) {
        out.bytes[size++] = extension[index];
    }
    out.size = size;
    return true;
}

bool encode_f3(std::uint8_t opcode, std::uint8_t fd, std::uint8_t fs, Encoding& out,
               std::string& error) {
    if (opcode > 9) {
        error = "kr216 F3 opcode out of range";
        return false;
    }
    if (fd > 3 || fs > 3) {
        error = "kr216 FP register must be f0-f3";
        return false;
    }
    const bool unary = opcode >= 4 && opcode <= 6 || opcode == 9;  // FNEG FABS FSQRT FRND
    if (unary && fs != 0) {
        error = "kr216 unary FP instruction requires Fs = 0";
        return false;
    }
    out.bytes[0] = 0xF3;
    out.bytes[1] = static_cast<std::uint8_t>((opcode << 4) | (fd << 2) | fs);
    out.size = 2;
    return true;
}

bool encode_f7_enter(std::uint16_t frame, std::uint16_t mask, Encoding& out, std::string& error) {
    if ((frame & 1u) != 0 || frame > 254) {
        error = "kr216 ENTER frame must be even and at most 254";
        return false;
    }
    if (mask == 0) {
        error = "kr216 ENTER mask may not be empty";
        return false;
    }
    out.bytes[0] = 0xF7;
    out.bytes[1] = static_cast<std::uint8_t>(frame);
    out.bytes[2] = static_cast<std::uint8_t>(mask & 0xFF);
    out.bytes[3] = static_cast<std::uint8_t>(mask >> 8);
    out.size = 4;
    return true;
}

bool encode_f1(std::uint8_t opcode, std::uint8_t reg, std::uint16_t field, Encoding& out,
               std::string& error) {
    if (opcode > 4) {
        error = "kr216 F1 opcode must be 0-4";
        return false;
    }
    if (!check_register(reg, error)) return false;
    out.bytes[0] = 0xF1;
    out.bytes[1] = static_cast<std::uint8_t>((opcode << 4) | reg);
    out.bytes[2] = static_cast<std::uint8_t>(field & 0xFF);
    out.bytes[3] = static_cast<std::uint8_t>(field >> 8);
    out.size = 4;
    return true;
}

bool encode_f6(std::uint8_t opcode, std::uint8_t fd, Encoding& out, std::string& error) {
    if (opcode > 1) {
        error = "kr216 F6 opcode must be 0 (FPUSH) or 1 (FPOP)";
        return false;
    }
    if (fd > 3) {
        error = "kr216 FP register must be f0-f3";
        return false;
    }
    out.bytes[0] = 0xF6;
    out.bytes[1] = static_cast<std::uint8_t>((opcode << 2) | fd);
    out.size = 2;
    return true;
}

bool encode_f8_enter(std::uint16_t frame, std::uint16_t mask, Encoding& out, std::string& error) {
    if ((frame & 1u) != 0) {
        error = "kr216 ENTER frame must be even";
        return false;
    }
    if (mask == 0) {
        error = "kr216 ENTER mask may not be empty";
        return false;
    }
    out.bytes[0] = 0xF8;
    out.bytes[1] = static_cast<std::uint8_t>(frame & 0xFF);
    out.bytes[2] = static_cast<std::uint8_t>(frame >> 8);
    out.bytes[3] = static_cast<std::uint8_t>(mask & 0xFF);
    out.bytes[4] = static_cast<std::uint8_t>(mask >> 8);
    out.size = 5;
    return true;
}

bool encode_f5(std::uint8_t opcode, std::uint8_t fd, std::uint8_t reg, Encoding& out,
               std::string& error) {
    if (opcode > 3) {
        error = "kr216 F5 opcode must be 0-3";
        return false;
    }
    if (fd > 3) {
        error = "kr216 FP register must be f0-f3";
        return false;
    }
    if (!check_register(reg, error)) return false;
    out.bytes[0] = 0xF5;
    out.bytes[1] = static_cast<std::uint8_t>((opcode << 4) | (fd << 2));
    out.bytes[2] = static_cast<std::uint8_t>(reg << 4);
    out.size = 3;
    return true;
}

}  // namespace krc::asmkr216
