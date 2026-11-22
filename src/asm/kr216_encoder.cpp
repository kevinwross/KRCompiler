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
    if (mask == 0 || (mask & 0xE000) != 0) {
        error = "kr216 ENTER mask must be non-empty and have bits 13-15 clear";
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
    if (mask == 0 || (mask & 0xE000) != 0) {
        error = "kr216 ENTER mask must be non-empty and have bits 13-15 clear";
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

bool encode_f4(std::uint8_t opcode, std::uint8_t fd, std::uint8_t mode, std::uint8_t base,
               std::int32_t displacement, Encoding& out, std::string& error) {
    if (opcode > 3) {
        error = "kr216 F4 opcode must be 0-3";
        return false;
    }
    if (fd > 3) {
        error = "kr216 FP register must be f0-f3";
        return false;
    }
    if (mode > 3) {
        error = "kr216 F4 address mode must be 0-3";
        return false;
    }
    if (!check_register(base, error)) return false;
    if (mode == 1 && (displacement < -128 || displacement > 127)) {
        error = "kr216 disp8 out of range";
        return false;
    }
    out.bytes[0] = 0xF4;
    out.bytes[1] = static_cast<std::uint8_t>((opcode << 4) | (fd << 2) | mode);
    out.bytes[2] = static_cast<std::uint8_t>(base << 4);
    out.size = 3;
    if (mode == 1) {
        out.bytes[3] = static_cast<std::uint8_t>(displacement & 0xFF);
        out.size = 4;
    }
    return true;
}

bool parse_register(std::string_view text, std::uint8_t& out) {
    if (text.size() < 2 || (text[0] != 'r' && text[0] != 'R')) return false;
    unsigned value = 0;
    for (std::size_t index = 1; index < text.size(); ++index) {
        const char c = text[index];
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<unsigned>(c - '0');
        if (value > 15) return false;
    }
    out = static_cast<std::uint8_t>(value);
    return true;
}

bool parse_number(std::string_view text, std::uint16_t& out) {
    if (text.empty()) return false;
    unsigned base = 10;
    std::size_t index = 0;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        index = 2;
    }
    if (index >= text.size()) return false;
    unsigned value = 0;
    for (; index < text.size(); ++index) {
        const char c = text[index];
        unsigned digit = 0;
        if (c >= '0' && c <= '9') {
            digit = static_cast<unsigned>(c - '0');
        } else if (base == 16 && c >= 'a' && c <= 'f') {
            digit = static_cast<unsigned>(c - 'a' + 10);
        } else if (base == 16 && c >= 'A' && c <= 'F') {
            digit = static_cast<unsigned>(c - 'A' + 10);
        } else {
            return false;
        }
        value = value * base + digit;
        if (value > 0xFFFF) return false;
    }
    out = static_cast<std::uint16_t>(value);
    return true;
}

namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
    return text;
}

// Parses `[rB+d8]` into its base register and displacement. Returns false for anything else, so
// an unimplemented operand shape is refused rather than mis-assembled.
bool parse_bracketed_displacement(std::string_view text, std::uint8_t& base, std::int32_t& disp) {
    if (text.size() < 5 || text.front() != '[' || text.back() != ']') return false;
    const auto inner = trim(text.substr(1, text.size() - 2));
    const std::size_t plus = inner.find('+');
    if (plus == std::string_view::npos) return false;
    if (!parse_register(trim(inner.substr(0, plus)), base)) return false;
    std::uint16_t offset = 0;
    if (!parse_number(trim(inner.substr(plus + 1)), offset)) return false;
    disp = static_cast<std::int32_t>(offset);
    return true;
}

}  // namespace

bool assemble_line(std::string_view line, Encoding& out, std::string& error) {
    line = trim(line);
    if (line.empty()) {
        error = "kr216: empty line";
        return false;
    }
    const std::size_t space = line.find_first_of(" \t");
    std::string mnemonic(line.substr(0, space));
    for (char& c : mnemonic) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    const std::string_view operands = space == std::string_view::npos ? std::string_view{}
                                                                     : trim(line.substr(space));

    if (operands.empty()) {
        static const struct {
            const char* name;
            std::uint8_t opcode;
        } kSystem[] = {{"ret", 0x70}, {"nop", 0x71}, {"brk", 0x72}, {"sys", 0x73},
                       {"ei", 0x74},  {"di", 0x75},  {"halt", 0x76}, {"iret", 0x77},
                       {"rdcycle", 0x78}};
        for (const auto& entry : kSystem) {
            if (mnemonic == entry.name) return encode_system(entry.opcode, out, error);
        }
        error = "kr216: unknown mnemonic '" + mnemonic + "'";
        return false;
    }

    const std::size_t comma = operands.find(',');
    const std::string_view first =
        trim(comma == std::string_view::npos ? operands : operands.substr(0, comma));
    const std::string_view second =
        comma == std::string_view::npos ? std::string_view{} : trim(operands.substr(comma + 1));

    std::uint8_t rd = 0;
    if (mnemonic == "push" || mnemonic == "pop" || mnemonic == "inc" || mnemonic == "dec") {
        if (!parse_register(first, rd)) {
            error = "kr216: " + mnemonic + " needs a register";
            return false;
        }
        if (mnemonic == "push") return encode_push_register(rd, out, error);
        if (mnemonic == "pop") return encode_pop_register(rd, out, error);
        if (mnemonic == "inc") return encode_inc_register(rd, out, error);
        return encode_dec_register(rd, out, error);
    }
    if (mnemonic == "mov") {
        if (!parse_register(first, rd)) {
            error = "kr216: mov needs a destination register";
            return false;
        }
        std::uint8_t rs = 0;
        if (!parse_register(second, rs)) {
            error = "kr216: mov needs a source register";
            return false;
        }
        return encode_compact(0, rd, rs, out, error);
    }
    if (mnemonic == "ld") {
        if (!parse_register(first, rd)) {
            error = "kr216: ld needs a destination register";
            return false;
        }
        if (second.size() > 2 && second.front() == '[' && second.back() == ']' && second.find("+") != std::string_view::npos) {
            std::uint8_t base = 0;
            std::int32_t displacement = 0;
            if (!parse_bracketed_displacement(second, base, displacement)) {
                error = "kr216: ld address is not [rB+d8]";
                return false;
            }
            return encode_normal_ld_d8(rd, base, displacement, out, error);
        }
        const auto indirect = [&second](std::string_view text, std::uint8_t& base) {
            if (text.size() < 3 || text.front() != '[' || text.back() != ']') return false;
            return parse_register(trim(text.substr(1, text.size() - 2)), base);
        };
        std::uint8_t base = 0;
        if (second.size() > 3 && second.back() == '+' &&
            indirect(second.substr(0, second.size() - 1), base)) {
            return encode_compact(19, rd, base, out, error);  // LD Rd, [Rb]+
        }
        if (second.size() > 3 && second.front() == '-' &&
            indirect(second.substr(1), base)) {
            return encode_compact(24, rd, base, out, error);  // LD Rd, -[Rb]
        }
        if (indirect(second, base)) return encode_compact(14, rd, base, out, error);  // LD Rd,[Rb]
        const bool bracketed = second.size() > 2 && second.front() == '[' && second.back() == ']';
        std::string_view number =
            bracketed ? trim(second.substr(1, second.size() - 2)) : second;
        if (!bracketed && !number.empty() && number.front() == '#') number.remove_prefix(1);
        std::uint16_t value = 0;
        if (!parse_number(number, value)) {
            error = "kr216: ld needs an immediate or an address";
            return false;
        }
        if (bracketed) return encode_ld_absolute(rd, value, out, error);
        return encode_ld_immediate(rd, value, out, error);
    }
    static const struct {
        const char* name;
        std::uint8_t opcode;
    } kCompactAlu[] = {{"add", 1}, {"sub", 3}, {"and", 5}, {"or", 6}, {"xor", 7}, {"cmp", 8}};
    for (const auto& entry : kCompactAlu) {
        if (mnemonic != entry.name) continue;
        if (!parse_register(first, rd)) {
            error = "kr216: " + mnemonic + " needs a destination register";
            return false;
        }
        if (!second.empty() && second.front() == '#') {
            std::uint16_t value = 0;
            if (!parse_number(trim(second.substr(1)), value)) {
                error = "kr216: " + mnemonic + " immediate is not a number";
                return false;
            }
            if (mnemonic == "add") return encode_normal_add_imm8(rd, value, out, error);
            if (mnemonic == "cmp") return encode_normal_cmp_imm8(rd, value, out, error);
            error = "kr216: " + mnemonic + " has no immediate form yet";
            return false;
        }
        std::uint8_t rs = 0;
        if (!parse_register(second, rs)) {
            error = "kr216: " + mnemonic + " needs a source register";
            return false;
        }
        return encode_compact(entry.opcode, rd, rs, out, error);
    }
    if (mnemonic == "st") {
        if (first.size() > 2 && first.front() == '[' && first.back() == ']' && first.find("+") != std::string_view::npos) {
            std::uint8_t base = 0;
            std::int32_t displacement = 0;
            if (!parse_bracketed_displacement(first, base, displacement)) {
                error = "kr216: st address is not [rB+d8]";
                return false;
            }
            std::uint8_t rs = 0;
            if (!parse_register(second, rs)) {
                error = "kr216: st needs a source register";
                return false;
            }
            return encode_normal_st_d8(rs, base, displacement, out, error);
        }
        const auto store_data = [&](std::uint8_t& rs) {
            return parse_register(second, rs);
        };
        const auto store_indirect = [&first](std::string_view text, std::uint8_t& base) {
            if (text.size() < 3 || text.front() != '[' || text.back() != ']') return false;
            return parse_register(trim(text.substr(1, text.size() - 2)), base);
        };
        std::uint8_t store_base = 0;
        std::uint8_t store_rs = 0;
        if (first.size() > 3 && first.back() == '+' &&
            store_indirect(first.substr(0, first.size() - 1), store_base) && store_data(store_rs)) {
            return encode_compact(20, store_rs, store_base, out, error);  // ST [Rb]+, Rs
        }
        if (first.size() > 3 && first.front() == '-' && store_indirect(first.substr(1), store_base) &&
            store_data(store_rs)) {
            return encode_compact(25, store_rs, store_base, out, error);  // ST -[Rb], Rs
        }
        if (store_indirect(first, store_base) && store_data(store_rs)) {
            return encode_compact(15, store_rs, store_base, out, error);  // ST [Rb], Rs
        }
        const bool bracketed = first.size() > 2 && first.front() == '[' && first.back() == ']';
        if (!bracketed) {
            error = "kr216: st needs a bracketed address";
            return false;
        }
        std::uint16_t address = 0;
        if (!parse_number(trim(first.substr(1, first.size() - 2)), address)) {
            error = "kr216: st address is not a number";
            return false;
        }
        std::uint8_t rs = 0;
        if (!parse_register(second, rs)) {
            error = "kr216: st needs a source register";
            return false;
        }
        return encode_st_absolute(rs, address, out, error);
    }
    if (mnemonic == "jmp" || mnemonic == "call") {
        std::uint16_t address = 0;
        if (!parse_number(first, address)) {
            error = "kr216: " + mnemonic + " needs an address";
            return false;
        }
        if (mnemonic == "jmp") return encode_normal_jmp_absolute(address, out);
        return encode_normal_call_absolute(address, out);
    }
    error = "kr216: unknown mnemonic '" + mnemonic + "'";
    return false;
}

}  // namespace krc::asmkr216
