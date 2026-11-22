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

bool encode_normal_byte_d8(std::uint8_t opcode, std::uint8_t rd, std::uint8_t base,
                           std::int32_t displacement, Encoding& out, std::string& error) {
    if (!check_register(rd, error) || !check_register(base, error)) return false;
    if (displacement < -128 || displacement > 127) {
        error = "kr216 disp8 out of range";
        return false;
    }
    out.bytes[0] = static_cast<std::uint8_t>(0xC0 | opcode);
    out.bytes[1] = static_cast<std::uint8_t>((rd << 4) | base);
    out.bytes[2] = static_cast<std::uint8_t>(displacement & 0xFF);
    out.size = 3;
    return true;
}

bool encode_normal_ldbu_d8(std::uint8_t rd, std::uint8_t base, std::int32_t displacement,
                           Encoding& out, std::string& error) {
    return encode_normal_byte_d8(2, rd, base, displacement, out, error);
}

bool encode_normal_ldbs_d8(std::uint8_t rd, std::uint8_t base, std::int32_t displacement,
                           Encoding& out, std::string& error) {
    return encode_normal_byte_d8(3, rd, base, displacement, out, error);
}

bool encode_normal_stb_d8(std::uint8_t rs, std::uint8_t base, std::int32_t displacement,
                          Encoding& out, std::string& error) {
    return encode_normal_byte_d8(4, rs, base, displacement, out, error);
}

bool encode_normal_div(std::uint8_t opcode, std::uint8_t rd, std::uint8_t rs, Encoding& out,
                       std::string& error) {
    if (opcode > 1) {
        error = "kr216 division opcode must be 0 (DIV) or 1 (DIVS)";
        return false;
    }
    if (!check_register(rd, error) || !check_register(rs, error)) return false;
    out.bytes[0] = static_cast<std::uint8_t>(0xD9 + opcode);
    out.bytes[1] = static_cast<std::uint8_t>((rd << 4) | rs);
    out.size = 2;
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
    if ((mask & 0xE000) != 0) {
        error = "kr216 LEAVE mask must have bits 13-15 clear";
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
    if (count > 15) {
        error = "kr216 shift count out of range (0-15)";
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
    const bool unary = (opcode >= 4 && opcode <= 6) || opcode == 9;  // FNEG FABS FSQRT FRND
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
    if ((mask & 0xE000) != 0) {
        error = "kr216 ENTER mask must have bits 13-15 clear";
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
    if ((mask & 0xE000) != 0) {
        error = "kr216 ENTER mask must have bits 13-15 clear";
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
    if (text == "sp" || text == "SP") {
        out = 15;
        return true;
    }
    if (text == "fp" || text == "FP") {
        out = 13;
        return true;
    }
    if (text == "pc" || text == "PC") {
        out = 14;
        return true;
    }
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

bool parse_bracketed_register(std::string_view text, std::uint8_t& base) {
    if (text.size() < 3 || text.front() != '[' || text.back() != ']') return false;
    return parse_register(text.substr(1, text.size() - 2), base);
}

bool parse_fp_register(std::string_view text, std::uint8_t& out) {
    if (text.size() != 2 || (text[0] != 'f' && text[0] != 'F')) return false;
    if (text[1] < '0' || text[1] > '3') return false;
    out = static_cast<std::uint8_t>(text[1] - '0');
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

bool unknown_mnemonic(std::string_view mnemonic, std::string& error) {
    error = "kr216: unknown mnemonic '" + std::string(mnemonic) + "'";
    return false;
}

// A label is a name the multi-line layer records before a `:`; the first character cannot be a
// digit or a sign, so an absolute operand that starts that way is refused as malformed rather
// than accepted as a label.
bool is_label_name(std::string_view text) {
    if (text.empty()) return false;
    const auto first = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '.';
    };
    const auto rest = [&first](char c) {
        return first(c) || (c >= '0' && c <= '9');
    };
    if (!first(text.front())) return false;
    for (const char c : text.substr(1)) {
        if (!rest(c)) return false;
    }
    return true;
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
    out = Encoding{};
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
        return unknown_mnemonic(mnemonic, error);
    }

    const std::size_t comma = operands.find(',');
    const std::string_view first =
        trim(comma == std::string_view::npos ? operands : operands.substr(0, comma));
    const std::string_view second =
        comma == std::string_view::npos ? std::string_view{} : trim(operands.substr(comma + 1));

    std::uint8_t rd = 0;
    if ((mnemonic == "push" || mnemonic == "pop") && !first.empty() && first.front() == '#') {
        std::uint16_t mask = 0;
        if (!parse_number(trim(first.substr(1)), mask)) {
            error = "kr216: " + mnemonic + " mask is not a number";
            return false;
        }
        if (mnemonic == "push") return encode_normal_push_mask(mask, out, error);
        return encode_normal_pop_mask(mask, out, error);
    }
    if (mnemonic == "neg" || mnemonic == "not") {
        if (!parse_register(first, rd)) {
            error = "kr216: " + mnemonic + " needs a register";
            return false;
        }
        return encode_compact(mnemonic == "neg" ? 29 : 30, rd, 0, out, error);
    }
    if (mnemonic == "shl" || mnemonic == "shr" || mnemonic == "sar") {
        if (!parse_register(first, rd)) {
            error = "kr216: " + mnemonic + " needs a register";
            return false;
        }
        const std::uint8_t kind = mnemonic == "shl" ? 0 : (mnemonic == "shr" ? 1 : 2);
        if (second.empty()) {
            error = "kr216: " + mnemonic + " needs a count";
            return false;
        }
        if (second.front() != '#') {
            std::uint8_t rs = 0;
            if (!parse_register(second, rs)) {
                error = "kr216: " + mnemonic + " needs a register or an immediate count";
                return false;
            }
            return encode_compact(static_cast<std::uint8_t>(11 + kind), rd, rs, out, error);
        }
        std::uint16_t count = 0;
        if (!parse_number(trim(second.substr(1)), count)) {
            error = "kr216: shift count is not a number";
            return false;
        }
        return encode_normal_shift_imm(kind, rd, static_cast<std::uint8_t>(count), out, error);
    }
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
            if (!bracketed && is_label_name(number)) {
                out.label = std::string(number);
                return encode_ld_immediate(rd, 0, out, error);
            }
            error = "kr216: ld needs an immediate or an address";
            return false;
        }
        if (bracketed) return encode_ld_absolute(rd, value, out, error);
        return encode_ld_immediate(rd, value, out, error);
    }
    static const struct {
        const char* name;
        std::uint8_t opcode;
    } kCompactAlu[] = {{"add", 1},  {"addc", 2}, {"sub", 3},  {"subb", 4},
                       {"and", 5},  {"or", 6},   {"xor", 7},  {"cmp", 8},
                       {"mul", 9},  {"mulh", 10}};
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
    if (mnemonic == "div" || mnemonic == "divs") {
        std::uint8_t rs = 0;
        if (!parse_register(first, rd) || !parse_register(second, rs)) {
            error = "kr216: " + mnemonic + " needs two registers";
            return false;
        }
        return encode_normal_div(mnemonic == "div" ? 0 : 1, rd, rs, out, error);
    }
    if (mnemonic == "ldbu" || mnemonic == "ldbs") {
        if (!parse_register(first, rd)) {
            error = "kr216: " + mnemonic + " needs a destination register";
            return false;
        }
        std::uint8_t base = 0;
        std::int32_t displacement = 0;
        if (parse_bracketed_displacement(second, base, displacement)) {
            return mnemonic == "ldbu"
                       ? encode_normal_ldbu_d8(rd, base, displacement, out, error)
                       : encode_normal_ldbs_d8(rd, base, displacement, out, error);
        }
        if (parse_bracketed_register(second, base)) {
            return encode_compact(mnemonic == "ldbu" ? 16 : 17, rd, base, out, error);
        }
        error = "kr216: " + mnemonic + " needs an [rB] or [rB+d8] address";
        return false;
    }
    if (mnemonic == "stb") {
        std::uint8_t base = 0;
        std::uint8_t rs = 0;
        std::int32_t displacement = 0;
        if (parse_bracketed_displacement(first, base, displacement)) {
            if (!parse_register(second, rs)) {
                error = "kr216: stb needs a source register";
                return false;
            }
            return encode_normal_stb_d8(rs, base, displacement, out, error);
        }
        if (parse_bracketed_register(first, base)) {
            if (!parse_register(second, rs)) {
                error = "kr216: stb needs a source register";
                return false;
            }
            return encode_compact(18, rs, base, out, error);
        }
        error = "kr216: stb needs an [rB] or [rB+d8] address";
        return false;
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
    if (mnemonic == "enter") {
        if (first.empty() || first.front() != '#' || second.empty() || second.front() != '#') {
            error = "kr216: enter needs #frame, #mask";
            return false;
        }
        std::uint16_t frame = 0;
        std::uint16_t mask = 0;
        if (!parse_number(trim(first.substr(1)), frame) ||
            !parse_number(trim(second.substr(1)), mask)) {
            error = "kr216: enter operands are not numbers";
            return false;
        }
        if (frame <= 254) return encode_f7_enter(frame, mask, out, error);
        return encode_f8_enter(frame, mask, out, error);
    }
    if (mnemonic == "leave") {
        if (first.empty() || first.front() != '#') {
            error = "kr216: leave needs #mask";
            return false;
        }
        std::uint16_t mask = 0;
        if (!parse_number(trim(first.substr(1)), mask)) {
            error = "kr216: leave mask is not a number";
            return false;
        }
        return encode_normal_leave(mask, out, error);
    }
    static const struct {
        const char* name;
        std::uint8_t opcode;
    } kFpBinary[] = {{"fadd", 0}, {"fsub", 1}, {"fmul", 2}, {"fdiv", 3}, {"fmov", 7}, {"fcmp", 8}};
    for (const auto& entry : kFpBinary) {
        if (mnemonic != entry.name) continue;
        std::uint8_t fd = 0;
        std::uint8_t fs = 0;
        if (!parse_fp_register(first, fd) || !parse_fp_register(second, fs)) {
            error = "kr216: " + mnemonic + " needs two FP registers";
            return false;
        }
        return encode_f3(entry.opcode, fd, fs, out, error);
    }
    static const struct {
        const char* name;
        std::uint8_t opcode;
    } kFpUnary[] = {{"fneg", 4}, {"fabs", 5}, {"fsqrt", 6}, {"frnd", 9}};
    for (const auto& entry : kFpUnary) {
        if (mnemonic != entry.name) continue;
        std::uint8_t fd = 0;
        if (!parse_fp_register(first, fd)) {
            error = "kr216: " + mnemonic + " needs an FP register";
            return false;
        }
        return encode_f3(entry.opcode, fd, 0, out, error);
    }
    if (mnemonic == "fpush" || mnemonic == "fpop") {
        std::uint8_t fd = 0;
        if (!parse_fp_register(first, fd)) {
            error = "kr216: " + mnemonic + " needs an FP register";
            return false;
        }
        return encode_f6(mnemonic == "fpush" ? 0 : 1, fd, out, error);
    }
    static const struct {
        const char* name;
        std::uint8_t opcode;
    } kFpMemory[] = {{"fld", 0}, {"fst", 1}, {"flds", 2}, {"fsts", 3}};
    for (const auto& entry : kFpMemory) {
        if (mnemonic != entry.name) continue;
        std::uint8_t fd = 0;
        if (!parse_fp_register(first, fd)) {
            error = "kr216: " + mnemonic + " needs an FP register";
            return false;
        }
        std::uint8_t base = 0;
        std::int32_t displacement = 0;
        std::uint8_t mode = 0;
        if (second.size() > 3 && second.back() == '+' &&
            parse_bracketed_register(second.substr(0, second.size() - 1), base)) {
            mode = 2;  // [Rb]+
        } else if (second.size() > 3 && second.front() == '-' &&
                   parse_bracketed_register(second.substr(1), base)) {
            mode = 3;  // -[Rb]
        } else if (parse_bracketed_displacement(second, base, displacement)) {
            mode = 1;  // [Rb+d8]
        } else if (parse_bracketed_register(second, base)) {
            mode = 0;  // [Rb]
        } else {
            error = "kr216: " + mnemonic + " needs an [rB] operand";
            return false;
        }
        return encode_f4(entry.opcode, fd, mode, base, displacement, out, error);
    }
    if (mnemonic == "i2f" || mnemonic == "u2f" || mnemonic == "f2i" || mnemonic == "f2u") {
        std::uint8_t fd = 0;
        std::uint8_t reg = 0;
        if (mnemonic == "i2f" || mnemonic == "u2f") {
            if (!parse_fp_register(first, fd) || !parse_register(second, reg)) {
                error = "kr216: " + mnemonic + " needs an FP register and an integer register";
                return false;
            }
        } else {
            if (!parse_register(first, reg) || !parse_fp_register(second, fd)) {
                error = "kr216: " + mnemonic + " needs an integer register and an FP register";
                return false;
            }
        }
        const std::uint8_t opcode = mnemonic == "i2f" ? 0 : (mnemonic == "u2f" ? 1 : (mnemonic == "f2i" ? 2 : 3));
        return encode_f5(opcode, fd, reg, out, error);
    }
    static const struct {
        const char* name;
        std::uint8_t condition;
    } kBranches[] = {{"beq", 0},  {"bne", 1},  {"bcs", 2},  {"bcc", 3},  {"bmi", 4},
                     {"bpl", 5},  {"bvs", 6},  {"bvc", 7},  {"bhi", 8},  {"bls", 9},
                     {"bge", 10}, {"blt", 11}, {"bgt", 12}, {"ble", 13}, {"bal", 14},
                     {"bra", 15}};
    for (const auto& entry : kBranches) {
        if (mnemonic != entry.name) continue;
        if (first.empty()) {
            error = "kr216: " + mnemonic + " needs a displacement";
            return false;
        }
        int sign = 1;
        std::string_view digits = first;
        if (digits.front() == '+') {
            digits.remove_prefix(1);
        } else if (digits.front() == '-') {
            sign = -1;
            digits.remove_prefix(1);
        }
        std::uint16_t magnitude = 0;
        if (!parse_number(digits, magnitude)) {
            if (sign == 1 && is_label_name(first)) {
                out.label = std::string(first);
                out.relative = true;
                out.condition = entry.condition;
                return encode_branch(entry.condition, 0, out, error);
            }
            error = "kr216: " + mnemonic + " displacement is not a number";
            return false;
        }
        return encode_branch(entry.condition, sign * static_cast<std::int32_t>(magnitude), out,
                             error);
    }
    if (mnemonic == "jmp" || mnemonic == "call") {
        const bool is_jump = mnemonic == "jmp";
        std::uint8_t target = 0;
        if (parse_register(first, target)) {
            return is_jump ? encode_compact(31, target, 0, out, error)
                           : encode_call_register(target, out, error);
        }
        if (!first.empty() && first.front() == '#') {
            error = "kr216: " + mnemonic +
                    " operand is malformed: '#' introduces an immediate, not a label";
            return false;
        }
        std::uint16_t address = 0;
        if (parse_number(first, address)) {
            return is_jump ? encode_normal_jmp_absolute(address, out)
                           : encode_normal_call_absolute(address, out);
        }
        if (!is_label_name(first)) {
            error = "kr216: " + mnemonic + " needs an address or a label";
            return false;
        }
        out.label = std::string(first);
        return is_jump ? encode_normal_jmp_absolute(0, out)
                       : encode_normal_call_absolute(0, out);
    }
    return unknown_mnemonic(mnemonic, error);
}

namespace {

std::vector<std::string_view> split_lines(std::string_view source) {
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start <= source.size()) {
        const std::size_t end = source.find('\n', start);
        const std::size_t stop = end == std::string_view::npos ? source.size() : end;
        lines.push_back(source.substr(start, stop - start));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return lines;
}

// Splits `a, b, c` into trimmed pieces; an empty input yields no pieces.
std::vector<std::string_view> split_commas(std::string_view text) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t comma = text.find(',', start);
        const std::size_t stop = comma == std::string_view::npos ? text.size() : comma;
        const auto part = trim(text.substr(start, stop - start));
        if (!part.empty()) parts.push_back(part);
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return parts;
}

}  // namespace

bool assemble_text(std::string_view source, AssembledProgram& out, std::string& error) {
    out.bytes.clear();
    out.labels.clear();
    out.globals.clear();
    out.externals.clear();
    out.relocations.clear();
    struct Fixup {
        std::size_t offset = 0;
        std::string label;
        std::uint8_t width = 2;
        bool relative = false;
        std::uint8_t condition = 0;
    };
    std::vector<Fixup> fixups;
    for (const std::string_view raw : split_lines(source)) {
        std::string_view line = raw;
        const std::size_t comment = line.find(';');
        if (comment != std::string_view::npos) line = line.substr(0, comment);
        line = trim(line);
        if (line.empty()) continue;

        if (line.back() == ':') {
            const auto name = trim(line.substr(0, line.size() - 1));
            if (name.empty()) {
                error = "kr216: empty label";
                return false;
            }
            for (const auto& existing : out.labels) {
                if (existing.first == name) {
                    error = "kr216: duplicate label '" + std::string(name) + "'";
                    return false;
                }
            }
            out.labels.emplace_back(std::string(name), static_cast<std::uint16_t>(out.bytes.size()));
            continue;
        }

        if (line.front() == '.') {
            const std::size_t space = line.find_first_of(" \t");
            const std::string directive(line.substr(0, space));
            const std::string_view operands =
                space == std::string_view::npos ? std::string_view{} : trim(line.substr(space));
            if (directive == ".text") {
                if (!operands.empty()) {
                    error = "kr216: .text takes no operands";
                    return false;
                }
                continue;
            }
            if (directive == ".global" || directive == ".globl") {
                if (operands.empty()) {
                    error = "kr216: " + directive + " needs a label name";
                    return false;
                }
                for (const std::string_view name : split_commas(operands)) {
                    if (!is_label_name(name)) {
                        error = "kr216: " + directive + " name '" + std::string(name) +
                                "' is not a label";
                        return false;
                    }
                    bool duplicate = false;
                    for (const auto& existing : out.globals) {
                        if (existing == name) {
                            duplicate = true;
                            break;
                        }
                    }
                    if (!duplicate) out.globals.emplace_back(name);
                }
                continue;
            }
            if (directive == ".extern") {
                if (operands.empty()) {
                    error = "kr216: .extern needs a label name";
                    return false;
                }
                for (const std::string_view name : split_commas(operands)) {
                    if (!is_label_name(name)) {
                        error = "kr216: .extern name '" + std::string(name) +
                                "' is not a label";
                        return false;
                    }
                    bool duplicate = false;
                    for (const auto& existing : out.externals) {
                        if (existing == name) {
                            duplicate = true;
                            break;
                        }
                    }
                    if (!duplicate) out.externals.emplace_back(name);
                }
                continue;
            }
            const std::size_t width = directive == ".byte" ? 1 : (directive == ".word" ? 2 : 0);
            if (width == 0) {
                error = "kr216: unknown directive '" + directive + "'";
                return false;
            }
            for (const std::string_view value : split_commas(operands)) {
                std::uint16_t number = 0;
                if (parse_number(value, number)) {
                    out.bytes.push_back(static_cast<std::uint8_t>(number & 0xFF));
                    if (width == 2) out.bytes.push_back(static_cast<std::uint8_t>(number >> 8));
                    continue;
                }
                if (width == 2 && is_label_name(value)) {
                    fixups.push_back({out.bytes.size(), std::string(value), 2});
                    out.bytes.push_back(0);
                    out.bytes.push_back(0);
                    continue;
                }
                error = "kr216: " + directive + " value is not a number";
                return false;
            }
            continue;
        }

        Encoding encoding;
        if (!assemble_line(line, encoding, error)) return false;
        if (!encoding.label.empty()) {
            if (encoding.relative) {
                fixups.push_back({out.bytes.size(), std::move(encoding.label), 2, true,
                                  encoding.condition});
            } else {
                fixups.push_back({out.bytes.size() + 1, std::move(encoding.label), 2, false, 0});
            }
        }
        out.bytes.insert(out.bytes.end(), encoding.bytes, encoding.bytes + encoding.size);
    }

    // The address field was emitted as zero. Patch it only after every label has been recorded,
    // so a forward reference resolves; a name declared `.extern` becomes a relocation and any
    // other missing label is an error rather than a silent address zero.
    for (const auto& fixup : fixups) {
        bool found = false;
        std::uint16_t address = 0;
        for (const auto& label : out.labels) {
            if (label.first == fixup.label) {
                address = label.second;
                found = true;
                break;
            }
        }
        if (fixup.relative) {
            if (!found) {
                error = "kr216: undefined label '" + fixup.label + "'";
                return false;
            }
            const auto displacement =
                static_cast<std::int32_t>(address) -
                static_cast<std::int32_t>(fixup.offset + 2);
            Encoding branch;
            std::string branch_error;
            if (!encode_branch(fixup.condition, displacement, branch, branch_error)) {
                error = "kr216: branch to '" + fixup.label + "': " + branch_error;
                return false;
            }
            out.bytes[fixup.offset] = branch.bytes[0];
            out.bytes[fixup.offset + 1] = branch.bytes[1];
            continue;
        }
        if (found) {
            // The section-relative value is useful to `assemble_text`'s callers, but the
            // linker still has to add the section's final base. Keep both: the checked
            // bytes for a standalone listing, and a relocation for the linked image.
            out.bytes[fixup.offset] = static_cast<std::uint8_t>(address & 0xFF);
            if (fixup.width == 2) {
                out.bytes[fixup.offset + 1] = static_cast<std::uint8_t>(address >> 8);
            }
            out.relocations.push_back({static_cast<std::uint32_t>(fixup.offset),
                                       fixup.label, fixup.width});
            continue;
        }
        bool external = false;
        for (const auto& name : out.externals) {
            if (name == fixup.label) {
                external = true;
                break;
            }
        }
        if (external) {
            out.relocations.push_back({static_cast<std::uint32_t>(fixup.offset),
                                       fixup.label, fixup.width});
            continue;
        }
        error = "kr216: undefined label '" + fixup.label + "'";
        return false;
    }
    return true;
}

}  // namespace krc::asmkr216
