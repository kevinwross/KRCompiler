#include "kr232_parser.h"

#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

namespace krc::asm_kr232 {
namespace {

std::string lower_and_trim(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    bool started = false;
    for (const char ch : text) {
        if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
            if (started && (out.empty() || out.back() != ' ')) {
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
    struct Alias {
        const char* name;
        unsigned reg;
    };
    constexpr Alias kAliases[] = {
        {"fp", 13}, {"sp", 15}, {"pc", 14}, {"efp", 12}, {"this", 10},
    };
    for (const Alias& alias : kAliases) {
        if (text == alias.name) {
            reg = alias.reg;
            return true;
        }
    }
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

bool parse_fp_register(const std::string& text, unsigned& reg) {
    if (text.size() < 2 || text[0] != 'f') {
        return false;
    }
    char* end = nullptr;
    const unsigned long value = std::strtoul(text.c_str() + 1, &end, 10);
    if (end == nullptr || *end != '\0' || value > 3) {
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

bool parse_address(const std::string& text, Address& address) {
    std::string body = text;
    if (body.size() >= 2 && body.front() == '-' && body[1] == '[') {
        address.pre = true;
        body.erase(body.begin());
    }
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

constexpr Condition kConditions[] = {
    {"eq", 0}, {"ne", 1}, {"cs", 2},  {"cc", 3},  {"mi", 4},  {"pl", 5}, {"vs", 6},
    {"vc", 7}, {"hi", 8}, {"ls", 9},  {"ge", 10}, {"lt", 11}, {"gt", 12}, {"le", 13},
};

bool branch_condition(const std::string& mnemonic, unsigned& code) {
    if (mnemonic.size() != 3 || mnemonic[0] != 'b') {
        return false;
    }
    for (const Condition& condition : kConditions) {
        if (mnemonic.compare(1, 2, condition.name) == 0) {
            code = condition.code;
            return true;
        }
    }
    return false;
}

bool setcc_condition(const std::string& mnemonic, unsigned& code) {
    for (const Condition& condition : kConditions) {
        if (mnemonic == std::string("set") + condition.name) {
            code = condition.code;
            return true;
        }
    }
    return false;
}

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
        error = "unsupported instruction";
        return std::nullopt;
    }

    if (operands.size() == 1) {
        const std::string& operand = operands[0];
        unsigned reg = 0;
        unsigned fp = 0;
        if (parse_register(operand, reg)) {
            struct ShortOpName {
                const char* name;
                ShortOp op;
            };
            constexpr ShortOpName kShort[] = {
                {"push", ShortOp::Push}, {"pop", ShortOp::Pop}, {"inc", ShortOp::Inc},
                {"dec", ShortOp::Dec},   {"call", ShortOp::Call},
            };
            for (const ShortOpName& entry : kShort) {
                if (mnemonic == entry.name) {
                    return encode_short(entry.op, reg, error);
                }
            }
            if (mnemonic == "neg") {
                return encode_compact_register(29, reg, 0, error);
            }
            if (mnemonic == "not") {
                return encode_compact_register(30, reg, 0, error);
            }
            if (mnemonic == "jmp") {
                return encode_compact_register(31, reg, 0, error);
            }
            if (mnemonic == "sxtb") {
                return encode_normal(27, reg, 0, 0, error);
            }
            if (mnemonic == "sxth") {
                return encode_generic(0x1B, {0, static_cast<std::uint8_t>(reg), 0}, {0, 0, 0},
                                      error);
            }
            unsigned code = 0;
            if (setcc_condition(mnemonic, code)) {
                return encode_normal(19, reg, code, 0, error);
            }
        }
        if (parse_fp_register(operand, fp)) {
            struct UnaryFp {
                const char* name;
                unsigned op;
            };
            constexpr UnaryFp kUnary[] = {
                {"fneg", 4}, {"fabs", 5}, {"fsqrt", 6}, {"frnd", 9},
            };
            for (const UnaryFp& entry : kUnary) {
                if (mnemonic == entry.name) {
                    return encode_fp_register(entry.op, fp, 0, error);
                }
            }
            if (mnemonic == "fpush" || mnemonic == "fpop") {
                return encode_fp_stack(mnemonic == "fpush" ? 0 : 1, fp, error);
            }
        }
        if (mnemonic == "jmp" || mnemonic == "call") {
            Address indirect;
            if (parse_address(operand, indirect) && indirect.absolute) {
                return encode_wide(mnemonic == "jmp" ? 2 : 3, 0, indirect.value, error);
            }
        }
        if (!operand.empty() && (operand[0] == '#' || operand[0] == '+' || operand[0] == '-' ||
                                 std::isdigit(static_cast<unsigned char>(operand[0])) != 0)) {
            std::int32_t value = 0;
            if (!parse_number(operand, value)) {
                error = "malformed operand";
                return std::nullopt;
            }
            if (mnemonic == "push" || mnemonic == "pop" || mnemonic == "leave") {
                const unsigned op = mnemonic == "push" ? 23 : (mnemonic == "pop" ? 24 : 28);
                return encode_normal(op, 0, 0, value, error);
            }
            const bool relative = operand[0] == '+' || operand[0] == '-';
            if (mnemonic == "bra") {
                return encode_compact_branch(15, value, error);
            }
            if (mnemonic == "jmp") {
                return relative ? encode_normal(15, 0, 0, value, error)
                                : encode_wide(0, 0, static_cast<std::uint32_t>(value), error);
            }
            if (mnemonic == "call") {
                return relative ? encode_normal(16, 0, 0, value, error)
                                : encode_wide(1, 0, static_cast<std::uint32_t>(value), error);
            }
            unsigned code = 0;
            if (branch_condition(mnemonic, code)) {
                return encode_compact_branch(code, value, error);
            }
            error = "unsupported branch";
            return std::nullopt;
        }
        error = "unsupported one-operand form";
        return std::nullopt;
    }

    if (operands.size() != 2) {
        error = "unsupported operand count";
        return std::nullopt;
    }

    const std::string& first = operands[0];
    const std::string& second = operands[1];
    unsigned ra = 0;
    unsigned rb = 0;
    unsigned fa = 0;
    unsigned fb = 0;
    std::int32_t value = 0;

    if (mnemonic == "enter" && parse_number(first, value)) {
        std::int32_t mask = 0;
        if (parse_number(second, mask)) {
            return encode_enter(static_cast<std::uint32_t>(value), static_cast<unsigned>(mask),
                                error);
        }
    }

    if (parse_register(first, ra) && parse_register(second, rb)) {
        struct CompactOp {
            const char* name;
            unsigned op;
        };
        constexpr CompactOp kCompact[] = {
            {"mov", 0},   {"add", 1},  {"addc", 2}, {"sub", 3},  {"subb", 4},
            {"and", 5},   {"or", 6},   {"xor", 7},  {"cmp", 8},  {"mul", 9},
            {"mulh", 10}, {"shl", 11}, {"shr", 12}, {"sar", 13},
        };
        for (const CompactOp& entry : kCompact) {
            if (mnemonic == entry.name) {
                return encode_compact_register(entry.op, ra, rb, error);
            }
        }
        if (mnemonic == "ld") {
            return encode_compact_register(14, ra, rb, error);
        }
        if (mnemonic == "div" || mnemonic == "divs") {
            return encode_normal(mnemonic == "div" ? 25 : 26, ra, rb, 0, error);
        }
    }

    if (parse_register(first, ra) && !second.empty() &&
        (second[0] == '#' || second[0] == '+' || second[0] == '-' ||
         std::isdigit(static_cast<unsigned char>(second[0])) != 0) &&
        parse_number(second, value)) {
        struct WideImmediate {
            const char* name;
            unsigned normal_op;
            unsigned wide_op;
            bool is_signed;
        };
        constexpr WideImmediate kWide[] = {
            {"add", 7, 0xAu, true},   {"sub", 8, 0xBu, true},   {"cmp", 9, 0xCu, true},
            {"and", 11, 0xDu, false}, {"or", 12, 0xEu, false},  {"xor", 13, 0xFu, false},
        };
        for (const WideImmediate& entry : kWide) {
            if (mnemonic != entry.name) {
                continue;
            }
            const bool fits = entry.is_signed ? (value >= -128 && value <= 127)
                                              : (value >= 0 && value <= 255);
            if (fits) {
                return encode_normal(entry.normal_op, ra, 0, value, error);
            }
            return encode_wide(entry.wide_op, ra, static_cast<std::uint32_t>(value), error);
        }
        if (mnemonic == "shl" || mnemonic == "shr" || mnemonic == "sar") {
            const unsigned op = mnemonic == "shl" ? 20 : (mnemonic == "shr" ? 21 : 22);
            return encode_normal(op, ra, 0, value, error);
        }
        if (mnemonic == "ld" || mnemonic == "mov") {
            if (mnemonic == "ld" && value >= -128 && value <= -1) {
                return encode_normal(5, ra, 0, value, error);
            }
            if (mnemonic == "ld" && value >= 0 && value <= 255) {
                return encode_normal(6, ra, 0, value, error);
            }
            return encode_ld_immediate(ra, static_cast<std::uint32_t>(value), error);
        }
    }

    struct LoadStore {
        const char* name;
        unsigned normal_op;
        unsigned compact_op;
        unsigned long_op;
        unsigned wide_op;
    };
    constexpr LoadStore kLoadStores[] = {
        {"ld", 0, 14, 0xFF, 6},   {"ldbu", 2, 16, 0, 7},   {"ldbs", 3, 17, 1, 8},
        {"ldhu", 29, 255, 5, 9},  {"ldhs", 31, 255, 7, 0xFF}, {"st", 1, 15, 0xFF, 0xFF},
        {"stb", 4, 18, 2, 0xFF},  {"sth", 30, 255, 6, 0xFF},
    };
    {
        Address address;
        if (parse_register(first, ra) && parse_address(second, address)) {
            for (const LoadStore& entry : kLoadStores) {
                if (mnemonic != entry.name) {
                    continue;
                }
                if (address.absolute) {
                    if (entry.long_op == 0xFF) {
                        return encode_ld_absolute(ra, address.value, error);
                    }
                    return encode_long_integer(entry.long_op, ra,
                                               static_cast<std::int32_t>(address.value), error);
                }
                if (address.pc) {
                    if (entry.wide_op == 0xFF) {
                        error = "this access has no PC-relative form";
                        return std::nullopt;
                    }
                    return encode_wide(entry.wide_op, ra,
                                       static_cast<std::uint32_t>(address.disp), error);
                }
                if (address.post || address.pre) {
                    unsigned op = entry.compact_op;
                    if (op == 255) {
                        error = "this access has no auto-increment form";
                        return std::nullopt;
                    }
                    op += address.post ? 5u : 10u;
                    return encode_compact_register(op, ra, address.base, error);
                }
                if (address.disp == 0 && entry.compact_op != 255) {
                    return encode_compact_register(entry.compact_op, ra, address.base, error);
                }
                return encode_normal(entry.normal_op, ra, address.base, address.disp, error);
            }
            if (mnemonic == "lea") {
                if (address.absolute) {
                    return encode_long_integer(3, ra, static_cast<std::int32_t>(address.value),
                                               error);
                }
                if (address.pc || address.sp) {
                    if (address.disp >= -128 && address.disp <= 127) {
                        return encode_long_integer(address.sp ? 4 : 3, ra, address.disp, error);
                    }
                    return encode_wide(address.sp ? 5 : 4, ra,
                                       static_cast<std::uint32_t>(address.disp), error);
                }
                return encode_normal(14, ra, address.base, address.disp, error);
            }
        }
        Address store_address;
        if (!parse_register(first, ra) && parse_address(first, store_address) &&
            parse_register(second, rb)) {
            for (const LoadStore& entry : kLoadStores) {
                if (mnemonic != entry.name) {
                    continue;
                }
                if (store_address.absolute) {
                    if (entry.long_op == 0xFF) {
                        return encode_st_absolute(rb, store_address.value, error);
                    }
                    return encode_long_integer(entry.long_op, rb,
                                               static_cast<std::int32_t>(store_address.value),
                                               error);
                }
                if (store_address.post || store_address.pre) {
                    unsigned op = entry.compact_op;
                    if (op == 255) {
                        error = "this access has no auto-increment form";
                        return std::nullopt;
                    }
                    op += store_address.post ? 5u : 10u;
                    return encode_compact_register(op, rb, store_address.base, error);
                }
                if (store_address.disp == 0 && entry.compact_op != 255) {
                    return encode_compact_register(entry.compact_op, rb, store_address.base,
                                                   error);
                }
                return encode_normal(entry.normal_op, rb, store_address.base, store_address.disp,
                                     error);
            }
        }
    }

    if (parse_fp_register(first, fa) && parse_fp_register(second, fb)) {
        struct BinaryFp {
            const char* name;
            unsigned op;
        };
        constexpr BinaryFp kBinary[] = {
            {"fadd", 0}, {"fsub", 1}, {"fmul", 2}, {"fdiv", 3}, {"fmov", 7}, {"fcmp", 8},
        };
        for (const BinaryFp& entry : kBinary) {
            if (mnemonic == entry.name) {
                return encode_fp_register(entry.op, fa, fb, error);
            }
        }
    }
    {
        Address fp_address;
        if (parse_fp_register(first, fa) && parse_address(second, fp_address)) {
            struct FpMemory {
                const char* name;
                unsigned op;
            };
            constexpr FpMemory kFpMemory[] = {
                {"fld", 0}, {"fst", 1}, {"flds", 2}, {"fsts", 3},
            };
            for (const FpMemory& entry : kFpMemory) {
                if (mnemonic != entry.name) {
                    continue;
                }
                if (fp_address.pc) {
                    return encode_fp_memory_wide(entry.op, fa, 1, 0, fp_address.disp, error);
                }
                if (fp_address.absolute || fp_address.sp) {
                    error = "an FP load or store needs a base register or PC";
                    return std::nullopt;
                }
                unsigned mode = 0;
                if (fp_address.post) {
                    mode = 2;
                } else if (fp_address.pre) {
                    mode = 3;
                } else if (fp_address.disp != 0) {
                    mode = 1;
                }
                if (mode == 1 && (fp_address.disp < -128 || fp_address.disp > 127)) {
                    return encode_fp_memory_wide(entry.op, fa, 0, fp_address.base, fp_address.disp,
                                                 error);
                }
                return encode_fp_memory(entry.op, fa, mode, fp_address.base, fp_address.disp,
                                        error);
            }
        }
    }
    if (mnemonic == "i2f" || mnemonic == "u2f") {
        if (parse_fp_register(first, fa) && parse_register(second, rb)) {
            return encode_fp_convert(mnemonic == "i2f" ? 0 : 1, fa, rb, error);
        }
    }
    if (mnemonic == "f2i" || mnemonic == "f2u") {
        if (parse_register(first, ra) && parse_fp_register(second, fb)) {
            return encode_fp_convert(mnemonic == "f2i" ? 2 : 3, fb, ra, error);
        }
    }

    error = "unsupported instruction or operand shape";
    return std::nullopt;
}

}  // namespace krc::asm_kr232
