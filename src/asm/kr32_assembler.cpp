#include "asm/kr32_assembler.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "asm/kr32_encoder.h"

namespace krc::asmkr32 {

namespace {

enum class Section { Text, Data, Exception };

// The section order make_object() writes, and the index an object's symbol or
// relocation uses for the section it belongs to.
std::uint32_t section_index(Section section) {
    switch (section) {
    case Section::Text:
        return 0;
    case Section::Data:
        return 1;
    case Section::Exception:
        break;
    }
    return 2;
}

enum class ItemKind { Label, Instruction, Bytes, Word, Zero, Align };

struct Item {
    ItemKind kind = ItemKind::Label;
    Section section = Section::Text;
    std::size_t line = 0;
    std::size_t offset = 0;
    std::size_t size = 0;
    std::string text;
    std::string name;
    std::string mnemonic;
    std::vector<std::string> operands;
};

std::string_view section_name(Section section) {
    switch (section) {
    case Section::Text:
        return ".text";
    case Section::Data:
        return ".data";
    case Section::Exception:
        break;
    }
    return ".cpp_except";
}

// The backend writes `.Ldbg.<function>.<line>.<column>:` before the first
// instruction of every source line. The listing uses it to attribute a
// statement to its source line; 0 means the statement had no such label.
std::uint32_t debug_line_from_label(std::string_view name) {
    if (!name.starts_with(".Ldbg.")) {
        return 0;
    }
    const auto column_dot = name.rfind('.');
    if (column_dot == std::string_view::npos || column_dot == 0) {
        return 0;
    }
    const auto line_dot = name.rfind('.', column_dot - 1);
    if (line_dot == std::string_view::npos) {
        return 0;
    }
    const auto digits = name.substr(line_dot + 1, column_dot - line_dot - 1);
    if (digits.empty()) {
        return 0;
    }
    std::uint32_t value = 0;
    for (const auto character : digits) {
        if (character < '0' || character > '9') {
            return 0;
        }
        value = value * 10 + static_cast<std::uint32_t>(character - '0');
    }
    return value;
}

struct MemoryOperand {
    int base = -1;
    bool stack_relative = false;
    int mode = 0;
    std::int32_t displacement = 0;
    std::string symbol;
    bool has_symbol = false;
};

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

std::string upper(std::string_view text) {
    std::string result(text);
    for (auto& character : result) {
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    }
    return result;
}

std::vector<std::string> split_operands(std::string_view text) {
    std::vector<std::string> result;
    std::size_t begin = 0;
    int depth = 0;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '[') {
            ++depth;
        } else if (text[index] == ']') {
            --depth;
        } else if (text[index] == ',' && depth == 0) {
            result.emplace_back(trim(text.substr(begin, index - begin)));
            begin = index + 1;
        }
    }
    const auto tail = trim(text.substr(begin));
    if (!tail.empty()) {
        result.emplace_back(tail);
    }
    return result;
}

// `CRn` names one of the sixteen control registers, which RDCR and WRCR carry in
// the field bits rather than in a general register.
bool parse_control_register(std::string_view text, std::uint8_t& reg) {
    if (text.size() < 3 || (text[0] != 'C' && text[0] != 'c') ||
        (text[1] != 'R' && text[1] != 'r')) {
        return false;
    }
    int value = 0;
    for (std::size_t index = 2; index < text.size(); ++index) {
        if (!std::isdigit(static_cast<unsigned char>(text[index]))) {
            return false;
        }
        value = value * 10 + (text[index] - '0');
        if (value > 15) {
            return false;
        }
    }
    reg = static_cast<std::uint8_t>(value);
    return true;
}
bool parse_register(std::string_view text, std::uint8_t& reg) {
    if (text.size() < 2 || (text[0] != 'R' && text[0] != 'r')) {
        return false;
    }
    int value = 0;
    for (std::size_t index = 1; index < text.size(); ++index) {
        if (!std::isdigit(static_cast<unsigned char>(text[index]))) {
            return false;
        }
        value = value * 10 + (text[index] - '0');
    }
    if (value < 0 || value > 15) {
        return false;
    }
    reg = static_cast<std::uint8_t>(value);
    return true;
}

bool parse_fp_register(std::string_view text, std::uint8_t& reg) {
    if (text.size() < 2 || (text[0] != 'F' && text[0] != 'f')) {
        return false;
    }
    int value = 0;
    for (std::size_t index = 1; index < text.size(); ++index) {
        if (!std::isdigit(static_cast<unsigned char>(text[index]))) {
            return false;
        }
        value = value * 10 + (text[index] - '0');
    }
    if (value < 0 || value > 15) {
        return false;
    }
    reg = static_cast<std::uint8_t>(value);
    return true;
}

bool parse_tile(std::string_view text, std::uint8_t& reg) {
    if (text.size() < 2 || (text[0] != 'T' && text[0] != 't')) {
        return false;
    }
    int value = 0;
    for (std::size_t index = 1; index < text.size(); ++index) {
        if (!std::isdigit(static_cast<unsigned char>(text[index]))) {
            return false;
        }
        value = value * 10 + (text[index] - '0');
    }
    if (value < 0 || value > 15) {
        return false;
    }
    reg = static_cast<std::uint8_t>(value);
    return true;
}

// The 0x61 M escape group: one mnemonic per matrix/transform function,
// docs/isa-v1.md §7.
std::optional<MatrixFunction> matrix_function(std::string_view mnemonic) {
    if (mnemonic == "MMUL") return MatrixFunction::Mul;
    if (mnemonic == "MMAC") return MatrixFunction::MulAdd;
    if (mnemonic == "MVEC") return MatrixFunction::Vector;
    if (mnemonic == "MTRANS") return MatrixFunction::Transpose;
    if (mnemonic == "VDOT") return MatrixFunction::Dot;
    if (mnemonic == "MPUSH") return MatrixFunction::Push;
    if (mnemonic == "MPOP") return MatrixFunction::Pop;
    if (mnemonic == "MDROP") return MatrixFunction::Drop;
    return std::nullopt;
}

// The 0x52/0x53 tile transfer group. `L` selects the 16-byte vector form;
// MLOAD/MSTORE with L clear transfer a 64-byte tile.
bool is_tile_transfer(std::string_view mnemonic) {
    return mnemonic == "MLOAD" || mnemonic == "MSTORE" || mnemonic == "VLOAD" ||
           mnemonic == "VSTORE";
}

bool is_tile_operation(std::string_view mnemonic) {
    return matrix_function(mnemonic).has_value() || is_tile_transfer(mnemonic);
}

std::optional<std::uint32_t> fp_function(std::string_view mnemonic) {
    if (mnemonic == "FADD.S") return 0x000;
    if (mnemonic == "FSUB.S") return 0x001;
    if (mnemonic == "FMUL.S") return 0x002;
    if (mnemonic == "FDIV.S") return 0x003;
    if (mnemonic == "FEQ.S") return 0x009;
    if (mnemonic == "FLT.S") return 0x00A;
    if (mnemonic == "FLE.S") return 0x00B;
    if (mnemonic == "FADD.D") return 0x100;
    if (mnemonic == "FSUB.D") return 0x101;
    if (mnemonic == "FMUL.D") return 0x102;
    if (mnemonic == "FDIV.D") return 0x103;
    if (mnemonic == "FEQ.D") return 0x109;
    if (mnemonic == "FLT.D") return 0x10A;
    if (mnemonic == "FLE.D") return 0x10B;
    if (mnemonic == "FCVT.S.D") return 0x020;
    if (mnemonic == "FCVT.D.S") return 0x021;
    if (mnemonic == "FCVT.W.S") return 0x022;
    if (mnemonic == "FCVT.WU.S") return 0x023;
    if (mnemonic == "FCVT.S.W") return 0x024;
    if (mnemonic == "FCVT.S.WU") return 0x025;
    if (mnemonic == "FCVT.W.D") return 0x026;
    if (mnemonic == "FCVT.WU.D") return 0x027;
    if (mnemonic == "FCVT.D.W") return 0x028;
    if (mnemonic == "FCVT.D.WU") return 0x029;
    if (mnemonic == "FMV.W.X") return 0x030;
    if (mnemonic == "FMV.X.W") return 0x031;
    return std::nullopt;
}

bool parse_integer(std::string_view text, std::int64_t& value) {
    text = trim(text);
    if (!text.empty() && text.front() == '#') {
        text.remove_prefix(1);
    }
    if (text.empty()) {
        return false;
    }
    bool negative = false;
    if (text.front() == '+' || text.front() == '-') {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }
    if (text.empty()) {
        return false;
    }
    unsigned base = 10;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text.remove_prefix(2);
    }
    std::uint64_t magnitude = 0;
    for (const auto character : text) {
        unsigned digit = 0;
        if (character >= '0' && character <= '9') {
            digit = static_cast<unsigned>(character - '0');
        } else if (base == 16 && character >= 'a' && character <= 'f') {
            digit = static_cast<unsigned>(character - 'a' + 10);
        } else if (base == 16 && character >= 'A' && character <= 'F') {
            digit = static_cast<unsigned>(character - 'A' + 10);
        } else {
            return false;
        }
        if (digit >= base) {
            return false;
        }
        magnitude = magnitude * base + digit;
    }
    value = negative ? -static_cast<std::int64_t>(magnitude)
                     : static_cast<std::int64_t>(magnitude);
    return true;
}

bool is_identifier(std::string_view text) {
    if (text.empty()) {
        return false;
    }
    // An identifier never starts with a digit: without this rule a number the
    // assembler could not parse (454660, say) is taken as a symbol name and the
    // real complaint -- an operand form it does not support -- never appears.
    if (std::isdigit(static_cast<unsigned char>(text.front()))) {
        return false;
    }
    for (const auto character : text) {
        const auto uc = static_cast<unsigned char>(character);
        if (!(std::isalnum(uc) || character == '_' || character == '.' || character == '$')) {
            return false;
        }
    }
    return true;
}

bool parse_memory(std::string_view text, MemoryOperand& memory, std::string& error) {
    text = trim(text);
    if (text.size() < 2 || text.front() != '[' || text.back() != ']') {
        error = "expected a bracketed memory operand";
        return false;
    }
    text = trim(text.substr(1, text.size() - 2));
    if (text.empty()) {
        error = "empty memory operand";
        return false;
    }

    std::string_view base_text = text;
    std::string_view rest;
    const auto plus = text.find_first_of("+-");
    if (plus != std::string_view::npos) {
        base_text = trim(text.substr(0, plus));
        rest = trim(text.substr(plus));
    }
    if (base_text == "SP" || base_text == "sp") {
        memory.stack_relative = true;
        memory.mode = 4;
        memory.base = 0;
    } else {
        std::uint8_t base_register = 0;
        if (parse_register(base_text, base_register)) {
            memory.base = base_register;
            memory.mode = rest.empty() ? 2 : 3;
        } else if (rest.empty() && is_identifier(base_text)) {
            memory.mode = 5;
            memory.symbol = std::string(base_text);
            memory.has_symbol = true;
            memory.base = 0;
            return true;
        } else {
            error = "invalid memory base";
            return false;
        }
    }

    if (rest.empty()) {
        return true;
    }
    bool negative = rest.front() == '-';
    auto magnitude_text = trim(rest.substr(1));
    std::int64_t magnitude = 0;
    if (parse_integer(magnitude_text, magnitude)) {
        memory.displacement = static_cast<std::int32_t>(negative ? -magnitude : magnitude);
        return true;
    }
    if (negative) {
        error = "a symbol displacement cannot be subtracted";
        return false;
    }
    if (!is_identifier(magnitude_text)) {
        error = "invalid memory displacement";
        return false;
    }
    memory.symbol = std::string(magnitude_text);
    memory.has_symbol = true;
    return true;
}

bool fits_inline(std::int64_t value) { return value >= -1024 && value <= 1023; }

struct ParsedInstruction {
    std::string mnemonic;
    std::vector<std::string> operands;
    bool extension = false;
};

bool instruction_layout(const Item& item, bool& extension, std::string& error) {
    ParsedInstruction parsed;
    parsed.mnemonic = upper(item.mnemonic);
    parsed.operands = item.operands;
    extension = false;

    const auto& operands = item.operands;
    const auto has_register = [&](std::size_t index) {
        std::uint8_t reg = 0;
        return index < operands.size() && parse_register(operands[index], reg);
    };
    const auto has_integer = [&](std::size_t index, std::int64_t& value) {
        return index < operands.size() && parse_integer(operands[index], value);
    };
    const auto has_memory = [&](std::size_t index, MemoryOperand& memory) {
        return index < operands.size() && parse_memory(operands[index], memory, error);
    };

    const auto& mnemonic = parsed.mnemonic;
    if (matrix_function(mnemonic)) {
        if (mnemonic == "MDROP") {
            if (!operands.empty()) {
                error = "MDROP takes no operands";
                return false;
            }
            return true;
        }
        if (mnemonic == "MPUSH" || mnemonic == "MPOP") {
            std::uint8_t tile = 0;
            if (operands.size() != 1 || !parse_tile(operands[0], tile)) {
                error = "expected a tile register";
                return false;
            }
            return true;
        }
        // MTRANS reads one tile; the multiply forms read two. VDOT writes a
        // floating-point register and reads two tiles.
        const std::size_t reads_two = mnemonic == "MMUL" || mnemonic == "MMAC" ||
                                      mnemonic == "MVEC" || mnemonic == "VDOT";
        if (operands.size() != (reads_two ? 3 : 2)) {
            error = reads_two ? "expected Td, Ta, Tb" : "expected Td, Ta";
            return false;
        }
        std::uint8_t destination = 0;
        if (reads_two && mnemonic == "VDOT") {
            if (!parse_fp_register(operands[0], destination)) {
                error = "expected a floating-point register";
                return false;
            }
        } else if (!parse_tile(operands[0], destination)) {
            error = "expected a tile register";
            return false;
        }
        std::uint8_t source = 0;
        if (!parse_tile(operands[1], source)) {
            error = "expected a tile register";
            return false;
        }
        std::uint8_t third = 0;
        if (reads_two && !parse_tile(operands[2], third)) {
            error = "expected a tile register";
            return false;
        }
        return true;
    }
    if (is_tile_transfer(mnemonic)) {
        if (operands.size() < 2 || operands.size() > 3) {
            error = "expected Td, [addr], optional #stride";
            return false;
        }
        std::uint8_t tile = 0;
        if (!parse_tile(operands[0], tile)) {
            error = "expected a tile register";
            return false;
        }
        MemoryOperand memory;
        if (!has_memory(1, memory)) {
            error = "invalid memory operand";
            return false;
        }
        if (operands.size() == 3) {
            // Only the 64-byte form has a strided variant, and its extension
            // word carries the row stride.
            if (mnemonic == "VLOAD" || mnemonic == "VSTORE") {
                error = "strided VLOAD/VSTORE is reserved";
                return false;
            }
            std::int64_t stride = 0;
            if (!parse_integer(operands[2], stride)) {
                error = "invalid row stride";
                return false;
            }
            if (memory.displacement != 0 || memory.has_symbol) {
                error = "strided MLOAD/MSTORE requires a zero displacement";
                return false;
            }
            extension = true;
            return true;
        }
        extension = memory.has_symbol ||
                    (memory.mode >= 3 && !fits_inline(memory.displacement));
        return true;
    }
    if (mnemonic == "FLOAD.W" || mnemonic == "FLOAD.D" || mnemonic == "FSTORE.W" ||
        mnemonic == "FSTORE.D") {
        if (operands.size() != 2) {
            error = "floating load/store expects two operands";
            return false;
        }
        std::uint8_t fp_register = 0;
        if (!parse_fp_register(operands[0], fp_register)) {
            error = "expected a floating-point register";
            return false;
        }
        MemoryOperand memory;
        if (!has_memory(1, memory)) {
            error = "invalid floating memory operand";
            return false;
        }
        extension = memory.has_symbol ||
                    (memory.mode >= 3 && !fits_inline(memory.displacement));
        return true;
    }
    if (fp_function(mnemonic)) {
        for (const auto& operand : operands) {
            std::uint8_t fp_register = 0;
            std::uint8_t integer_register = 0;
            if (!parse_fp_register(operand, fp_register) &&
                !parse_register(operand, integer_register)) {
                error = "invalid floating-point operand";
                return false;
            }
        }
        return operands.size() == 2 || operands.size() == 3;
    }
    if (mnemonic == "MOV" && operands.size() == 2 && has_register(0)) {
        std::int64_t value = 0;
        extension = !has_register(1) && (!has_integer(1, value) || !fits_inline(value));
        return true;
    }
    if ((mnemonic == "ADD" || mnemonic == "SUB" || mnemonic == "MUL" ||
         mnemonic == "MULH" || mnemonic == "MULHU" || mnemonic == "DIV" ||
         mnemonic == "DIVS" || mnemonic == "MOD" ||
         mnemonic == "MODS" || mnemonic == "AND" || mnemonic == "OR" ||
         mnemonic == "XOR" || mnemonic == "SHL" || mnemonic == "SHR" ||
         mnemonic == "SAR" || mnemonic == "CMP" || mnemonic == "SLT" ||
         mnemonic == "SLTU") &&
        operands.size() == 3 && has_register(0)) {
        std::int64_t value = 0;
        extension = !has_register(2) && (!has_integer(2, value) || !fits_inline(value));
        return true;
    }
    if (mnemonic == "CMP" && operands.size() == 2 && has_register(0)) {
        std::int64_t value = 0;
        extension = !has_register(1) && (!has_integer(1, value) || !fits_inline(value));
        return true;
    }
    if ((mnemonic == "NOT" || mnemonic == "NEG") && operands.size() == 2) {
        return true;
    }
    if ((mnemonic == "LOAD" || mnemonic == "LOAD8U" || mnemonic == "LOAD8S" ||
         mnemonic == "LOAD16U" || mnemonic == "LOAD16S" || mnemonic == "LEA") &&
        operands.size() == 2 && has_register(0)) {
        MemoryOperand memory;
        if (!has_memory(1, memory)) {
            error = "invalid memory operand";
            return false;
        }
        extension = memory.has_symbol || (memory.mode >= 3 && !fits_inline(memory.displacement));
        return true;
    }
    if ((mnemonic == "STORE" || mnemonic == "STORE8" || mnemonic == "STORE16") &&
        operands.size() == 2 && has_register(0)) {
        MemoryOperand memory;
        if (!has_memory(1, memory)) {
            error = "invalid memory operand";
            return false;
        }
        extension = memory.has_symbol || (memory.mode >= 3 && !fits_inline(memory.displacement));
        return true;
    }
    if (mnemonic == "JMP" || mnemonic == "CALL" ||
        (mnemonic.size() > 1 && mnemonic[0] == 'J' && mnemonic != "JMP")) {
        std::uint8_t target_register = 0;
        extension = !parse_register(operands[0], target_register);
        return operands.size() == 1;
    }
    if (mnemonic == "BRK" || mnemonic == "IRET" || mnemonic == "PUSHR" ||
        mnemonic == "POPR") {
        return operands.empty();
    }
    if (mnemonic == "WRCR" && operands.size() == 2) {
        std::uint8_t control = 0;
        std::uint8_t data = 0;
        return parse_control_register(operands[0], control) && parse_register(operands[1], data);
    }
    if (mnemonic == "RDCR" && operands.size() == 2) {
        std::uint8_t control = 0;
        std::uint8_t destination = 0;
        return parse_register(operands[0], destination) &&
               parse_control_register(operands[1], control);
    }
    if (mnemonic == "RET" || mnemonic == "HALT") {
        return operands.empty();
    }
    if (mnemonic == "SYSCALL" && operands.size() == 1) {
        std::int64_t value = 0;
        if (!parse_integer(operands[0], value)) {
            error = "SYSCALL expects an immediate";
            return false;
        }
        extension = !fits_inline(value);
        return true;
    }
    if ((mnemonic == "PUSH" || mnemonic == "POP") && operands.size() == 1) {
        return true;
    }
    if (mnemonic == "WRSP" && operands.size() == 1) {
        std::uint8_t reg = 0;
        if (!parse_register(operands[0], reg)) {
            error = "WRSP expects a register";
            return false;
        }
        return true;
    }
    if (mnemonic == "SETSP" && operands.size() == 1) {
        std::uint8_t reg = 0;
        if (!parse_register(operands[0], reg)) {
            error = "SETSP expects a register";
            return false;
        }
        return true;
    }
    if (mnemonic == "ADJSP" && operands.size() == 1) {
        std::int64_t value = 0;
        if (!parse_integer(operands[0], value)) {
            error = "ADJSP expects an immediate";
            return false;
        }
        extension = !fits_inline(value);
        return true;
    }

    error = "unsupported instruction '" + mnemonic + "'";
    return false;
}

std::optional<Opcode> opcode_for(std::string_view mnemonic) {
    if (mnemonic == "MOV") return Opcode::Mov;
    if (mnemonic == "LOAD") return Opcode::Load;
    if (mnemonic == "STORE") return Opcode::Store;
    if (mnemonic == "LOAD8U") return Opcode::Load8U;
    if (mnemonic == "LOAD8S") return Opcode::Load8S;
    if (mnemonic == "LOAD16U") return Opcode::Load16U;
    if (mnemonic == "LOAD16S") return Opcode::Load16S;
    if (mnemonic == "STORE8") return Opcode::Store8;
    if (mnemonic == "STORE16") return Opcode::Store16;
    if (mnemonic == "LEA") return Opcode::Lea;
    if (mnemonic == "SLT") return Opcode::Slt;
    if (mnemonic == "SLTU") return Opcode::Sltu;
    if (mnemonic == "ADD") return Opcode::Add;
    if (mnemonic == "SUB") return Opcode::Sub;
    if (mnemonic == "MUL") return Opcode::Mul;
    if (mnemonic == "MULH") return Opcode::MulH;
    if (mnemonic == "MULHU") return Opcode::MulHu;
    if (mnemonic == "DIV") return Opcode::Div;
    if (mnemonic == "DIVS") return Opcode::DivS;
    if (mnemonic == "MOD") return Opcode::Mod;
    if (mnemonic == "MODS") return Opcode::ModS;
    if (mnemonic == "CMP") return Opcode::Cmp;
    if (mnemonic == "AND") return Opcode::And;
    if (mnemonic == "OR") return Opcode::Or;
    if (mnemonic == "XOR") return Opcode::Xor;
    if (mnemonic == "NOT") return Opcode::Not;
    if (mnemonic == "NEG") return Opcode::Neg;
    if (mnemonic == "SHL") return Opcode::Shl;
    if (mnemonic == "SHR") return Opcode::Shr;
    if (mnemonic == "SAR") return Opcode::Sar;
    if (mnemonic == "SYSCALL") return Opcode::Syscall;
    return std::nullopt;
}

std::optional<Condition> condition_for(std::string_view mnemonic) {
    if (mnemonic == "JZ") return Condition::Eq;
    if (mnemonic == "JNZ") return Condition::Ne;
    if (mnemonic == "JCS") return Condition::Cs;
    if (mnemonic == "JNC" || mnemonic == "JCC") return Condition::Cc;
    if (mnemonic == "JMI") return Condition::Mi;
    if (mnemonic == "JPL") return Condition::Pl;
    if (mnemonic == "JVS") return Condition::Vs;
    if (mnemonic == "JVC") return Condition::Vc;
    if (mnemonic == "JHI") return Condition::Hi;
    if (mnemonic == "JLS") return Condition::Ls;
    if (mnemonic == "JGE") return Condition::Ge;
    if (mnemonic == "JLT") return Condition::Lt;
    if (mnemonic == "JGT") return Condition::Gt;
    if (mnemonic == "JLE") return Condition::Le;
    return std::nullopt;
}



class Assembler {
public:
    bool run(std::string_view source, AssemblyResult& result, std::string& error);

private:
    bool parse(std::string_view source, std::string& error);
    bool layout(std::string& error);
    bool encode_items(AssemblyResult& result, std::string& error);
    bool resolve_symbol(std::string_view name, std::uint32_t& address, bool& external,
                        std::string& error) const;

    std::vector<Item> items_;
    std::unordered_set<std::string> global_names_;
    std::unordered_set<std::string> weak_names_;
    std::unordered_set<std::string> external_names_;
    std::unordered_map<std::string, std::uint32_t> addresses_;
    std::unordered_map<std::string, Section> labels_;
    // `.equ NAME, VALUE` constants, substituted into operands as they are parsed:
    // the kr32os sources define offsets this way and then use them in immediates.
    std::unordered_map<std::string, std::int64_t> constants_;
    // Replaces every identifier that names a constant with its value. Identifiers
    // that are not constants are left alone, so labels and registers pass through.
    std::string substitute_constants(std::string_view text) const {
        std::string out;
        std::size_t index = 0;
        while (index < text.size()) {
            const auto character = text[index];
            if (std::isalpha(static_cast<unsigned char>(character)) || character == '_') {
                std::size_t end = index;
                while (end < text.size() &&
                       (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_')) {
                    ++end;
                }
                const std::string name(text.substr(index, end - index));
                const auto found = constants_.find(name);
                out += found == constants_.end() ? name : std::to_string(found->second);
                index = end;
                continue;
            }
            out.push_back(character);
            ++index;
        }
        return out;
    }
    std::size_t text_size_ = 0;
    std::size_t data_size_ = 0;
    std::size_t exception_size_ = 0;
    std::uint32_t data_base_ = 0;
};

bool Assembler::parse(std::string_view source, std::string& error) {
    Section section = Section::Text;
    bool debug_info_section = false;
    std::size_t line_number = 0;
    std::size_t position = 0;
    while (position <= source.size()) {
        const auto newline = source.find('\n', position);
        const auto end = newline == std::string_view::npos ? source.size() : newline;
        auto line = source.substr(position, end - position);
        position = end + 1;
        ++line_number;
        const auto comment = line.find(';');
        if (comment != std::string_view::npos) {
            line = line.substr(0, comment);
        }
        line = trim(line);
        if (line.empty()) {
            if (end == source.size()) {
                break;
            }
            continue;
        }

        if (line.back() == ':') {
            Item item;
            item.kind = ItemKind::Label;
            item.section = section;
            item.line = line_number;
            item.text = std::string(line);
            item.name = std::string(trim(line.substr(0, line.size() - 1)));
            if (item.name.empty() || !is_identifier(item.name)) {
                error = "line " + std::to_string(line_number) + ": invalid label";
                return false;
            }
            items_.push_back(std::move(item));
            if (end == source.size()) {
                break;
            }
            continue;
        }

        if (line.front() == '.') {
            const auto space = line.find_first_of(" \t");
            const auto directive = upper(line.substr(0, space));
            const auto rest = space == std::string_view::npos
                                  ? std::string_view{}
                                  : trim(line.substr(space + 1));
            if (directive == ".TEXT") {
                section = Section::Text;
            } else if (directive == ".DATA") {
                section = Section::Data;
            } else if (directive == ".CPP_EXCEPT") {
                section = Section::Exception;
            } else if (directive == ".GLOBAL") {
                global_names_.emplace(std::string(rest));
            } else if (directive == ".WEAK") {
                weak_names_.emplace(std::string(rest));
                global_names_.emplace(std::string(rest));
            } else if (directive == ".EXTERN") {
                external_names_.emplace(std::string(rest));
            } else if (directive == ".ALIGN") {
                std::int64_t value = 0;
                if (!parse_integer(rest, value) || value <= 0) {
                    error = "line " + std::to_string(line_number) + ": invalid .align";
                    return false;
                }
                Item item;
                item.kind = ItemKind::Align;
                item.section = section;
                item.line = line_number;
                item.text = std::string(line);
                item.operands.push_back(std::to_string(value));
                items_.push_back(std::move(item));
            } else if (directive == ".RODATA" || directive == ".BSS") {
                // kr32os names its read-only and zero-filled data separately; both
                // land in the data section, which is what the old assembler's
                // `.rodata`/`.bss` produced before the linker saw them.
                section = Section::Data;
            } else if (directive == ".DEBUG_INFO") {
                // The sources carry debug strings for the retired toolchain's KDBG.
                // This compiler builds KDBG from its own bridge, so the directive is
                // accepted and its contents are not emitted.
                debug_info_section = true;
            } else if (directive == ".SECTION") {
                const auto name = trim(rest);
                if (name == ".text") {
                    section = Section::Text;
                    debug_info_section = false;
                } else if (name == ".data" || name == ".rodata" || name == ".bss") {
                    section = Section::Data;
                    debug_info_section = false;
                } else if (name == ".debug_info") {
                    debug_info_section = true;
                } else {
                    error = "line " + std::to_string(line_number) + ": unsupported section " +
                            std::string(name);
                    return false;
                }
            } else if (directive == ".FILE") {
                // A source-name hint for debuggers; this toolchain reads the path
                // from the file it was given.
            } else if (directive == ".EQU") {
                const auto comma = rest.find(',');
                if (comma == std::string_view::npos) {
                    error = "line " + std::to_string(line_number) + ": .equ expects NAME, VALUE";
                    return false;
                }
                const auto name = std::string(trim(rest.substr(0, comma)));
                std::int64_t value = 0;
                if (name.empty() || !parse_integer(trim(rest.substr(comma + 1)), value)) {
                    error = "line " + std::to_string(line_number) + ": invalid .equ";
                    return false;
                }
                constants_[name] = value;
            } else if (directive == ".ASCIZ") {
                auto text = trim(rest);
                if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
                    text = text.substr(1, text.size() - 2);
                }
                if (!debug_info_section) {
                    Item item;
                    item.kind = ItemKind::Bytes;
                    item.section = section;
                    item.line = line_number;
                    item.text = std::string(line);
                    for (const auto character : text) {
                        item.operands.push_back(std::to_string(static_cast<unsigned char>(character)));
                    }
                    item.operands.push_back("0");
                    items_.push_back(std::move(item));
                }
            } else if (directive == ".SPACE") {
                std::int64_t size = 0;
                if (!parse_integer(substitute_constants(rest), size) || size < 0) {
                    error = "line " + std::to_string(line_number) + ": invalid .space";
                    return false;
                }
                if (!debug_info_section) {
                    Item item;
                    item.kind = ItemKind::Zero;
                    item.section = section;
                    item.line = line_number;
                    item.text = std::string(line);
                    item.operands.push_back(std::to_string(size));
                    items_.push_back(std::move(item));
                }
            } else if (directive == ".BYTE" || directive == ".WORD" || directive == ".ZERO") {
                Item item;
                item.kind = directive == ".BYTE" ? ItemKind::Bytes
                            : directive == ".WORD" ? ItemKind::Word
                                                   : ItemKind::Zero;
                item.section = section;
                item.line = line_number;
                item.text = std::string(line);
                const auto parts = split_operands(rest);
                if (item.kind == ItemKind::Zero) {
                    if (parts.size() != 1) {
                        error = "line " + std::to_string(line_number) + ": .zero expects one value";
                        return false;
                    }
                    item.operands.push_back(parts.front());
                } else {
                    for (const auto& part : parts) {
                        item.operands.push_back(substitute_constants(part));
                    }
                }
                items_.push_back(std::move(item));
            } else {
                error = "line " + std::to_string(line_number) + ": unsupported directive " +
                        std::string(directive);
                return false;
            }
            if (end == source.size()) {
                break;
            }
            continue;
        }

        const auto space = line.find_first_of(" \t");
        Item item;
        item.kind = ItemKind::Instruction;
        item.section = section;
        item.line = line_number;
        item.text = std::string(line);
        item.mnemonic = std::string(space == std::string_view::npos ? line
                                                                    : line.substr(0, space));
        if (space != std::string_view::npos) {
            for (const auto& operand : split_operands(line.substr(space + 1))) {
                item.operands.push_back(substitute_constants(operand));
            }
        }
        items_.push_back(std::move(item));
        if (end == source.size()) {
            break;
        }
    }
    return true;
}

bool Assembler::layout(std::string& error) {
    std::size_t text_offset = 0;
    std::size_t data_offset = 0;
    std::size_t exception_offset = 0;
    const auto section_offset = [&](Section which) -> std::size_t& {
        switch (which) {
        case Section::Text:
            return text_offset;
        case Section::Data:
            return data_offset;
        case Section::Exception:
            break;
        }
        return exception_offset;
    };
    for (auto& item : items_) {
        const auto align_to = [](std::size_t value, std::size_t alignment) {
            return (value + alignment - 1) / alignment * alignment;
        };
        if (item.kind == ItemKind::Label) {
            labels_.emplace(item.name, item.section);
            item.offset = section_offset(item.section);
            continue;
        }
        if (item.kind == ItemKind::Align) {
            std::int64_t alignment = 0;
            parse_integer(item.operands.front(), alignment);
            const auto target = static_cast<std::size_t>(alignment);
            section_offset(item.section) = align_to(section_offset(item.section), target);
            item.offset = section_offset(item.section);
            continue;
        }
        if (item.kind == ItemKind::Instruction) {
            bool extension = false;
            if (!instruction_layout(item, extension, error)) {
                error = "line " + std::to_string(item.line) + ": " + error;
                return false;
            }
            item.offset = section_offset(item.section);
            item.size = extension ? 8 : 4;
            section_offset(item.section) += item.size;
            continue;
        }
        item.offset = section_offset(item.section);
        if (item.kind == ItemKind::Bytes) {
            item.size = item.operands.size();
            section_offset(item.section) += item.size;
        } else if (item.kind == ItemKind::Word) {
            item.size = item.operands.size() * 4;
            section_offset(item.section) += item.size;
        } else {
            std::int64_t size = 0;
            if (!parse_integer(item.operands.front(), size) || size < 0) {
                error = "line " + std::to_string(item.line) + ": invalid .zero size";
                return false;
            }
            item.size = static_cast<std::size_t>(size);
            section_offset(item.section) += item.size;
        }
    }
    text_size_ = text_offset;
    data_size_ = data_offset;
    exception_size_ = exception_offset;
    data_base_ = static_cast<std::uint32_t>((text_size_ + 3) / 4 * 4);
    for (const auto& item : items_) {
        if (item.kind == ItemKind::Label) {
            const auto label = labels_.find(item.name);
            if (label != labels_.end() && label->second != Section::Exception) {
                addresses_[item.name] = static_cast<std::uint32_t>(
                    item.offset + (label->second == Section::Data ? data_base_ : 0));
            }
        }
    }
    return true;
}

bool Assembler::resolve_symbol(std::string_view name, std::uint32_t& address, bool& external,
                               std::string& error) const {
    const auto found = addresses_.find(std::string(name));
    if (found != addresses_.end()) {
        address = found->second;
        external = false;
        return true;
    }
    if (external_names_.find(std::string(name)) != external_names_.end()) {
        address = 0;
        external = true;
        return true;
    }
    error = "undefined symbol: " + std::string(name);
    return false;
}

bool Assembler::encode_items(AssemblyResult& result, std::string& error) {
    result.text.assign(text_size_, 0);
    result.data.assign(data_size_, 0);
    result.exception.assign(exception_size_, 0);

    const auto emit_word = [&](std::vector<std::uint8_t>& bytes, std::size_t offset,
                               std::uint32_t word) {
        if (offset + 4 > bytes.size()) {
            bytes.resize(offset + 4, 0);
        }
        bytes[offset + 0] = static_cast<std::uint8_t>(word & 0xFF);
        bytes[offset + 1] = static_cast<std::uint8_t>((word >> 8) & 0xFF);
        bytes[offset + 2] = static_cast<std::uint8_t>((word >> 16) & 0xFF);
        bytes[offset + 3] = static_cast<std::uint8_t>((word >> 24) & 0xFF);
    };

    const auto encode_or_fail = [&](Opcode opcode, std::uint8_t rd, std::uint8_t rs1,
                                    std::uint8_t field, bool literal, std::int32_t immediate,
                                    bool extension, std::uint32_t extension_word) {
        std::string local_error;
        auto encoded = encode(opcode, rd, rs1, field, literal, immediate, extension,
                              extension_word, local_error);
        if (!encoded) {
            error = local_error;
            return std::optional<EncodedInstruction>{};
        }
        return encoded;
    };

    const auto operand_value = [&](std::string_view text, std::uint8_t& reg, std::int64_t& value,
                                   bool& is_register, bool& is_symbol, std::string& symbol) {
        is_register = parse_register(text, reg);
        is_symbol = false;
        if (is_register) {
            return true;
        }
        if (parse_integer(text, value)) {
            return true;
        }
        if (is_identifier(text)) {
            is_symbol = true;
            symbol = std::string(text);
            return true;
        }
        // `#symbol` is the immediate form of the same thing: the kr32os assembly
        // loads an address with `MOV Rd, #label`, and the relocation is identical
        // to the one the bare form emits.
        if (text.size() > 1 && text.front() == '#') {
            const auto bare = trim(text.substr(1));
            if (is_identifier(bare)) {
                is_symbol = true;
                symbol = std::string(bare);
                return true;
            }
        }
        return false;
    };

    for (auto& item : items_) {
        auto& bytes = item.section == Section::Text
                          ? result.text
                          : (item.section == Section::Data ? result.data
                                                           : result.exception);
        if (item.kind == ItemKind::Bytes) {
            for (std::size_t index = 0; index < item.operands.size(); ++index) {
                std::int64_t value = 0;
                if (!parse_integer(item.operands[index], value) || value < 0 || value > 255) {
                    error = "line " + std::to_string(item.line) + ": invalid .byte value";
                    return false;
                }
                bytes[item.offset + index] = static_cast<std::uint8_t>(value);
            }
            continue;
        }
        if (item.kind == ItemKind::Word) {
            for (std::size_t index = 0; index < item.operands.size(); ++index) {
                std::int64_t value = 0;
                if (!parse_integer(item.operands[index], value)) {
                    // A symbolic word holds a relocated address: the linker
                    // patches it once the symbol is placed. This is how a
                    // read-only vtable or typeinfo table names its entries.
                    if (!is_identifier(item.operands[index])) {
                        error = "line " + std::to_string(item.line) +
                                ": invalid .word value";
                        return false;
                    }
                    result.relocations.push_back(
                        {static_cast<std::uint32_t>(item.offset + index * 4),
                         item.operands[index],
                         section_index(item.section)});
                    value = 0;
                }
                const auto word = static_cast<std::uint32_t>(value);
                for (std::size_t byte = 0; byte < 4; ++byte) {
                    bytes[item.offset + index * 4 + byte] =
                        static_cast<std::uint8_t>((word >> (byte * 8)) & 0xFF);
                }
            }
            continue;
        }
        if (item.kind == ItemKind::Zero || item.kind == ItemKind::Align ||
            item.kind == ItemKind::Label) {
            continue;
        }
        if (item.kind != ItemKind::Instruction) {
            continue;
        }
        const auto mnemonic = upper(item.mnemonic);
        const auto operands = item.operands;

        std::uint8_t rd = 0;
        if (!operands.empty() && !parse_register(operands[0], rd) && mnemonic != "RET" &&
            mnemonic != "HALT" && mnemonic != "ADJSP" && mnemonic != "JMP" &&
            mnemonic != "CALL" && mnemonic != "SYSCALL" && mnemonic != "WRCR" &&
            mnemonic != "RDCR" &&
            condition_for(mnemonic) == std::nullopt && !fp_function(mnemonic) &&
            !is_tile_operation(mnemonic) &&
            mnemonic.rfind("FLOAD", 0) != 0 && mnemonic.rfind("FSTORE", 0) != 0) {
            error = "line " + std::to_string(item.line) + ": expected a destination register";
            return false;
        }

        std::optional<EncodedInstruction> encoded;

        if (matrix_function(mnemonic)) {
            const auto function =
                static_cast<std::int32_t>(*matrix_function(mnemonic));
            std::uint8_t destination = 0;
            std::uint8_t left = 0;
            std::uint8_t right = 0;
            if (mnemonic == "MDROP") {
                encoded = encode_or_fail(Opcode::MEscape, 0, 0, 0, false, function,
                                         false, 0);
            } else if (mnemonic == "MPUSH" || mnemonic == "MPOP") {
                parse_tile(operands[0], destination);
                encoded = encode_or_fail(Opcode::MEscape, destination, 0, 0, false,
                                         function, false, 0);
            } else {
                if (mnemonic == "VDOT") {
                    parse_fp_register(operands[0], destination);
                } else {
                    parse_tile(operands[0], destination);
                }
                parse_tile(operands[1], left);
                if (operands.size() > 2) {
                    parse_tile(operands[2], right);
                }
                encoded = encode_or_fail(Opcode::MEscape, destination, left, right,
                                         false, function, false, 0);
            }
        } else if (is_tile_transfer(mnemonic)) {
            std::uint8_t tile = 0;
            parse_tile(operands[0], tile);
            MemoryOperand memory;
            if (!parse_memory(operands[1], memory, error)) {
                error = "line " + std::to_string(item.line) + ": " + error;
                return false;
            }
            std::uint32_t extension_word = static_cast<std::uint32_t>(memory.displacement);
            bool extension = memory.has_symbol ||
                             (memory.mode >= 3 && !fits_inline(memory.displacement));
            if (memory.has_symbol) {
                bool external = false;
                std::uint32_t address = 0;
                if (!resolve_symbol(memory.symbol, address, external, error)) {
                    return false;
                }
                extension_word = address;
                static_cast<void>(external);
                result.relocations.push_back(
                    {static_cast<std::uint32_t>(item.offset + 4), memory.symbol,
                     section_index(item.section)});
            }
            if (operands.size() == 3) {
                std::int64_t stride = 0;
                if (!parse_integer(operands[2], stride)) {
                    error = "line " + std::to_string(item.line) + ": invalid row stride";
                    return false;
                }
                extension = true;
                extension_word = static_cast<std::uint32_t>(stride);
            }
            const bool vector = mnemonic == "VLOAD" || mnemonic == "VSTORE";
            const auto opcode = (mnemonic == "MLOAD" || mnemonic == "VLOAD")
                                    ? Opcode::MLoad
                                    : Opcode::MStore;
            encoded = encode_or_fail(opcode, tile,
                                     static_cast<std::uint8_t>(memory.base),
                                     static_cast<std::uint8_t>(memory.mode), vector,
                                     memory.displacement, extension, extension_word);
        } else if (mnemonic == "FLOAD.W" || mnemonic == "FLOAD.D" || mnemonic == "FSTORE.W" ||
            mnemonic == "FSTORE.D") {
            std::uint8_t fp_register = 0;
            if (!parse_fp_register(operands[0], fp_register)) {
                error = "line " + std::to_string(item.line) + ": expected a floating register";
                return false;
            }
            MemoryOperand memory;
            if (!parse_memory(operands[1], memory, error)) {
                error = "line " + std::to_string(item.line) + ": " + error;
                return false;
            }
            std::uint32_t extension_word = static_cast<std::uint32_t>(memory.displacement);
            const bool is_double = mnemonic == "FLOAD.D" || mnemonic == "FSTORE.D";
            bool extension = memory.has_symbol ||
                             (memory.mode >= 3 && !fits_inline(memory.displacement));
            if (memory.has_symbol) {
                bool external = false;
                std::uint32_t address = 0;
                if (!resolve_symbol(memory.symbol, address, external, error)) {
                    return false;
                }
                extension_word = address;
                static_cast<void>(external);
                result.relocations.push_back(
                    {static_cast<std::uint32_t>(item.offset + 4), memory.symbol,
                     section_index(item.section)});
            }
            const auto opcode = (mnemonic.rfind("FLOAD", 0) == 0) ? 0x50 : 0x51;
            encoded = encode_or_fail(
                static_cast<Opcode>(opcode), fp_register,
                static_cast<std::uint8_t>(memory.base),
                static_cast<std::uint8_t>(memory.mode), is_double, memory.displacement,
                extension, extension_word);
        } else if (fp_function(mnemonic)) {
            std::uint8_t first_fp = 0;
            std::uint8_t first_int = 0;
            const bool first_is_fp = parse_fp_register(operands[0], first_fp);
            if (!first_is_fp && !parse_register(operands[0], first_int)) {
                error = "line " + std::to_string(item.line) + ": invalid first FP operand";
                return false;
            }
            std::uint8_t second_fp = 0;
            std::uint8_t second_int = 0;
            const bool second_is_fp = parse_fp_register(operands[1], second_fp);
            if (!second_is_fp && !parse_register(operands[1], second_int)) {
                error = "line " + std::to_string(item.line) + ": invalid second FP operand";
                return false;
            }
            std::uint8_t third_fp = 0;
            if (operands.size() > 2 && !parse_fp_register(operands[2], third_fp)) {
                error = "line " + std::to_string(item.line) + ": invalid third FP operand";
                return false;
            }
            encoded = encode_or_fail(
                static_cast<Opcode>(0x60), first_is_fp ? first_fp : first_int,
                second_is_fp ? second_fp : second_int, third_fp, false,
                static_cast<std::int32_t>(*fp_function(mnemonic)), false, 0);
        } else if (mnemonic == "MOV") {
            std::uint8_t source_reg = 0;
            std::int64_t value = 0;
            bool is_reg = false;
            bool is_sym = false;
            std::string symbol;
            if (!operand_value(operands[1], source_reg, value, is_reg, is_sym, symbol)) {
                error = "line " + std::to_string(item.line) + ": invalid MOV source";
                return false;
            }            bool extension = !is_reg && (!fits_inline(value) || is_sym);
            std::uint32_t extension_word = static_cast<std::uint32_t>(value);
            if (is_sym) {
                bool external = false;
                std::uint32_t address = 0;
                if (!resolve_symbol(symbol, address, external, error)) {
                    return false;
                }
                extension_word = address;
                static_cast<void>(external);
                result.relocations.push_back(
                    {static_cast<std::uint32_t>(item.offset + 4), symbol,
                     section_index(item.section)});
            }
            encoded = encode_or_fail(Opcode::Mov, rd, is_reg ? source_reg : 0, 0, !is_reg,
                                     static_cast<std::int32_t>(value), extension, extension_word);
        } else if (mnemonic == "RET" || mnemonic == "HALT") {
            encoded = encode_or_fail(mnemonic == "RET" ? Opcode::Ret : Opcode::Halt, 0, 0, 0,
                                     false, 0, false, 0);
        } else if (mnemonic == "ADJSP") {
            std::int64_t value = 0;
            if (!parse_integer(operands[0], value)) {
                error = "line " + std::to_string(item.line) + ": invalid ADJSP immediate";
                return false;
            }
            const bool extension = !fits_inline(value);
            encoded = encode_or_fail(Opcode::Adjsp, 0, 0, 0, true,
                                     static_cast<std::int32_t>(value), extension,
                                     static_cast<std::uint32_t>(value));
        } else if (mnemonic == "SYSCALL") {
            std::int64_t value = 0;
            if (!parse_integer(operands[0], value)) {
                error = "line " + std::to_string(item.line) + ": invalid SYSCALL immediate";
                return false;
            }
            const bool extension = !fits_inline(value);
            encoded = encode_or_fail(Opcode::Syscall, 0, 0, 0, true,
                                     static_cast<std::int32_t>(value), extension,
                                     static_cast<std::uint32_t>(value));
        } else if (mnemonic == "PUSH" || mnemonic == "POP") {
            std::uint8_t reg = 0;
            if (!parse_register(operands[0], reg)) {
                error = "line " + std::to_string(item.line) + ": expected a register";
                return false;
            }
            encoded = encode_or_fail(mnemonic == "PUSH" ? Opcode::Push : Opcode::Pop, reg, 0, 0,
                                     false, 0, false, 0);
        } else if (mnemonic == "WRSP") {
            encoded = encode_or_fail(Opcode::Wrsp, rd, 0, 0, false, 0, false, 0);
        } else if (mnemonic == "BRK") {
            encoded = encode_or_fail(Opcode::Brk, 0, 0, 0, false, 0, false, 0);
        } else if (mnemonic == "IRET") {
            encoded = encode_or_fail(Opcode::Iret, 0, 0, 0, false, 0, false, 0);
        } else if (mnemonic == "PUSHR") {
            encoded = encode_or_fail(Opcode::PushR, 0, 0, 0, false, 0, false, 0);
        } else if (mnemonic == "POPR") {
            encoded = encode_or_fail(Opcode::PopR, 0, 0, 0, false, 0, false, 0);
        } else if (mnemonic == "WRCR") {
            std::uint8_t control = 0;
            std::uint8_t data = 0;
            parse_control_register(operands[0], control);
            parse_register(operands[1], data);
            encoded = encode_or_fail(Opcode::WrCr, data, 0, control, false, 0, false, 0);
        } else if (mnemonic == "RDCR") {
            std::uint8_t control = 0;
            std::uint8_t destination = 0;
            parse_register(operands[0], destination);
            parse_control_register(operands[1], control);
            encoded = encode_or_fail(Opcode::RdCr, destination, 0, control, false, 0, false, 0);
        } else if (mnemonic == "SETSP") {
            encoded = encode_or_fail(Opcode::Setsp, 0, rd, 0, false, 0, false, 0);
        } else if (mnemonic == "JMP" || mnemonic == "CALL" || condition_for(mnemonic)) {
            bool external = false;
            std::uint32_t address = 0;
            std::int64_t value = 0;
            std::string symbol;
            std::uint8_t target_register = 0;
            if (parse_register(operands[0], target_register)) {
                if (mnemonic == "JMP" || mnemonic == "CALL") {
                    encoded = encode_or_fail(mnemonic == "JMP" ? Opcode::Jmp : Opcode::Call,
                                             0, target_register, 0, false, 0, false, 0);
                } else {
                    encoded = encode_or_fail(
                        Opcode::Jcond, 0, target_register,
                        static_cast<std::uint8_t>(*condition_for(mnemonic)), false, 0, false,
                        0);
                }
            } else if (mnemonic == "JMP" || mnemonic == "CALL") {
                if (parse_integer(operands[0], value)) {
                    address = static_cast<std::uint32_t>(value);
                } else if (is_identifier(operands[0])) {
                    symbol = std::string(operands[0]);
                    if (!resolve_symbol(symbol, address, external, error)) {
                        return false;
                    }
                } else {
                    error = "line " + std::to_string(item.line) + ": invalid branch target";
                    return false;
                }
                if (!symbol.empty()) {
                    static_cast<void>(external);
                    result.relocations.push_back(
                        {static_cast<std::uint32_t>(item.offset + 4), symbol,
                         section_index(item.section)});
                }
                encoded = encode_or_fail(mnemonic == "JMP" ? Opcode::Jmp : Opcode::Call, 0, 0, 0,
                                         true, 0, true, address);
            } else {
                if (parse_integer(operands[0], value)) {
                    address = static_cast<std::uint32_t>(value);
                } else if (is_identifier(operands[0])) {
                    symbol = std::string(operands[0]);
                    if (!resolve_symbol(symbol, address, external, error)) {
                        return false;
                    }
                } else {
                    error = "line " + std::to_string(item.line) + ": invalid branch target";
                    return false;
                }
                if (!symbol.empty()) {
                    static_cast<void>(external);
                    result.relocations.push_back(
                        {static_cast<std::uint32_t>(item.offset + 4), symbol,
                         section_index(item.section)});
                }
                encoded = encode_or_fail(Opcode::Jcond, 0, 0,
                                         static_cast<std::uint8_t>(*condition_for(mnemonic)),
                                         true, 0, true, address);
            }
        } else if (mnemonic == "LOAD" || mnemonic == "LOAD8U" || mnemonic == "LOAD8S" ||
                   mnemonic == "LOAD16U" || mnemonic == "LOAD16S" ||
                   mnemonic == "STORE" || mnemonic == "STORE8" || mnemonic == "STORE16" ||
                   mnemonic == "LEA") {
            MemoryOperand memory;
            if (!parse_memory(operands[1], memory, error)) {
                error = "line " + std::to_string(item.line) + ": " + error;
                return false;
            }
            std::uint32_t extension_word = static_cast<std::uint32_t>(memory.displacement);
            bool extension = memory.has_symbol || (memory.mode >= 3 && !fits_inline(memory.displacement));
            bool external = false;
            if (memory.has_symbol) {
                std::uint32_t address = 0;
                if (!resolve_symbol(memory.symbol, address, external, error)) {
                    return false;
                }
                extension_word = address;
                static_cast<void>(external);
                result.relocations.push_back(
                    {static_cast<std::uint32_t>(item.offset + 4), memory.symbol,
                     section_index(item.section)});
            }
            auto opcode = opcode_for(mnemonic);
            if (!opcode) {
                error = "line " + std::to_string(item.line) + ": unknown memory opcode";
                return false;
            }
            encoded = encode_or_fail(*opcode, rd, static_cast<std::uint8_t>(memory.base),
                                     static_cast<std::uint8_t>(memory.mode), true,
                                     memory.displacement, extension, extension_word);
            if (encoded && external) {
                encoded->words[1] = 0;
            }
        } else if (mnemonic == "NOT" || mnemonic == "NEG") {
            std::uint8_t source = 0;
            if (!parse_register(operands[1], source)) {
                error = "line " + std::to_string(item.line) + ": expected a source register";
                return false;
            }
            encoded = encode_or_fail(mnemonic == "NOT" ? Opcode::Not : Opcode::Neg, rd, source, 0,
                                     false, 0, false, 0);
        } else {
            auto opcode = opcode_for(mnemonic);
            if (!opcode) {
                error = "line " + std::to_string(item.line) + ": unknown instruction '" +
                        mnemonic + "'";
                return false;
            }
            std::uint8_t rs = 0;
            if (mnemonic == "CMP") {
                if (!parse_register(operands[0], rs)) {
                    error = "line " + std::to_string(item.line) + ": invalid CMP operand";
                    return false;
                }
            } else if (!parse_register(operands[1], rs)) {
                error = "line " + std::to_string(item.line) + ": invalid source register";
                return false;
            }
            std::uint8_t third_reg = 0;
            std::int64_t value = 0;
            const bool third_is_reg = mnemonic != "CMP" && operands.size() > 2 &&
                                      parse_register(operands[2], third_reg);
            const bool second_is_reg = parse_register(operands[mnemonic == "CMP" ? 1 : 2], third_reg);
            if (mnemonic == "CMP") {
                const bool second_reg = operands.size() > 1 && parse_register(operands[1], third_reg);
                const bool second_imm = operands.size() > 1 && parse_integer(operands[1], value);
                if (!second_reg && !second_imm) {
                    error = "line " + std::to_string(item.line) + ": invalid CMP operand";
                    return false;
                }
                const bool extension = !second_reg && !fits_inline(value);
                encoded = encode_or_fail(*opcode, 0, rs,
                                         second_reg ? third_reg : 0, !second_reg,
                                         static_cast<std::int32_t>(value), extension,
                                         static_cast<std::uint32_t>(value));
            } else {
                if (!third_is_reg && !second_is_reg &&
                    !parse_integer(operands[2], value)) {
                    error = "line " + std::to_string(item.line) + ": invalid third operand";
                    return false;
                }
                const bool extension = !(third_is_reg || second_is_reg) && !fits_inline(value);
                encoded = encode_or_fail(*opcode, rd, rs,
                                         (third_is_reg || second_is_reg) ? third_reg : 0,
                                         !(third_is_reg || second_is_reg),
                                         static_cast<std::int32_t>(value), extension,
                                         static_cast<std::uint32_t>(value));
            }
        }

        if (!encoded) {
            error = "line " + std::to_string(item.line) + ": " + error;
            return false;
        }
        if (encoded->count == 0) {
            error = "line " + std::to_string(item.line) + ": empty encoding";
            return false;
        }
        emit_word(bytes, item.offset, encoded->words[0]);
        if (encoded->count == 2) {
            emit_word(bytes, item.offset + 4, encoded->words[1]);
        }
    }

    // The listing is one row per statement, in source order. A row carries the
    // bytes its statement encoded and the source line the nearest preceding
    // `.Ldbg` label named, so a host can render a `.lst` without re-reading the
    // assembly text.
    result.listing.reserve(items_.size());
    std::uint32_t source_line = 0;
    for (const auto& item : items_) {
        if (item.kind == ItemKind::Label && item.name.starts_with(".Ldbg.")) {
            source_line = debug_line_from_label(item.name);
        }
        toolchain::ListingRow row;
        row.section = std::string(section_name(item.section));
        row.address = item.offset;
        row.text = item.text;
        row.source_line = source_line;
        if (item.size != 0) {
            const auto& bytes = item.section == Section::Text
                                    ? result.text
                                    : (item.section == Section::Data ? result.data
                                                                     : result.exception);
            if (item.offset + item.size <= bytes.size()) {
                row.bytes.assign(
                    bytes.begin() + static_cast<std::ptrdiff_t>(item.offset),
                    bytes.begin() +
                        static_cast<std::ptrdiff_t>(item.offset + item.size));
            }
        }
        result.listing.push_back(std::move(row));
    }

    for (const auto& [name, address] : addresses_) {
        std::uint32_t section = 0;
        std::uint64_t offset = address;
        const auto label = labels_.find(name);
        if (label != labels_.end()) {
            section = section_index(label->second);
            for (const auto& item : items_) {
                if (item.kind == ItemKind::Label && item.name == name) {
                    offset = item.offset;
                    break;
                }
            }
        }
        result.symbols.push_back(
            {name, address, global_names_.find(name) != global_names_.end(),
             weak_names_.find(name) != weak_names_.end(), section, offset});
    }
    std::sort(result.symbols.begin(), result.symbols.end(),
              [](const AssembledSymbol& left, const AssembledSymbol& right) {
                  return left.name < right.name;
              });
    return true;
}

bool Assembler::run(std::string_view source, AssemblyResult& result, std::string& error) {
    result = AssemblyResult{};
    if (!parse(source, error)) {
        return false;
    }
    if (!layout(error)) {
        return false;
    }
    return encode_items(result, error);
}

}  // namespace

bool assemble(std::string_view source, AssemblyResult& result, std::string& error) {
    Assembler assembler;
    return assembler.run(source, result, error);
}

}  // namespace krc::asmkr32
