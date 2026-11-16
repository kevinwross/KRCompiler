#include "asm/kr16_assembler.h"

#include <algorithm>
#include <cstdio>
#include <cctype>
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

    if (name == "ADD" || name == "SUB" || name == "CMP" || name == "AND" ||
        name == "OR" || name == "XOR" || name == "MOV" || name == "TEST" ||
        name == "MUL" || name == "DIV" || name == "DIVU" || name == "MOD" ||
        name == "MODU") {
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
                return encode_i(*rd, functions.at(name), *immediate, error);
            }
            const std::map<std::string, XOp> extended{
                {"ADD", XOp::AddImmediate}, {"SUB", XOp::SubImmediate},
                {"CMP", XOp::CmpImmediate}, {"AND", XOp::AndImmediate},
                {"OR", XOp::OrImmediate},   {"XOR", XOp::XorImmediate},
                {"MOV", XOp::MoveImmediate}, {"TEST", XOp::TestImmediate}};
            return encode_x(extended.at(name), *rd, 0,
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
            {"MUL", RFunction::Mul},   {"DIV", RFunction::Div},
            {"DIVU", RFunction::DivU}, {"MOD", RFunction::Mod},
            {"MODU", RFunction::ModU}};
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
        name == "MUL" || name == "DIV" || name == "DIVU" || name == "MOD" ||
        name == "MODU") {
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
    std::vector<Line> lines;
    std::map<std::string, Label> labels;
    // Where each label sits in the line sequence. Promotion widens an
    // instruction and moves everything after it, so the fixpoint below needs to
    // recompute addresses rather than trust the offsets the parse pass recorded.
    std::map<std::string, std::size_t> label_positions;
    std::vector<std::string> globals;
    std::vector<std::string> weaks;
    std::vector<std::string> externs;

    std::uint32_t section = 0;
    std::uint64_t offset = 0;
    std::istringstream stream{std::string(source)};
    std::string raw;
    std::size_t line_number = 0;
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
            labels[name] = Label{section, offset};
            label_positions[name] = lines.size();
            continue;
        }
        if (line.front() == '.') {
            std::istringstream directive(line);
            std::string name;
            directive >> name;
            if (name == ".text") {
                section = 0;
            } else if (name == ".data") {
                section = 1;
            } else if (name == ".global") {
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
            } else if (name == ".short") {
                // A data word holds either a number or a symbol. A symbol needs a
                // relocation at this offset in the data section, which is what
                // fills a vtable's typeinfo and destructor pointers.
                Line data;
                data.mnemonic = ".short";
                data.operands = split_operands(trim(line.substr(6)));
                for (const auto& operand : data.operands) {
                    if (parse_number(operand)) {
                        continue;
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
                        error = "invalid .short operand on line " +
                                std::to_string(line_number);
                        return false;
                    }
                }
                offset += data.operands.size() * 2;
                lines.push_back(std::move(data));
            } else if (name == ".byte") {
                Line data;
                data.mnemonic = ".byte";
                data.operands = split_operands(trim(line.substr(5)));
                for (const auto& operand : data.operands) {
                    if (!parse_number(operand)) {
                        error = "invalid .byte value on line " +
                                std::to_string(line_number);
                        return false;
                    }
                }
                offset += data.operands.size();
                lines.push_back(std::move(data));
            } else {
                error = "unknown directive '" + name + "' on line " +
                        std::to_string(line_number);
                return false;
            }
            continue;
        }

        Line parsed;
        std::istringstream instruction(line);
        instruction >> parsed.mnemonic;
        std::string rest;
        std::getline(instruction, rest);
        parsed.operands = split_operands(trim(rest));
        const auto size = instruction_size(parsed, error);
        if (!size) {
            error += " (line " + std::to_string(line_number) + ")";
            return false;
        }
        lines.push_back(std::move(parsed));
        offset += static_cast<std::uint64_t>(*size) * 2;
    }

    // Promote an out-of-range BRA to JMP. Promotion widens the instruction, which
    // moves every later label, so this repeats until nothing changes. Only BRA is
    // promoted: an out-of-range conditional needs the inverse condition plus a JMP
    // over a synthetic label, which this assembler has no way to name (ADR 0043),
    // and it keeps reporting the range error instead.
    for (int pass = 0; pass < 64; ++pass) {
        std::vector<std::uint64_t> line_address(lines.size(), 0);
        std::uint64_t address = 0;
        for (std::size_t index = 0; index < lines.size(); ++index) {
            line_address[index] = address;
            if (lines[index].mnemonic == ".short") {
                address += lines[index].operands.size() * 2;
            } else if (lines[index].mnemonic == ".byte") {
                address += lines[index].operands.size();
            } else {
                std::string size_error;
                const auto size = instruction_size(lines[index], size_error);
                address += size ? static_cast<std::uint64_t>(*size) * 2 : 2;
            }
        }
        std::map<std::string, std::uint64_t> addresses;
        for (const auto& entry : label_positions) {
            addresses[entry.first] = entry.second < line_address.size()
                                         ? line_address[entry.second]
                                         : address;
        }
        bool changed = false;
        for (std::size_t index = 0; index < lines.size(); ++index) {
            if (lines[index].mnemonic != "BRA" || lines[index].operands.empty()) {
                continue;
            }
            const auto target = addresses.find(trim(lines[index].operands[0]));
            if (target == addresses.end()) {
                continue;
            }
            const auto displacement =
                static_cast<std::int64_t>(target->second) -
                static_cast<std::int64_t>(line_address[index] + 2);
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

    section = 0;
    offset = 0;
    for (const auto& line : lines) {
        if (line.mnemonic == ".short") {
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
                    result.text.push_back(static_cast<std::uint8_t>(word & 0xFF));
                    result.text.push_back(static_cast<std::uint8_t>(word >> 8));
                    offset += 2;
                    continue;
                }
                const auto symbol = trim(operand);
                result.text.push_back(0);
                result.text.push_back(0);
                // This object has one section: the offset is absolute within
                // it, which is where the linker patches the symbol.
                result.relocations.push_back(AssembledRelocation{symbol, 0, offset});
                offset += 2;
            }
            continue;
        }
        if (line.mnemonic == ".byte") {
            for (const auto& operand : line.operands) {
                const auto value = parse_number(operand);
                if (!value) {
                    error = "invalid .byte value";
                    return false;
                }
                result.text.push_back(
                    static_cast<std::uint8_t>(*value & 0xFF));
            }
            offset += line.operands.size();
            continue;
        }
        const auto encoded =
            encode_mnemonic(line, static_cast<std::uint32_t>(offset), labels,
                            externs, result.relocations, error);
        if (!encoded) {
            return false;
        }
        for (std::uint8_t index = 0; index < encoded->count; ++index) {
            append_word(result.text, encoded->words[index]);
        }
        offset += static_cast<std::uint64_t>(encoded->count) * 2;
    }

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
