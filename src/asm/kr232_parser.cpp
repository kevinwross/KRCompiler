#include "kr232_parser.h"

#include <array>
#include <cctype>
#include <cstdlib>
#include <vector>

namespace krc::asm_kr232 {
namespace {

std::string lower_and_trim(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    bool started = false;
    for (const char ch : text) {
        if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
            if (started) {
                out.push_back(' ');
            }
            continue;
        }
        started = true;
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

std::vector<std::string> split_operands(const std::string& text) {
    std::vector<std::string> operands;
    std::string current;
    for (const char ch : text) {
        if (ch == ',') {
            operands.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    if (!current.empty()) {
        operands.push_back(current);
    }
    for (std::string& operand : operands) {
        while (!operand.empty() && operand.front() == ' ') {
            operand.erase(operand.begin());
        }
        while (!operand.empty() && operand.back() == ' ') {
            operand.pop_back();
        }
    }
    return operands;
}

bool parse_register(const std::string& text, unsigned& reg) {
    if (text.size() < 2 || text[0] != 'r') {
        return false;
    }
    char* end = nullptr;
    const unsigned long value = std::strtoul(text.c_str() + 1, &end, 10);
    if (end == nullptr || *end != '\0' || value > 15) {
        return false;
    }
    reg = static_cast<unsigned>(value);
    return true;
}

bool parse_number(const std::string& text, std::int32_t& value) {
    std::string digits = text;
    bool negative = false;
    if (!digits.empty() && digits.front() == '#') {
        digits.erase(digits.begin());
    }
    if (!digits.empty() && (digits.front() == '+' || digits.front() == '-')) {
        negative = digits.front() == '-';
        digits.erase(digits.begin());
    }
    if (digits.empty()) {
        return false;
    }
    char* end = nullptr;
    const unsigned long magnitude = std::strtoul(digits.c_str(), &end, 0);
    if (end == nullptr || *end != '\0') {
        return false;
    }
    const auto wide = static_cast<std::int64_t>(magnitude);
    value = static_cast<std::int32_t>(negative ? -wide : wide);
    return true;
}

struct Address {
    bool pc = false;
    bool sp = false;
    bool absolute = false;
    bool post = false;
    bool pre = false;
    unsigned base = 0;
    std::int32_t disp = 0;
    std::uint32_t value = 0;
};

// `[r3]`, `[r3+8]`, `[r3-8]`, `[r3]+`, `-[r3]`, `[pc+0x40]`, `[sp+8]`, `[0x1234]`.
bool parse_address(const std::string& text, Address& address) {
    std::string body = text;
    if (body.size() >= 2 && body.front() == '-' && body[1] == '[') {
        address.pre = true;
        body.erase(body.begin());
    }
    // A trailing '+' marks a post-increment and comes outside the brackets.
    if (body.size() >= 2 && body.back() == '+') {
        address.post = true;
        body.pop_back();
    }
    if (body.size() < 3 || body.front() != '[' || body.back() != ']') {
        return false;
    }
    body = body.substr(1, body.size() - 2);
    if (body.rfind("pc", 0) == 0) {
        address.pc = true;
        body.erase(0, 2);
    } else if (body.rfind("sp", 0) == 0) {
        address.sp = true;
        body.erase(0, 2);
    } else if (body.rfind("r", 0) == 0) {
        const std::size_t split = body.find_first_of("+-");
        const std::string reg_text = split == std::string::npos ? body : body.substr(0, split);
        if (!parse_register(reg_text, address.base)) {
            return false;
        }
        body = split == std::string::npos ? std::string() : body.substr(split);
    } else {
        address.absolute = true;
        std::int32_t value = 0;
        if (!parse_number(body, value)) {
            return false;
        }
        address.value = static_cast<std::uint32_t>(value);
        return true;
    }
    if (!body.empty() && !parse_number(body, address.disp)) {
        return false;
    }
    return true;
}

struct Condition {
    const char* name;
    unsigned code;
};

// Mnemonics of the compact register form (ISA §6.1), excluding the load/store
// and branch forms, which need their own operand shapes.
struct CompactOp {
    const char* name;
    unsigned op;
};

constexpr CompactOp kCompactOps[] = {
    {"mov", 0},  {"addc", 2}, {"sub", 3},  {"subb", 4}, {"and", 5},  {"or", 6},  {"xor", 7},
    {"cmp", 8},  {"mul", 9},  {"mulh", 10}, {"shl", 11}, {"shr", 12}, {"sar", 13},
};

}  // namespace

std::optional<EncodedInstruction> parse_instruction(std::string_view text, std::string& error) {
    const std::string line = lower_and_trim(text);
    if (line.empty()) {
        error = "empty instruction";
        return std::nullopt;
    }
    const std::size_t space = line.find(' ');
    const std::string mnemonic = line.substr(0, space);
    const std::vector<std::string> operands =
        space == std::string::npos ? std::vector<std::string>{}
                                   : split_operands(line.substr(space + 1));

    // Zero-operand Short forms.
    if (operands.empty()) {
        struct NoOperand {
            const char* name;
            ShortOp op;
        };
        constexpr NoOperand kNoOperand[] = {
            {"ret", ShortOp::Ret},     {"nop", ShortOp::Nop},     {"brk", ShortOp::Brk},
            {"sys", ShortOp::Sys},     {"ei", ShortOp::Ei},       {"di", ShortOp::Di},
            {"halt", ShortOp::Halt},   {"iret", ShortOp::Iret},   {"rdcycle", ShortOp::RdCycle},
            {"syscall", ShortOp::SysCall},
        };
        for (const NoOperand& entry : kNoOperand) {
            if (mnemonic == entry.name) {
                return encode_short(entry.op, 0, error);
            }
        }
    }

    // Short register forms: push/pop/inc/dec/call with a single register.
    if (operands.size() == 1) {
        unsigned reg = 0;
        struct ShortOpName {
            const char* name;
            ShortOp op;
        };
        constexpr ShortOpName kShort[] = {
            {"push", ShortOp::Push}, {"pop", ShortOp::Pop}, {"inc", ShortOp::Inc},
            {"dec", ShortOp::Dec},   {"call", ShortOp::Call},
        };
        if (parse_register(operands[0], reg)) {
            for (const ShortOpName& entry : kShort) {
                if (mnemonic == entry.name) {
                    return encode_short(entry.op, reg, error);
                }
            }
            if (mnemonic == "neg" || mnemonic == "not" || mnemonic == "sxtb" ||
                mnemonic == "sxth") {
                const unsigned op = mnemonic == "neg" ? 29 : (mnemonic == "not" ? 30 : 27);
                if (mnemonic == "sxth") {
                    return encode_generic(0x1B, {0, static_cast<std::uint8_t>(reg), 0}, {0, 0, 0},
                                          error);
                }
                return encode_compact_register(op, reg, 0, error);
            }
        }
        std::int32_t mask = 0;
        if ((mnemonic == "push" || mnemonic == "pop" || mnemonic == "leave") &&
            parse_number(operands[0], mask)) {
            const unsigned op = mnemonic == "push" ? 23 : (mnemonic == "pop" ? 24 : 28);
            return encode_normal(op, 0, 0, mask, error);
        }
    }

    // Compact register form with two registers.
    if (operands.size() == 2) {
        unsigned ra = 0;
        unsigned rb = 0;
        if (parse_register(operands[0], ra) && parse_register(operands[1], rb)) {
            for (const CompactOp& entry : kCompactOps) {
                if (mnemonic == entry.name) {
                    return encode_compact_register(entry.op, ra, rb, error);
                }
            }
            if (mnemonic == "add") {
                return encode_compact_register(1, ra, rb, error);
            }
            if (mnemonic == "ld") {
                return encode_compact_register(14, ra, rb, error);
            }
            if (mnemonic == "mov") {
                return encode_compact_register(0, ra, rb, error);
            }
        }
    }

    // Register with a bracketed address, or a store whose address comes first.
    if (operands.size() == 2) {
        struct LoadStore {
            const char* name;
            unsigned normal_op;
            unsigned compact_op;
            unsigned long_op;
        };
        constexpr LoadStore kLoadStores[] = {
            {"ld", 0, 14, 0xFF},   {"ldbu", 2, 16, 0},  {"ldbs", 3, 17, 1},
            {"ldhu", 29, 255, 5},  {"ldhs", 31, 255, 7}, {"st", 1, 15, 0xFF},
            {"stb", 4, 18, 2},     {"sth", 30, 255, 6},
        };
        unsigned rd = 0;
        Address address;
        const bool register_first = parse_register(operands[0], rd);
        const bool address_second = register_first && parse_address(operands[1], address);
        if (address_second) {
            for (const LoadStore& entry : kLoadStores) {
                if (mnemonic != entry.name) {
                    continue;
                }
                if (address.absolute) {
                    if (entry.long_op == 0xFF) {
                        return encode_ld_absolute(rd, address.value, error);
                    }
                    return encode_long_integer(entry.long_op, rd,
                                               static_cast<std::int32_t>(address.value), error);
                }
                if (address.pc) {
                    if (mnemonic != "ldbu" && mnemonic != "ldbs") {
                        error = "only the byte loads have a PC-relative normal form";
                        return std::nullopt;
                    }
                    return encode_normal(mnemonic == "ldbu" ? 17 : 18, rd, 0, address.disp, error);
                }
                if (address.post || address.pre) {
                    unsigned op = entry.compact_op;
                    op += address.post ? 5u : 10u;
                    return encode_compact_register(op, rd, address.base, error);
                }
                // `[Rb]` with no displacement is the two-byte compact form when
                // that form exists; the normal form is three bytes.
                if (address.disp == 0 && entry.compact_op != 255) {
                    return encode_compact_register(entry.compact_op, rd, address.base, error);
                }
                return encode_normal(entry.normal_op, rd, address.base, address.disp, error);
            }
            if (mnemonic == "lea") {
                if (address.absolute) {
                    return encode_long_integer(3, rd, static_cast<std::int32_t>(address.value),
                                               error);
                }
                if (address.sp || address.pc) {
                    return encode_long_integer(address.sp ? 4 : 3, rd, address.disp, error);
                }
                return encode_normal(14, rd, address.base, address.disp, error);
            }
        }
        unsigned rs = 0;
        Address store_address;
        if (!register_first && parse_address(operands[0], store_address) &&
            parse_register(operands[1], rs)) {
            for (const LoadStore& entry : kLoadStores) {
                if (mnemonic != entry.name) {
                    continue;
                }
                if (store_address.absolute) {
                    if (entry.long_op == 0xFF) {
                        return encode_st_absolute(rs, store_address.value, error);
                    }
                    return encode_long_integer(entry.long_op, rs,
                                               static_cast<std::int32_t>(store_address.value),
                                               error);
                }
                if (store_address.post || store_address.pre) {
                    unsigned op = entry.compact_op;
                    op += store_address.post ? 5u : 10u;
                    return encode_compact_register(op, rs, store_address.base, error);
                }
                if (store_address.disp == 0 && entry.compact_op != 255) {
                    return encode_compact_register(entry.compact_op, rs, store_address.base, error);
                }
                return encode_normal(entry.normal_op, rs, store_address.base, store_address.disp,
                                     error);
            }
        }
    }

    // Register with an immediate: the shortest form whose range fits (ISA §7).
    if (operands.size() == 2) {
        unsigned rd = 0;
        std::int32_t value = 0;
        const bool register_first = parse_register(operands[0], rd);
        const bool immediate_second =
            !operands[1].empty() &&
            (operands[1][0] == '#' || operands[1][0] == '-' || operands[1][0] == '+' ||
             std::isdigit(static_cast<unsigned char>(operands[1][0])) != 0);
        if (register_first && immediate_second && parse_number(operands[1], value)) {
            struct ImmediateOp {
                const char* name;
                unsigned op;
            };
            constexpr ImmediateOp kSigned[] = {{"add", 7}, {"sub", 8}, {"cmp", 9}};
            constexpr ImmediateOp kUnsigned[] = {{"and", 11}, {"or", 12}, {"xor", 13}};
            constexpr ImmediateOp kShift[] = {{"shl", 20}, {"shr", 21}, {"sar", 22}};
            for (const ImmediateOp& entry : kSigned) {
                if (mnemonic == entry.name) {
                    return encode_normal(entry.op, rd, 0, value, error);
                }
            }
            for (const ImmediateOp& entry : kUnsigned) {
                if (mnemonic == entry.name) {
                    return encode_normal(entry.op, rd, 0, value, error);
                }
            }
            for (const ImmediateOp& entry : kShift) {
                if (mnemonic == entry.name) {
                    return encode_normal(entry.op, rd, 0, value, error);
                }
            }
            if (mnemonic == "ld") {
                if (value >= -128 && value <= -1) {
                    return encode_normal(5, rd, 0, value, error);
                }
                if (value >= 0 && value <= 255) {
                    return encode_normal(6, rd, 0, value, error);
                }
                return encode_ld_immediate(rd, static_cast<std::uint32_t>(value), error);
            }
            if (mnemonic == "mov") {
                return encode_ld_immediate(rd, static_cast<std::uint32_t>(value), error);
            }
        }
    }

    error = "unsupported instruction or operand shape";
    return std::nullopt;
}

}  // namespace krc::asm_kr232
