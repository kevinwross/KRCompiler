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

    error = "unsupported instruction or operand shape";
    return std::nullopt;
}

}  // namespace krc::asm_kr232
