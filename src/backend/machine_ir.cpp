#include "backend/machine_ir.h"

#include <cstdio>
#include <optional>
#include <utility>

namespace krc::backend::machine {

Operand Operand::physical(int number) {
    Operand operand;
    operand.kind = Kind::PhysicalRegister;
    operand.value = number;
    return operand;
}

Operand Operand::float_register(int number) {
    Operand operand;
    operand.kind = Kind::FloatRegister;
    operand.value = number;
    return operand;
}

Operand Operand::virtual_register(int number) {
    Operand operand;
    operand.kind = Kind::VirtualRegister;
    operand.value = number;
    return operand;
}

Operand Operand::immediate(std::int64_t value) {
    Operand operand;
    operand.kind = Kind::Immediate;
    operand.value = value;
    return operand;
}

Operand Operand::frame_offset(std::int64_t offset) {
    Operand operand;
    operand.kind = Kind::FrameOffset;
    operand.value = offset;
    operand.symbol = "R7";
    return operand;
}

Operand Operand::bare_address(std::string base) {
    auto operand = Operand::address(std::move(base), 0);
    operand.displacement = false;
    return operand;
}

Operand Operand::address(std::string base, std::int64_t offset) {
    Operand operand;
    operand.kind = Kind::FrameOffset;
    operand.value = offset;
    operand.symbol = std::move(base);
    return operand;
}

Operand Operand::named(std::string symbol) {
    Operand operand;
    operand.kind = Kind::Symbol;
    operand.symbol = std::move(symbol);
    return operand;
}

std::string mnemonic(Opcode opcode) {
    switch (opcode) {
    case Opcode::Mov:
        return "MOV";
    case Opcode::Load:
        return "LOAD";
    case Opcode::Load8S:
        return "LOAD8S";
    case Opcode::Load8U:
        return "LOAD8U";
    case Opcode::Store:
        return "STORE";
    case Opcode::Store8:
        return "STORE8";
    case Opcode::LoadFloat:
        return "FLOAD.D";
    case Opcode::StoreFloat:
        return "FSTORE.D";
    case Opcode::Lea:
        return "LEA";
    case Opcode::Add:
        return "ADD";
    case Opcode::Sub:
        return "SUB";
    case Opcode::Mul:
        return "MUL";
    case Opcode::MulHighUnsigned:
        return "MULHU";
    case Opcode::DivideSigned:
        return "DIVS";
    case Opcode::ModuloSigned:
        return "MODS";
    case Opcode::And:
        return "AND";
    case Opcode::Or:
        return "OR";
    case Opcode::Xor:
        return "XOR";
    case Opcode::Shl:
        return "SHL";
    case Opcode::Shr:
        return "SHR";
    case Opcode::Sar:
        return "SAR";
    case Opcode::Cmp:
        return "CMP";
    case Opcode::FLoadWord:
        return "FLOAD.W";
    case Opcode::FStoreWord:
        return "FSTORE.W";
    case Opcode::FAddDouble:
        return "FADD.D";
    case Opcode::FAddSingle:
        return "FADD.S";
    case Opcode::FSubDouble:
        return "FSUB.D";
    case Opcode::FMulDouble:
        return "FMUL.D";
    case Opcode::FMulSingle:
        return "FMUL.S";
    case Opcode::FDivDouble:
        return "FDIV.D";
    case Opcode::FCompareEqual:
        return "FEQ.D";
    case Opcode::FCompareEqualSingle:
        return "FEQ.S";
    case Opcode::FCompareLessSingle:
        return "FLT.S";
    case Opcode::FCompareLessOrEqualSingle:
        return "FLE.S";
    case Opcode::FSubSingle:
        return "FSUB.S";
    case Opcode::FDivSingle:
        return "FDIV.S";
    case Opcode::FCompareLess:
        return "FLT.D";
    case Opcode::FCompareLessOrEqual:
        return "FLE.D";
    case Opcode::ConvertDoubleToSingle:
        return "FCVT.S.D";
    case Opcode::ConvertSingleToDouble:
        return "FCVT.D.S";
    case Opcode::ConvertWordToDouble:
        return "FCVT.D.W";
    case Opcode::ConvertDoubleToWord:
        return "FCVT.W.D";
    case Opcode::ConvertFloatToWordSingle:
        return "FCVT.W.S";
    case Opcode::ConvertWordToFloatSingle:
        return "FCVT.S.W";
    case Opcode::Jump:
        return "JMP";
    case Opcode::JumpIfZero:
        return "JZ";
    case Opcode::JumpIfNotZero:
        return "JNZ";
    case Opcode::JumpIfCarry:
        return "JCC";
    case Opcode::JumpIfLess:
        return "JLT";
    case Opcode::JumpIfGreater:
        return "JGT";
    case Opcode::JumpIfLessOrEqual:
        return "JLE";
    case Opcode::JumpIfGreaterOrEqual:
        return "JGE";
    case Opcode::Call:
        return "CALL";
    case Opcode::Return:
        return "RET";
    case Opcode::Push:
        return "PUSH";
    case Opcode::Pop:
        return "POP";
    case Opcode::AdjustStack:
        return "ADJSP";
    case Opcode::Halt:
        return "HALT";
    case Opcode::Nop:
        return "NOP";
    }
    return "NOP";
}

namespace {

std::string format_operand(const Operand& operand) {
    switch (operand.kind) {
    case Operand::Kind::PhysicalRegister:
        return "R" + std::to_string(operand.value);
    case Operand::Kind::FloatRegister:
        return "F" + std::to_string(operand.value);
    case Operand::Kind::VirtualRegister:
        // A virtual register is printed where a physical one would go, so that a
        // lowered function is comparable to the emitter's output before allocation
        // has run. `%` keeps it visually distinct and cannot collide with `R<n>`.
        return "%v" + std::to_string(operand.value);
    case Operand::Kind::Immediate:
        return "#" + std::to_string(operand.value);
    case Operand::Kind::FrameOffset: {
        // `[R7 + k]`, `[R7 - k]`, `[R7]` and `[SP]`: the base register is part of the
        // operand because the frame base is loaded off `SP`, which is not a frame
        // offset and cannot be printed as one.
        const auto base = operand.symbol.empty() ? std::string("R7") : operand.symbol;
        if (!operand.displacement) {
            return "[" + base + "]";
        }
        return "[" + base + (operand.value < 0 ? " - " : " + ") +
               std::to_string(operand.value < 0 ? -operand.value : operand.value) + "]";
    }
    case Operand::Kind::Symbol:
        return operand.symbol;
    }
    return {};
}

}  // namespace

std::string print_operand(const Operand& operand) { return format_operand(operand); }

std::string print_instruction(const Instruction& instruction) {
    std::string text = "        ";
    text += mnemonic(instruction.opcode);
    for (std::size_t index = 0; index < instruction.operands.size(); ++index) {
        text += index == 0 ? " " : ", ";
        text += print_operand(instruction.operands[index]);
    }
    if (!instruction.comment.empty()) {
        text += " ; ";
        text += instruction.comment;
    }
    text += "\n";
    return text;
}

std::string print_function(const Function& function) {
    std::string text;
    text += function.symbol;
    text += ":\n";
    for (const auto& block : function.blocks) {
        if (!block.label.empty()) {
            text += block.label;
            text += ":\n";
        }
        for (const auto& instruction : block.instructions) {
            text += print_instruction(instruction);
        }
    }
    return text;
}

namespace {

// The inverse of `mnemonic`. A line whose mnemonic is unknown is not an
// instruction, which is how the round-trip test notices a shape the representation
// cannot express.
std::optional<Opcode> opcode_of(std::string_view name) {
    static const std::pair<std::string_view, Opcode> table[] = {
        {"MOV", Opcode::Mov},           {"LOAD", Opcode::Load},
        {"LOAD8S", Opcode::Load8S}, {"LOAD8U", Opcode::Load8U},     {"STORE", Opcode::Store},
        {"STORE8", Opcode::Store8},     {"FLOAD.D", Opcode::LoadFloat},
        {"FSTORE.D", Opcode::StoreFloat}, {"LEA", Opcode::Lea},
        {"ADD", Opcode::Add},           {"SUB", Opcode::Sub},
        {"MUL", Opcode::Mul},           {"MULHU", Opcode::MulHighUnsigned},
        {"DIVS", Opcode::DivideSigned},   {"MODS", Opcode::ModuloSigned},
        {"JCC", Opcode::JumpIfCarry},           {"AND", Opcode::And},
        {"OR", Opcode::Or},             {"XOR", Opcode::Xor},
        {"SHL", Opcode::Shl},           {"SHR", Opcode::Shr},
        {"SAR", Opcode::Sar},           {"CMP", Opcode::Cmp},           {"FLOAD.W", Opcode::FLoadWord},
        {"FSTORE.W", Opcode::FStoreWord}, {"FADD.D", Opcode::FAddDouble},
        {"FADD.S", Opcode::FAddSingle},   {"FSUB.D", Opcode::FSubDouble},
        {"FMUL.D", Opcode::FMulDouble},   {"FMUL.S", Opcode::FMulSingle},
        {"FDIV.D", Opcode::FDivDouble},   {"FEQ.D", Opcode::FCompareEqual}, {"FLT.D", Opcode::FCompareLess}, {"FEQ.S", Opcode::FCompareEqualSingle},
        {"FLT.S", Opcode::FCompareLessSingle}, {"FLE.S", Opcode::FCompareLessOrEqualSingle},
        {"FSUB.S", Opcode::FSubSingle},   {"FDIV.S", Opcode::FDivSingle},
        {"FLE.D", Opcode::FCompareLessOrEqual},
        {"FCVT.S.D", Opcode::ConvertDoubleToSingle},
        {"FCVT.D.S", Opcode::ConvertSingleToDouble},
        {"FCVT.D.W", Opcode::ConvertWordToDouble},
        {"FCVT.W.D", Opcode::ConvertDoubleToWord},
        {"FCVT.W.S", Opcode::ConvertFloatToWordSingle},
        {"FCVT.S.W", Opcode::ConvertWordToFloatSingle},
        {"JMP", Opcode::Jump},          {"JZ", Opcode::JumpIfZero},
        {"JNZ", Opcode::JumpIfNotZero}, {"JLT", Opcode::JumpIfLess},
        {"JGT", Opcode::JumpIfGreater}, {"JLE", Opcode::JumpIfLessOrEqual},
        {"JGE", Opcode::JumpIfGreaterOrEqual}, {"CALL", Opcode::Call},
        {"RET", Opcode::Return},        {"PUSH", Opcode::Push},
        {"POP", Opcode::Pop},           {"ADJSP", Opcode::AdjustStack},
        {"HALT", Opcode::Halt},         {"NOP", Opcode::Nop},
    };
    for (const auto& [text, opcode] : table) {
        if (text == name) {
            return opcode;
        }
    }
    return std::nullopt;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
        text.remove_suffix(1);
    }
    return text;
}

// One operand in the printer's own forms: `R<n>`, `%v<n>`, `#<n>`, `[R7 + k]`,
// `[R7 - k]`, or a bare symbol.
std::optional<Operand> parse_operand(std::string_view text) {
    text = trim(text);
    if (text.empty()) {
        return std::nullopt;
    }
    if (text.front() == '[') {
        // `[<base>]`, `[<base> + k]`, `[<base> - k]`
        const auto close = text.find(']');
        if (close == std::string_view::npos || close + 1 != text.size()) {
            return std::nullopt;
        }
        auto inside = trim(text.substr(1, close - 1));
        const auto split = inside.find_first_of("+-");
        if (split == std::string_view::npos) {
            auto operand = Operand::address(std::string(inside), 0);
            operand.displacement = false;
            return operand;
        }
        const auto base = trim(inside.substr(0, split));
        const auto sign = inside[split];
        auto number = trim(inside.substr(split + 1));
        std::int64_t value = 0;
        for (const auto character : number) {
            if (character < '0' || character > '9') {
                return std::nullopt;
            }
            value = value * 10 + (character - '0');
        }
        return Operand::address(std::string(base), sign == '-' ? -value : value);
    }
    if (text.front() == '#') {
        auto number = text.substr(1);
        bool negative = false;
        if (!number.empty() && number.front() == '-') {
            negative = true;
            number.remove_prefix(1);
        }
        std::int64_t value = 0;
        for (const auto character : number) {
            if (character < '0' || character > '9') {
                return std::nullopt;
            }
            value = value * 10 + (character - '0');
        }
        return Operand::immediate(negative ? -value : value);
    }
    if (text.front() == 'F' && text.size() > 1) {
        std::int64_t value = 0;
        for (const auto character : text.substr(1)) {
            if (character < '0' || character > '9') {
                return std::nullopt;
            }
            value = value * 10 + (character - '0');
        }
        return Operand::float_register(static_cast<int>(value));
    }
    if (text.front() == 'R' && text.size() > 1) {
        std::int64_t value = 0;
        for (const auto character : text.substr(1)) {
            if (character < '0' || character > '9') {
                return std::nullopt;
            }
            value = value * 10 + (character - '0');
        }
        return Operand::physical(static_cast<int>(value));
    }
    if (text.size() > 2 && text[0] == '%' && text[1] == 'v') {
        std::int64_t value = 0;
        for (const auto character : text.substr(2)) {
            if (character < '0' || character > '9') {
                return std::nullopt;
            }
            value = value * 10 + (character - '0');
        }
        return Operand::virtual_register(static_cast<int>(value));
    }
    return Operand::named(std::string(text));
}

}  // namespace

std::optional<Opcode> mnemonic_to_opcode(std::string_view name) {
    return opcode_of(name);
}

std::optional<Instruction> parse_instruction(std::string_view line) {
    const auto indented = line.size() > 8 && line.substr(0, 8) == "        ";
    if (!indented) {
        return std::nullopt;
    }
    auto rest = line.substr(8);
    std::string comment;
    if (const auto marker = rest.find(" ; "); marker != std::string_view::npos) {
        comment = std::string(trim(rest.substr(marker + 3)));
        rest = rest.substr(0, marker);
    }
    rest = trim(rest);
    const auto space = rest.find(' ');
    const auto name = space == std::string_view::npos ? rest : rest.substr(0, space);
    const auto opcode = opcode_of(name);
    if (!opcode) {
        return std::nullopt;
    }
    Instruction instruction;
    instruction.opcode = *opcode;
    instruction.comment = comment;
    if (space == std::string_view::npos) {
        return instruction;
    }
    auto operands = rest.substr(space + 1);
    // Operands are separated by ", ". A `[R7 + k]` form contains spaces but no
    // comma, so splitting on commas is enough.
    std::size_t start = 0;
    while (start <= operands.size()) {
        const auto comma = operands.find(',', start);
        const auto piece = comma == std::string_view::npos
                                ? operands.substr(start)
                                : operands.substr(start, comma - start);
        const auto operand = parse_operand(piece);
        if (!operand) {
            return std::nullopt;
        }
        instruction.operands.push_back(*operand);
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return instruction;
}

}  // namespace krc::backend::machine
