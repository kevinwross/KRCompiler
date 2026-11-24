#include "asm/kr16_assembler.h"

#include <algorithm>
#include <cstdio>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "asm/kr16_encoder.h"

namespace krc::asmkr16 {

namespace {

struct Label {
    std::uint32_t section = 0;
    std::uint64_t offset = 0;
};

struct Line {
    std::string label;
    std::string mnemonic;
    std::vector<std::string> operands;
    // The statement as written, trimmed of comments, for the listing.
    std::string text;
    // 0 = .text, 1 = .data, 2 = .bss. A `.bss` statement reserves space only.
    std::uint32_t section = 0;
};

std::string trim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::vector<std::string> split_operands(std::string_view text) {
    std::vector<std::string> operands;
    std::size_t start = 0;
    int depth = 0;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char character = text[index];
        if (character == '[') {
            ++depth;
        } else if (character == ']') {
            --depth;
        } else if (character == ',' && depth == 0) {
            operands.push_back(trim(text.substr(start, index - start)));
            start = index + 1;
        }
    }
    const auto last = trim(text.substr(start));
    if (!last.empty()) {
        operands.push_back(last);
    }
    return operands;
}

std::optional<std::uint8_t> parse_register(std::string_view text) {
    if (text == "SP" || text == "sp") {
        return static_cast<std::uint8_t>(5);
    }
    if (text == "FP" || text == "fp") {
        return static_cast<std::uint8_t>(4);
    }
    if (text == "PC" || text == "pc") {
        return static_cast<std::uint8_t>(7);
    }
    if (text.size() < 2 || (text[0] != 'R' && text[0] != 'r')) {
        return std::nullopt;
    }
    if (text[1] >= '0' && text[1] <= '7' && text.size() == 2) {
        return static_cast<std::uint8_t>(text[1] - '0');
    }
    return std::nullopt;
}

std::optional<std::int32_t> parse_number(std::string_view text) {
    const auto value = trim(text);
    if (value.empty()) {
        return std::nullopt;
    }
    // A character literal holds the character's byte value. kr16's expression
    // evaluator accepts these, and `resources/libc.s` uses `#'0'` and `#'9'`.
    if (value.size() >= 3 && value.front() == '\'' && value.back() == '\'') {
        const auto inner = value.substr(1, value.size() - 2);
        if (inner.size() == 1) {
            return static_cast<std::int32_t>(
                static_cast<unsigned char>(inner.front()));
        }
        if (inner.size() == 2 && inner.front() == '\\') {
            switch (inner.back()) {
            case 'n':
                return '\n';
            case 't':
                return '\t';
            case 'r':
                return '\r';
            case '0':
                return 0;
            case '\\':
                return '\\';
            case '\'':
                return '\'';
            default:
                return std::nullopt;
            }
        }
        return std::nullopt;
    }
    char* end = nullptr;
    const auto parsed = std::strtol(value.c_str(), &end, 0);
    if (end == value.c_str() || (end && *end != '\0')) {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(parsed);
}

bool parse_memory(std::string_view text, std::uint8_t& base,
                  std::int32_t& displacement) {
    const auto value = trim(text);
    if (value.size() < 3 || value.front() != '[' || value.back() != ']') {
        return false;
    }
    const auto inner = trim(value.substr(1, value.size() - 2));
    const auto separator = inner.find_first_of("+-");
    if (separator == std::string::npos) {
        const auto reg = parse_register(inner);
        if (!reg) {
            return false;
        }
        base = *reg;
        displacement = 0;
        return true;
    }
    const auto reg = parse_register(trim(inner.substr(0, separator)));
    const auto offset = parse_number(trim(inner.substr(separator + 1)));
    if (!reg || !offset) {
        return false;
    }
    base = *reg;
    displacement = inner[separator] == '-' ? -*offset : *offset;
    return true;
}

std::optional<EncodedInstruction> encode_mnemonic(
    const Line& line, std::uint32_t address,
    const std::map<std::string, Label>& labels,
    const std::vector<std::string>& externs,
    std::vector<AssembledRelocation>& relocations, std::string& error) {
    const auto& name = line.mnemonic;
    const auto& operands = line.operands;
    const auto register_at = [&](std::size_t index)
        -> std::optional<std::uint8_t> {
        if (index >= operands.size()) {
            return std::nullopt;
        }
        return parse_register(operands[index]);
    };
    const auto immediate_at = [&](std::size_t index)
        -> std::optional<std::int32_t> {
        if (index >= operands.size()) {
            return std::nullopt;
        }
        auto text = trim(operands[index]);
        if (!text.empty() && text.front() == '#') {
            text = trim(std::string_view(text).substr(1));
        }
        return parse_number(text);
    };

    if (name == "NEG" || name == "NOT" || name == "INC" || name == "DEC") {
        const auto rd = register_at(0);
        if (!rd || operands.size() != 1) {
            error = "expected one destination register";
            return std::nullopt;
        }
        const std::map<std::string, RFunction> unary{
            {"NEG", RFunction::Neg}, {"NOT", RFunction::Not},
            {"INC", RFunction::Inc}, {"DEC", RFunction::Dec}};
        // The R-type encoding has a source field with no meaning for a unary
        // operation; kr16's assembler writes the destination there too.
        return encode_r(*rd, *rd, unary.at(name), error);
    }
    if (name == "ADD" || name == "SUB" || name == "CMP" || name == "AND" ||
        name == "OR" || name == "XOR" || name == "MOV" || name == "TEST" ||
        name == "MUL" || name == "MULU" || name == "DIV" || name == "DIVU" ||
        name == "MOD" || name == "MODU") {
        const auto rd = register_at(0);
        if (!rd) {
            error = "expected destination register";
            return std::nullopt;
        }
        if (operands.size() >= 2 && !operands[1].empty() &&
            operands[1].front() == '#') {
            const auto immediate = immediate_at(1);
            if (!immediate) {
                auto label_text = trim(std::string_view(operands[1]).substr(1));
                const auto target = labels.find(label_text);
                if (target == labels.end()) {
                    if (std::find(externs.begin(), externs.end(),
                                  label_text) == externs.end()) {
                        error = "undefined label '" + label_text + "'";
                        return std::nullopt;
                    }
                    relocations.push_back(
                        AssembledRelocation{label_text, 0, address + 2});
                    return encode_x(XOp::MoveImmediate, *rd, 0, 0, error);
                }
                relocations.push_back(
                    AssembledRelocation{label_text, 0, address + 2});
                return encode_x(XOp::MoveImmediate, *rd, 0,
                                static_cast<std::uint16_t>(
                                    0x0100 + target->second.offset),
                                error);
            }
            // A sixteen-bit machine cannot hold a wider literal. Accepting one
            // and truncating it silently is how a 64-bit constant became 0x2001.
            if (*immediate < -32768 || *immediate > 65535) {
                error = "immediate " + std::to_string(*immediate) +
                        " does not fit in a sixteen-bit word for '" + name + "'";
                return std::nullopt;
            }
            if (name == "MOV" && (*immediate < -128 || *immediate > 255)) {
                return encode_x(XOp::MoveImmediate, *rd, 0,
                                static_cast<std::uint16_t>(*immediate), error);
            }
            if (*immediate >= -32 && *immediate <= 31) {
                const std::map<std::string, IFunction> functions{
                    {"ADD", IFunction::Add},   {"SUB", IFunction::Sub},
                    {"CMP", IFunction::Cmp},   {"AND", IFunction::And},
                    {"OR", IFunction::Or},     {"XOR", IFunction::Xor},
                    {"MOV", IFunction::Mov},   {"TEST", IFunction::Test}};
                const auto function = functions.find(name);
                if (function == functions.end()) {
                    error = "'" + name + "' needs a register source";
                    return std::nullopt;
                }
                return encode_i(*rd, function->second, *immediate, error);
            }
            const std::map<std::string, XOp> extended{
                {"ADD", XOp::AddImmediate}, {"SUB", XOp::SubImmediate},
                {"CMP", XOp::CmpImmediate}, {"AND", XOp::AndImmediate},
                {"OR", XOp::OrImmediate},   {"XOR", XOp::XorImmediate},
                {"MOV", XOp::MoveImmediate}, {"TEST", XOp::TestImmediate}};
            const auto operation = extended.find(name);
            if (operation == extended.end()) {
                error = "'" + name + "' needs a register source";
                return std::nullopt;
            }
            return encode_x(operation->second, *rd, 0,
                            static_cast<std::uint16_t>(*immediate), error);
        }
        const auto rs = register_at(1);
        if (!rs) {
            error = "expected source register";
            return std::nullopt;
        }
        const std::map<std::string, RFunction> functions{
            {"ADD", RFunction::Add},   {"SUB", RFunction::Sub},
            {"CMP", RFunction::Cmp},   {"AND", RFunction::And},
            {"OR", RFunction::Or},     {"XOR", RFunction::Xor},
            {"MOV", RFunction::Mov},   {"TEST", RFunction::Test},
            {"MUL", RFunction::Mul},   {"MULU", RFunction::MulU},
            {"DIV", RFunction::Div},   {"DIVU", RFunction::DivU},
            {"MOD", RFunction::Mod},   {"MODU", RFunction::ModU}};
        return encode_r(*rd, *rs, functions.at(name), error);
    }

    const auto memory_op = [&](MOpcode opcode, XOp extended) {
        const auto rd = register_at(0);
        if (!rd || operands.size() < 2) {
            error = "expected load destination and address";
            return std::optional<EncodedInstruction>{};
        }
        std::uint8_t base = 0;
        std::int32_t displacement = 0;
        if (!parse_memory(operands[1], base, displacement)) {
            if (name == "LEA" && operands[1].size() > 2 &&
                operands[1].front() == '[' && operands[1].back() == ']') {
                const auto label_text =
                    trim(std::string_view(operands[1]).substr(1,
                                                              operands[1].size() - 2));
                const auto target = labels.find(label_text);
                if (target == labels.end()) {
                    if (std::find(externs.begin(), externs.end(),
                                  label_text) == externs.end()) {
                        error = "undefined label '" + label_text + "'";
                        return std::optional<EncodedInstruction>{};
                    }
                    relocations.push_back(
                        AssembledRelocation{label_text, 0, address + 2});
                    return encode_x(XOp::MoveImmediate, *rd, 0, 0, error);
                }
                relocations.push_back(
                    AssembledRelocation{label_text, 0, address + 2});
                return encode_x(XOp::MoveImmediate, *rd, 0,
                                static_cast<std::uint16_t>(
                                    0x0100 + target->second.offset),
                                error);
            }
            const auto absolute = immediate_at(1);
            if (!absolute) {
                // An absolute address in brackets: `[0xF005]` or `[symbol]`.
                // kr16's assembler accepts both, and `resources/libc.s` uses
                // them for the console port and its static buffers.
                const auto& operand = operands[1];
                if (operand.size() >= 3 && operand.front() == '[' &&
                    operand.back() == ']') {
                    const auto inner =
                        trim(std::string_view(operand).substr(1, operand.size() - 2));
                    if (const auto value = parse_number(inner)) {
                        return encode_x(extended, *rd, 0,
                                        static_cast<std::uint16_t>(*value), error);
                    }
                    relocations.push_back(
                        AssembledRelocation{inner, 0, address + 2});
                    const auto target = labels.find(inner);
                    return encode_x(
                        extended, *rd, 0,
                        target == labels.end()
                            ? std::uint16_t{0}
                            : static_cast<std::uint16_t>(0x0100 +
                                                         target->second.offset),
                        error);
                }
                error = "expected memory operand";
                return std::optional<EncodedInstruction>{};
            }
            return encode_x(extended, *rd, 0,
                            static_cast<std::uint16_t>(*absolute), error);
        }
        if (displacement >= -32 && displacement <= 31) {
            return encode_m(opcode, *rd, base, displacement, error);
        }
        const auto extended_op =
            extended == XOp::LoadAbsolute
                ? XOp::LoadIndexed
                : (extended == XOp::StoreAbsolute
                       ? XOp::StoreIndexed
                       : (extended == XOp::LoadByteAbsolute
                              ? XOp::LoadIndexed
                              : (extended == XOp::LoadByteSignedAbsolute
                                     ? XOp::LoadIndexed
                                     : (extended == XOp::StoreByteAbsolute
                                            ? XOp::StoreIndexed
                                            : XOp::LeaIndexed))));
        return encode_x(extended_op, *rd, base,
                        static_cast<std::uint16_t>(displacement), error);
    };

    if (name == "LD") {
        return memory_op(MOpcode::Load, XOp::LoadAbsolute);
    }
    if (name == "LDB") {
        return memory_op(MOpcode::LoadByte, XOp::LoadByteAbsolute);
    }
    if (name == "LDSB") {
        return memory_op(MOpcode::LoadByteSigned,
                         XOp::LoadByteSignedAbsolute);
    }
    if (name == "LEA") {
        return memory_op(MOpcode::Lea, XOp::LeaIndexed);
    }
    if (name == "ST" || name == "STB") {
        if (operands.size() < 2) {
            error = "expected address and source register";
            return std::nullopt;
        }
        const auto source = register_at(1);
        if (!source) {
            error = "expected source register";
            return std::nullopt;
        }
        std::uint8_t base = 0;
        std::int32_t displacement = 0;
        if (!parse_memory(operands[0], base, displacement)) {
            const auto& operand = operands[0];
            if (operand.size() >= 3 && operand.front() == '[' &&
                operand.back() == ']') {
                const auto inner =
                    trim(std::string_view(operand).substr(1, operand.size() - 2));
                const auto opcode_absolute = name == "ST" ? XOp::StoreAbsolute
                                                          : XOp::StoreByteAbsolute;
                if (const auto value = parse_number(inner)) {
                    return encode_x(opcode_absolute, *source, 0,
                                    static_cast<std::uint16_t>(*value), error);
                }
                relocations.push_back(
                    AssembledRelocation{inner, 0, address + 2});
                const auto target = labels.find(inner);
                return encode_x(
                    opcode_absolute, *source, 0,
                    target == labels.end()
                        ? std::uint16_t{0}
                        : static_cast<std::uint16_t>(0x0100 +
                                                     target->second.offset),
                    error);
            }
            error = "expected memory destination";
            return std::nullopt;
        }
        const auto opcode =
            name == "ST" ? MOpcode::Store : MOpcode::StoreByte;
        if (displacement >= -32 && displacement <= 31) {
            return encode_m(opcode, *source, base, displacement, error);
        }
        return encode_x(name == "ST" ? XOp::StoreIndexed
                                     : XOp::StoreByteAbsolute,
                        *source, base,
                        static_cast<std::uint16_t>(displacement), error);
    }

    if (name == "SHL" || name == "SHR" || name == "SAR") {
        const auto rd = register_at(0);
        if (!rd) {
            error = "expected shift register";
            return std::nullopt;
        }
        const auto shift = name == "SHL" ? RFunction::Shl
                                         : (name == "SHR" ? RFunction::Shr
                                                          : RFunction::Sar);
        if (const auto count = immediate_at(1)) {
            const auto immediate_shift =
                name == "SHL" ? Shift::Shl
                              : (name == "SHR" ? Shift::Shr : Shift::Sar);
            return encode_shift(*rd, immediate_shift,
                                static_cast<std::uint8_t>(*count), error);
        }
        const auto rs = register_at(1);
        if (!rs) {
            error = "expected shift count register";
            return std::nullopt;
        }
        return encode_r(*rd, *rs, shift, error);
    }

    if (name == "BTST" || name == "BSET" || name == "BCLR" ||
        name == "BTOG") {
        const auto rd = register_at(0);
        const auto bit = immediate_at(1);
        if (!rd || !bit) {
            error = "expected bit register and index";
            return std::nullopt;
        }
        const auto operation = name == "BTST" ? BitOperation::Btst
                                              : (name == "BSET"
                                                     ? BitOperation::Bset
                                                     : (name == "BCLR"
                                                            ? BitOperation::Bclr
                                                            : BitOperation::Btog));
        return encode_bit(*rd, operation, static_cast<std::uint8_t>(*bit),
                          error);
    }

    if (name == "PUSH" || name == "POP") {
        const auto rd = register_at(0);
        if (!rd) {
            error = "expected register";
            return std::nullopt;
        }
        return encode_c(name == "PUSH" ? CSubop::Push : CSubop::Pop, *rd, 0,
                        error);
    }
    if (name == "RET") {
        return encode_c(CSubop::Return, 0, 0, error);
    }
    if (name == "RETI") {
        return encode_c(CSubop::ReturnInterrupt, 0, 0, error);
    }
    if (name == "HLT") {
        return encode_c(CSubop::Halt, 0, 0, error);
    }
    if (name == "NOP") {
        return encode_c(CSubop::Nop, 0, 0, error);
    }
    if (name == "EI") {
        return encode_c(CSubop::EnableInterrupts, 0, 0, error);
    }
    if (name == "DI") {
        return encode_c(CSubop::DisableInterrupts, 0, 0, error);
    }
    if (name == "WAIT") {
        return encode_c(CSubop::Wait, 0, 0, error);
    }
    if (name == "BRK") {
        return encode_c(CSubop::Break, 0, 0, error);
    }
    if (name == "TRAP" || name == "SYS") {
        const auto number = immediate_at(0);
        if (!number) {
            error = "expected service number";
            return std::nullopt;
        }
        return encode_c(name == "TRAP" ? CSubop::Trap : CSubop::System, 0,
                        static_cast<std::uint8_t>(*number), error);
    }
    if (name == "JMP" || name == "CALL") {
        if (operands.empty()) {
            error = "expected target";
            return std::nullopt;
        }
        if (const auto reg = register_at(0)) {
            return encode_c(name == "JMP" ? CSubop::JumpRegister
                                          : CSubop::CallRegister,
                            *reg, 0, error);
        }
        const auto target = labels.find(trim(operands[0]));
        if (target == labels.end()) {
            error = "undefined label '" + trim(operands[0]) + "'";
            return std::nullopt;
        }
        const auto displacement =
            static_cast<std::int32_t>(target->second.offset) -
            static_cast<std::int32_t>(address + 4);
        if (displacement % 2 != 0) {
            error = "unpaired control-transfer target";
            return std::nullopt;
        }
        return encode_c_relative(name == "JMP" ? CSubop::JumpRelative
                                               : CSubop::CallRelative,
                                 displacement / 2, error);
    }

    static const std::map<std::string, Condition> conditions{
        {"BEQ", Condition::Eq}, {"BNE", Condition::Ne},
        {"BCS", Condition::Cs}, {"BCC", Condition::Cc},
        {"BHS", Condition::Cs}, {"BLO", Condition::Cc},
        {"BMI", Condition::Mi}, {"BPL", Condition::Pl},
        {"BVS", Condition::Vs}, {"BVC", Condition::Vc},
        {"BHI", Condition::Hi}, {"BLS", Condition::Ls},
        {"BGE", Condition::Ge}, {"BLT", Condition::Lt},
        {"BGT", Condition::Gt}, {"BLE", Condition::Le},
        {"BRA", Condition::Al}};
    if (const auto found = conditions.find(name); found != conditions.end()) {
        if (operands.empty()) {
            error = "expected branch target";
            return std::nullopt;
        }
        const auto target = labels.find(trim(operands[0]));
        if (target == labels.end()) {
            error = "undefined label '" + trim(operands[0]) + "'";
            return std::nullopt;
        }
        const auto displacement =
            static_cast<std::int32_t>(target->second.offset) -
            static_cast<std::int32_t>(address + 2);
        if (displacement % 2 != 0) {
            error = "unpaired branch target";
            return std::nullopt;
        }
        return encode_b(found->second, displacement / 2, error);
    }

    error = "unknown KR16 mnemonic '" + name + "'";
    return std::nullopt;
}

std::optional<std::uint8_t> instruction_size(const Line& line,
                                             std::string& error) {
    const auto& name = line.mnemonic;
    const auto immediate_at = [&](std::size_t index)
        -> std::optional<std::int32_t> {
        if (index >= line.operands.size()) {
            return std::nullopt;
        }
        auto text = trim(line.operands[index]);
        if (!text.empty() && text.front() == '#') {
            text = trim(std::string_view(text).substr(1));
        }
        return parse_number(text);
    };
    const auto memory_size = [&](std::size_t index) -> std::uint8_t {
        std::uint8_t base = 0;
        std::int32_t displacement = 0;
        if (!parse_memory(line.operands[index], base, displacement)) {
            return 2;
        }
        return displacement >= -32 && displacement <= 31 ? 1 : 2;
    };

    if (name == "ADD" || name == "SUB" || name == "CMP" || name == "AND" ||
        name == "OR" || name == "XOR" || name == "MOV" || name == "TEST" ||
        name == "MUL" || name == "MULU" || name == "DIV" || name == "DIVU" ||
        name == "MOD" || name == "MODU") {
        if (line.operands.size() >= 2 && !line.operands[1].empty() &&
            line.operands[1].front() == '#') {
            const auto immediate = immediate_at(1);
            if (!immediate) {
                if (name == "MOV") {
                    return std::uint8_t{2};
                }
                error = "expected immediate";
                return std::nullopt;
            }
            if (name == "MOV") {
                return *immediate >= -32 && *immediate <= 31
                           ? std::uint8_t{1}
                           : std::uint8_t{2};
            }
            return *immediate >= -32 && *immediate <= 31 ? std::uint8_t{1}
                                                         : std::uint8_t{2};
        }
        return std::uint8_t{1};
    }
    if (name == "LD" || name == "LDB" || name == "LDSB" || name == "LEA") {
        return line.operands.size() >= 2 ? memory_size(1) : std::uint8_t{1};
    }    if (name == "ST" || name == "STB") {
        return line.operands.empty() ? std::uint8_t{1} : memory_size(0);
    }
    if (name == "JMP" || name == "CALL") {
        if (line.operands.empty()) {
            error = "expected target";
            return std::nullopt;
        }
        return parse_register(line.operands[0]) ? std::uint8_t{1}
                                                : std::uint8_t{2};
    }
    return std::uint8_t{1};
}

void append_word(std::vector<std::uint8_t>& bytes, std::uint16_t word) {
    bytes.push_back(static_cast<std::uint8_t>(word & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>(word >> 8));
}

}  // namespace

bool assemble(std::string_view source, AssemblyResult& result,
              std::string& error) {
    constexpr std::uint32_t kText = 0;
    constexpr std::uint32_t kData = 1;
    constexpr std::uint32_t kBss = 2;
    const auto align_up = [](std::uint64_t value, std::uint64_t alignment) {
        if (alignment <= 1) {
            return value;
        }
        return (value + alignment - 1) / alignment * alignment;
    };
    // Section ids: the three fixed sections first, then `.section .text.<name>`
    // sections in first-use order. Labels, relocations and listing rows carry the
    // id, so a named section keeps its identity through the object.
    std::vector<std::string> section_names{".text", ".data", ".bss"};
    std::vector<std::uint32_t> section_types{0, 1, 2};
    std::map<std::string, std::uint32_t> named_sections;
    const auto ensure_section = [&](const std::string& name) -> std::uint32_t {
        const auto found = named_sections.find(name);
        if (found != named_sections.end()) {
            return found->second;
        }
        const auto id = static_cast<std::uint32_t>(section_names.size());
        section_names.push_back(name);
        section_types.push_back(0);
        named_sections.emplace(name, id);
        return id;
    };
    const auto section_name = [&](std::uint32_t section) -> const std::string& {
        return section_names[section];
    };
    const auto valid_section_name = [](std::string_view name) {
        return std::all_of(name.begin(), name.end(), [](char character) {
            return std::isalnum(static_cast<unsigned char>(character)) != 0 ||
                   character == '_' || character == '.' || character == '$' ||
                   character == '-';
        });
    };

    std::vector<Line> lines;
    std::map<std::string, Label> labels;
    // Where each label sits in the line sequence. Promotion widens an
    // instruction and moves everything after it, so the fixpoint below needs to
    // recompute addresses rather than trust the offsets the parse pass recorded.
    std::map<std::string, std::size_t> label_positions;
    std::vector<std::string> globals;
    std::vector<std::string> weaks;
    std::vector<std::string> externs;

    std::uint32_t section = kText;
    std::istringstream stream{std::string(source)};
    std::string raw;
    std::size_t line_number = 0;
    const auto note_section = [&]() {
        result.has_data = result.has_data || section == kData;
        result.has_bss = result.has_bss || section == kBss;
    };
    while (std::getline(stream, raw)) {
        ++line_number;
        const auto comment = raw.find(';');
        if (comment != std::string::npos) {
            raw.resize(comment);
        }
        auto line = trim(raw);
        if (line.empty()) {
            continue;
        }
        if (line.back() == ':') {
            const auto name = trim(std::string_view(line).substr(0, line.size() - 1));
            if (name.empty()) {
                error = "empty label on line " + std::to_string(line_number);
                return false;
            }
            labels[name] = Label{section, 0};
            label_positions[name] = lines.size();
            continue;
        }
        if (line.front() == '.') {
            std::istringstream directive(line);
            std::string name;
            directive >> name;
            if (name == ".text") {
                section = kText;
                note_section();
            } else if (name == ".section") {
                std::string target;
                directive >> target;
                if (target.rfind(".text.", 0) != 0 || target.size() <= 6 ||
                    !valid_section_name(target)) {
                    error = "unsupported section '" + target + "' on line " +
                            std::to_string(line_number) +
                            " (expected .section .text.<name>)";
                    return false;
                }
                section = ensure_section(target);
            } else if (name == ".data") {
                section = kData;
                note_section();
            } else if (name == ".bss") {
                section = kBss;
                note_section();
            } else if (name == ".global" || name == ".globl") {
                std::string symbol;
                directive >> symbol;
                globals.push_back(symbol);
            } else if (name == ".weak") {
                std::string symbol;
                directive >> symbol;
                weaks.push_back(symbol);
                globals.push_back(symbol);
            } else if (name == ".extern") {
                std::string symbol;
                directive >> symbol;
                externs.push_back(symbol);
            } else if (name == ".short" || name == ".word" || name == ".byte") {
                // A data word holds either a number or a symbol. A symbol needs a
                // relocation at this offset in its section, which is what fills a
                // vtable's typeinfo and destructor pointers.
                if (section == kBss) {
                    error = name + " is not allowed in .bss on line " +
                            std::to_string(line_number);
                    return false;
                }
                Line data;
                data.mnemonic = name;
                data.text = line;
                data.section = section;
                data.operands = split_operands(trim(line.substr(name.size())));
                for (const auto& operand : data.operands) {
                    if (parse_number(operand)) {
                        continue;
                    }
                    if (name == ".byte") {
                        error = "invalid .byte value on line " +
                                std::to_string(line_number);
                        return false;
                    }
                    const auto symbol = trim(operand);
                    const auto name_of = [](std::string_view text) {
                        return std::all_of(
                            text.begin(), text.end(), [](char character) {
                                return std::isalnum(
                                           static_cast<unsigned char>(character)) != 0 ||
                                       character == '_' || character == '.' ||
                                       character == '$';
                            });
                    };
                    if (symbol.empty() || !name_of(symbol)) {
                        error = "invalid " + name + " operand on line " +
                                std::to_string(line_number);
                        return false;
                    }
                }
                lines.push_back(std::move(data));
            } else if (name == ".space" || name == ".zero") {
                // kr16 reserves at most one 64 KiB section in a single .space.
                const auto value = parse_number(trim(line.substr(name.size())));
                if (!value || *value < 0 || *value > 0x10000) {
                    error = name + " size out of range on line " +
                            std::to_string(line_number);
                    return false;
                }
                Line data;
                data.mnemonic = ".space";
                data.text = line;
                data.section = section;
                data.operands.push_back(std::to_string(*value));
                lines.push_back(std::move(data));
            } else if (name == ".align" || name == ".balign") {
                const auto value = parse_number(trim(line.substr(name.size())));
                if (!value || *value < 0) {
                    error = name + " needs a constant on line " +
                            std::to_string(line_number);
                    return false;
                }
                Line data;
                data.mnemonic = ".align";
                data.text = line;
                data.section = section;
                data.operands.push_back(std::to_string(*value));
                lines.push_back(std::move(data));
            } else {
                error = "unknown directive '" + name + "' on line " +
                        std::to_string(line_number);
                return false;
            }
            continue;
        }

        Line parsed;
        parsed.text = line;
        parsed.section = section;
        std::istringstream instruction(line);
        instruction >> parsed.mnemonic;
        std::string rest;
        std::getline(instruction, rest);
        parsed.operands = split_operands(trim(rest));
        if (section == kBss) {
            error = "instruction in .bss on line " + std::to_string(line_number);
            return false;
        }
        const auto size = instruction_size(parsed, error);
        if (!size) {
            error += " (line " + std::to_string(line_number) + ")";
            return false;
        }
        lines.push_back(std::move(parsed));
    }

    // One pass computes every section's cursor, each line's section-relative
    // address, and each label's address. A label takes the cursor of the section
    // it was declared in at its position, which is what makes a label at the end
    // of `.bss` resolve to the end of `.bss` rather than to `.text`'s origin.
    struct Layout {
        std::vector<std::uint64_t> offsets;
        std::vector<std::uint64_t> sizes;
        std::map<std::string, std::uint64_t> label_addresses;
    };
    const auto compute_layout = [&](Layout& layout) -> bool {
        layout.offsets.assign(lines.size(), 0);
        layout.sizes.assign(section_names.size(), 0);
        layout.label_addresses.clear();
        std::map<std::size_t, std::vector<std::string>> at;
        for (const auto& entry : label_positions) {
            at[entry.second].push_back(entry.first);
        }
        for (std::size_t index = 0; index < lines.size(); ++index) {
            for (const auto& name : at[index]) {
                layout.label_addresses[name] = layout.sizes[labels[name].section];
            }
            const auto& line = lines[index];
            const auto line_section = line.section;
            layout.offsets[index] = layout.sizes[line_section];
            std::uint64_t size = 0;
            if (line.mnemonic == ".short" || line.mnemonic == ".word") {
                size = line.operands.size() * 2;
            } else if (line.mnemonic == ".byte") {
                size = line.operands.size();
            } else if (line.mnemonic == ".space") {
                size = static_cast<std::uint64_t>(
                    parse_number(line.operands.front()).value_or(0));
            } else if (line.mnemonic == ".align") {
                const auto alignment = static_cast<std::uint64_t>(
                    parse_number(line.operands.front()).value_or(0));
                size = align_up(layout.sizes[line_section], alignment) -
                       layout.sizes[line_section];
            } else {
                std::string size_error;
                const auto instruction_bytes = instruction_size(line, size_error);
                if (!instruction_bytes) {
                    error = size_error;
                    return false;
                }
                size = static_cast<std::uint64_t>(*instruction_bytes) * 2;
            }
            layout.sizes[line_section] += size;
        }
        for (const auto& name : at[lines.size()]) {
            layout.label_addresses[name] = layout.sizes[labels[name].section];
        }
        return true;
    };

    // Promote an out-of-range BRA to JMP. Promotion widens the instruction, which
    // moves every later label, so this repeats until nothing changes. Only BRA is
    // promoted: an out-of-range conditional needs the inverse condition plus a JMP
    // over a synthetic label, which this assembler has no way to name (ADR 0043),
    // and it keeps reporting the range error instead.
    Layout layout;
    for (int pass = 0; pass < 64; ++pass) {
        if (!compute_layout(layout)) {
            return false;
        }
        bool changed = false;
        for (std::size_t index = 0; index < lines.size(); ++index) {
            if (lines[index].mnemonic != "BRA" || lines[index].operands.empty()) {
                continue;
            }
            const auto name = trim(lines[index].operands[0]);
            const auto target = layout.label_addresses.find(name);
            if (target == layout.label_addresses.end()) {
                continue;
            }
            const auto declared = labels.find(name);
            if (declared == labels.end() ||
                declared->second.section != lines[index].section) {
                continue;
            }
            const auto displacement =
                static_cast<std::int64_t>(target->second) -
                static_cast<std::int64_t>(layout.offsets[index] + 2);
            const auto words = displacement / 2;
            if (words < -128 || words > 127) {
                lines[index].mnemonic = "JMP";
                changed = true;
            }
        }
        if (!changed) {
            break;
        }
    }
    if (!compute_layout(layout)) {
        return false;
    }
    // The encoder resolves branch targets through `labels`, whose offsets are now
    // final and section-relative.
    for (auto& entry : labels) {
        const auto found = layout.label_addresses.find(entry.first);
        if (found != layout.label_addresses.end()) {
            entry.second.offset = found->second;
        }
    }

    result.text.clear();
    result.data.clear();
    result.bss_size = 0;
    result.sections.clear();
    result.relocations.clear();
    result.symbols.clear();
    result.listing.clear();
    result.listing.reserve(lines.size());
    std::vector<std::vector<std::uint8_t>> section_bytes(section_names.size());
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const auto& line = lines[index];
        const auto line_section = line.section;
        const auto offset = layout.offsets[index];
        const bool nobits = line_section == kBss;
        auto& bytes = section_bytes[line_section];
        const auto start = bytes.size();

        if (line.mnemonic == ".align") {
            if (!nobits) {
                const auto alignment = static_cast<std::uint64_t>(
                    parse_number(line.operands.front()).value_or(0));
                const auto padding = align_up(start, alignment) - start;
                bytes.insert(bytes.end(), static_cast<std::size_t>(padding), 0);
            }
        } else if (line.mnemonic == ".space") {
            if (!nobits) {
                const auto count = static_cast<std::size_t>(
                    parse_number(line.operands.front()).value_or(0));
                bytes.insert(bytes.end(), count, 0);
            }
        } else if (line.mnemonic == ".short" || line.mnemonic == ".word") {
            for (const auto& operand : line.operands) {
                // Only a digit or a sign makes a number; parse_number accepts an
                // identifier as zero, which would silently drop the relocation.
                const auto trimmed = trim(operand);
                const bool numeric =
                    !trimmed.empty() &&
                    (std::isdigit(static_cast<unsigned char>(trimmed.front())) != 0 ||
                     trimmed.front() == '-' || trimmed.front() == '+');
                const auto value = numeric
                                       ? parse_number(trimmed)
                                       : std::optional<std::int32_t>{};
                if (value) {
                    const auto word = static_cast<std::uint16_t>(*value & 0xFFFF);
                    bytes.push_back(static_cast<std::uint8_t>(word & 0xFF));
                    bytes.push_back(static_cast<std::uint8_t>(word >> 8));
                    continue;
                }
                const auto symbol = trim(operand);
                bytes.push_back(0);
                bytes.push_back(0);
                result.relocations.push_back(AssembledRelocation{
                    symbol, line_section,
                    offset + static_cast<std::uint64_t>(bytes.size() - start - 2)});
            }
        } else if (line.mnemonic == ".byte") {
            for (const auto& operand : line.operands) {
                const auto value = parse_number(operand);
                if (!value) {
                    error = "invalid .byte value";
                    return false;
                }
                bytes.push_back(static_cast<std::uint8_t>(*value & 0xFF));
            }
        } else {
            const auto relocation_start = result.relocations.size();
            const auto encoded =
                encode_mnemonic(line, static_cast<std::uint32_t>(offset), labels,
                                externs, result.relocations, error);
            if (!encoded) {
                return false;
            }
            for (std::uint8_t word = 0; word < encoded->count; ++word) {
                append_word(bytes, encoded->words[word]);
            }
            // The encoder assumes text; a statement in `.data` must carry the
            // section the linker patches.
            for (std::size_t relocation = relocation_start;
                 relocation < result.relocations.size(); ++relocation) {
                result.relocations[relocation].section = line_section;
            }
        }

        toolchain::ListingRow row;
        row.section = std::string(section_name(line_section));
        row.address = offset;
        row.text = line.text;
        if (!nobits && start <= bytes.size()) {
            row.bytes.assign(bytes.begin() + static_cast<std::ptrdiff_t>(start),
                             bytes.end());
        }
        result.listing.push_back(std::move(row));
    }
    result.sections.reserve(section_names.size());
    for (std::size_t index = 0; index < section_names.size(); ++index) {
        AssembledSection assembled_section;
        assembled_section.name = section_names[index];
        assembled_section.type = section_types[index];
        assembled_section.size = layout.sizes[index];
        assembled_section.data = std::move(section_bytes[index]);
        assembled_section.present =
            index == kText || !assembled_section.data.empty() ||
            assembled_section.size > 0;
        result.sections.push_back(std::move(assembled_section));
    }
    result.text = result.sections[kText].data;
    result.data = result.sections[kData].data;
    result.bss_size = result.sections[kBss].size;

    for (const auto& entry : labels) {
        const bool global = std::find(globals.begin(), globals.end(),
                                      entry.first) != globals.end();
        const bool weak = std::find(weaks.begin(), weaks.end(),
                                 entry.first) != weaks.end();
        result.symbols.push_back(AssembledSymbol{
            entry.first, entry.second.section, entry.second.offset, global,
            false, weak});
    }
    for (const auto& name : externs) {
        result.symbols.push_back(AssembledSymbol{name, 0, 0, false, true, false});
    }
    return true;
}

}  // namespace krc::asmkr16
