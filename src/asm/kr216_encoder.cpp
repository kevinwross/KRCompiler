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

}  // namespace krc::asmkr216
