#include "kr232_assembler.h"

#include <cctype>
#include <cstdlib>
#include <map>

#include "asm/kr232_parser.h"

namespace krc::asm_kr232 {
namespace {

std::string trim(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

// The `#` prefix belongs to immediate operands, so a comment starts at `;`.
std::string strip_comment(const std::string& line) {
    const std::size_t at = line.find(';');
    return at == std::string::npos ? line : line.substr(0, at);
}

bool parse_label(const std::string& line, std::string& label) {
    const std::size_t colon = line.find(':');
    if (colon == std::string::npos) {
        return false;
    }
    if (!line.substr(colon + 1).empty()) {
        return false;
    }
    label = trim(line.substr(0, colon));
    return !label.empty();
}

struct Fixup {
    std::uint32_t offset = 0;  // of the two-byte branch in .text
    std::string label;
    unsigned line = 0;
};

bool is_label_operand(const std::string& operand) {
    if (operand.empty()) {
        return false;
    }
    const char first = operand[0];
    // A leading '.' is a local label: the backend names its blocks `.LbbN` and its
    // comparison tails `.LcmpN`, and a parser that rejected those would refuse
    // every generated branch.
    return std::isalpha(static_cast<unsigned char>(first)) != 0 || first == '_' || first == '.';
}

}  // namespace

bool assemble(std::string_view source, std::string_view file_name, AssembledProgram& out,
              std::string& error) {
    out = AssembledProgram{};
    out.program.sections.push_back(Section{".text", {}, 4, 5});
    out.program.sections.push_back(Section{".data", {}, 4, 3});
    out.program.sections.push_back(Section{".debug_info", {}, 1, 8});

    std::map<std::string, std::uint32_t> labels;   // label -> offset in its section
    std::map<std::string, std::uint32_t> label_section;
    std::map<std::string, bool> globals;
    std::vector<Fixup> fixups;
    std::uint32_t data_offset = 0;
    bool in_data = false;

    std::size_t line_number = 0;
    std::size_t cursor = 0;
    while (cursor <= source.size()) {
        const std::size_t end = source.find('\n', cursor);
        const std::string raw(source.substr(cursor, end == std::string_view::npos
                                                        ? std::string_view::npos
                                                        : end - cursor));
        cursor = end == std::string_view::npos ? source.size() + 1 : end + 1;
        ++line_number;
        const std::string line = trim(strip_comment(raw));
        if (line.empty()) {
            continue;
        }
        if (line.rfind(".text", 0) == 0) {
            in_data = false;
            continue;
        }
        if (line.rfind(".data", 0) == 0) {
            in_data = true;
            continue;
        }
        if (line.rfind(".globl", 0) == 0) {
            globals[trim(line.substr(6))] = true;
            continue;
        }
        if (line.rfind(".word", 0) == 0 || line.rfind(".byte", 0) == 0) {
            const bool word = line.rfind(".word", 0) == 0;
            const std::string rest = trim(line.substr(word ? 5 : 5));
            std::size_t start = 0;
            while (start <= rest.size()) {
                const std::size_t comma = rest.find(',', start);
                const std::string piece =
                    trim(rest.substr(start, comma == std::string::npos ? std::string::npos
                                                                       : comma - start));
                if (!piece.empty()) {
                    char* stop = nullptr;
                    const unsigned long value = std::strtoul(piece.c_str(), &stop, 0);
                    if (stop == nullptr || *stop != '\0') {
                        error = "line " + std::to_string(line_number) + ": bad data value '" +
                                piece + "'";
                        return false;
                    }
                    std::vector<std::uint8_t>& bytes = out.program.sections[1].bytes;
                    const unsigned width = word ? 4 : 1;
                    for (unsigned i = 0; i < width; ++i) {
                        bytes.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF));
                    }
                    data_offset += width;
                }
                if (comma == std::string::npos) {
                    break;
                }
                start = comma + 1;
            }
            continue;
        }
        std::string label;
        if (parse_label(line, label)) {
            if (in_data) {
                labels[label] = data_offset;
                label_section[label] = 1;
            } else {
                labels[label] = static_cast<std::uint32_t>(out.program.sections[0].bytes.size());
                label_section[label] = 0;
            }
            continue;
        }
        if (line[0] == '.') {
            error = "line " + std::to_string(line_number) + ": unknown directive '" + line + "'";
            return false;
        }

        // A label operand on a branch becomes a compact branch with a placeholder.
        const std::size_t space = line.find(' ');
        const std::string mnemonic = line.substr(0, space);
        const bool is_branch = mnemonic == "bra" || mnemonic == "jmp" || mnemonic == "call" ||
                               (mnemonic.size() == 3 && mnemonic[0] == 'b');
        if (is_branch && space != std::string::npos) {
            const std::string operand = trim(line.substr(space + 1));
            if (is_label_operand(operand)) {
                if (in_data) {
                    error = "line " + std::to_string(line_number) +
                            ": an instruction cannot appear in .data";
                    return false;
                }
                // `call` and `jmp` to a label use the three-byte rel16 form plus a
                // relocation: the label may live in another object, and a fixed
                // length lets the assembler emit the bytes before it knows.
                if (mnemonic == "call" || mnemonic == "jmp") {
                    const std::uint32_t offset =
                        static_cast<std::uint32_t>(out.program.sections[0].bytes.size());
                    out.program.sections[0].bytes.push_back(mnemonic == "call" ? 0xD0 : 0xCF);
                    out.program.sections[0].bytes.push_back(0);
                    out.program.sections[0].bytes.push_back(0);
                    Program::Relocation relocation;
                    relocation.offset = offset + 1;
                    relocation.symbol = operand;
                    relocation.addend = -2;  // relative to the next instruction
                    out.program.relocations.push_back(std::move(relocation));
                    out.lines.push_back(DebugLine{offset, static_cast<std::uint32_t>(line_number),
                                                  std::string(file_name)});
                    continue;
                }
                std::string encode_error;
                const unsigned cond = mnemonic == "bra" ? 15 : (mnemonic == "call" ? 14 : 0);
                unsigned code = cond;
                if (mnemonic != "bra" && mnemonic != "call" && mnemonic != "jmp") {
                    if (mnemonic == "beq") {
                        code = 0;
                    } else if (mnemonic == "bne") {
                        code = 1;
                    } else if (mnemonic == "bcs") {
                        code = 2;
                    } else if (mnemonic == "bcc") {
                        code = 3;
                    } else if (mnemonic == "bmi") {
                        code = 4;
                    } else if (mnemonic == "bpl") {
                        code = 5;
                    } else if (mnemonic == "bvs") {
                        code = 6;
                    } else if (mnemonic == "bvc") {
                        code = 7;
                    } else if (mnemonic == "bhi") {
                        code = 8;
                    } else if (mnemonic == "bls") {
                        code = 9;
                    } else if (mnemonic == "bge") {
                        code = 10;
                    } else if (mnemonic == "blt") {
                        code = 11;
                    } else if (mnemonic == "bgt") {
                        code = 12;
                    } else if (mnemonic == "ble") {
                        code = 13;
                    } else {
                        error = "line " + std::to_string(line_number) +
                                ": label targets support bra, call and the common conditions";
                        return false;
                    }
                }
                const auto encoded = encode_compact_branch(code, 0, encode_error);
                if (!encoded.has_value()) {
                    error = encode_error;
                    return false;
                }
                const std::uint32_t offset = static_cast<std::uint32_t>(
                    out.program.sections[0].bytes.size());
                out.program.sections[0].bytes.push_back(encoded->bytes[0]);
                out.program.sections[0].bytes.push_back(encoded->bytes[1]);
                fixups.push_back(Fixup{offset, operand, static_cast<unsigned>(line_number)});
                out.lines.push_back(DebugLine{offset, static_cast<std::uint32_t>(line_number),
                                              std::string(file_name)});
                continue;
            }
        }

        std::string encode_error;
        // An immediate that names a symbol instead of a number is the address of
        // something in `.data`: it encodes as zero and carries an absolute
        // relocation the linker fills with the symbol's address.
        std::string symbol_immediate;
        std::string effective = line;
        {
            const auto hash = line.find('#');
            if (hash != std::string::npos) {
                const std::string token = trim(line.substr(hash + 1));
                const bool numeric = !token.empty() &&
                                     (std::isdigit(static_cast<unsigned char>(token[0])) != 0 ||
                                      token[0] == '-' || token[0] == '+');
                if (!token.empty() && !numeric) {
                    symbol_immediate = token;
                    effective = line.substr(0, hash + 1) + "0";
                }
            }
        }
        const auto encoded = parse_instruction(effective, encode_error);
        if (!encoded.has_value()) {
            error = "line " + std::to_string(line_number) + ": " + encode_error;
            return false;
        }
        if (in_data) {
            error = "line " + std::to_string(line_number) +
                    ": an instruction cannot appear in .data";
            return false;
        }
        const std::uint32_t offset =
            static_cast<std::uint32_t>(out.program.sections[0].bytes.size());
        for (unsigned i = 0; i < encoded->length; ++i) {
            out.program.sections[0].bytes.push_back(encoded->bytes[i]);
        }
        if (!symbol_immediate.empty()) {
            Program::Relocation relocation;
            relocation.offset = offset + 1;
            relocation.symbol = symbol_immediate;
            relocation.absolute = true;
            out.program.relocations.push_back(std::move(relocation));
        }
        out.lines.push_back(
            DebugLine{offset, static_cast<std::uint32_t>(line_number), std::string(file_name)});
    }

    // Patch the label branches once every label has an offset.
    for (const Fixup& fixup : fixups) {
        const auto found = labels.find(fixup.label);
        if (found == labels.end()) {
            error = "line " + std::to_string(fixup.line) + ": undefined label '" + fixup.label +
                    "'";
            return false;
        }
        const auto displacement = static_cast<std::int64_t>(found->second) -
                                  static_cast<std::int64_t>(fixup.offset + 2);
        if (displacement < -256 || displacement > 255) {
            error = "line " + std::to_string(fixup.line) + ": '" + fixup.label +
                    "' is out of the nine-bit branch range";
            return false;
        }
        const auto bits = static_cast<std::uint16_t>(displacement & 0x1FF);
        const std::uint8_t condition =
            static_cast<std::uint8_t>((out.program.sections[0].bytes[fixup.offset] >> 1) & 0x0F);
        out.program.sections[0].bytes[fixup.offset] =
            static_cast<std::uint8_t>(0xA0u | (condition << 1) | ((bits >> 8) & 1u));
        out.program.sections[0].bytes[fixup.offset + 1] = static_cast<std::uint8_t>(bits & 0xFF);
    }

    // A call or jmp to a label defined in this same object is patched here: the
    // linker is for what this file cannot see, and a relocation left for a local
    // symbol is a width the linker cannot verify.
    std::vector<Program::Relocation> unresolved;
    for (const Program::Relocation& relocation : out.program.relocations) {
        const auto found = relocation.absolute ? labels.end() : labels.find(relocation.symbol);
        if (found == labels.end()) {
            unresolved.push_back(relocation);
            continue;
        }
        const auto displacement = static_cast<std::int64_t>(found->second) -
                                  static_cast<std::int64_t>(relocation.offset + 2);
        if (displacement < -32768 || displacement > 32767) {
            error = "'" + relocation.symbol + "' is out of the 16-bit branch range";
            return false;
        }
        const auto value = static_cast<std::uint16_t>(displacement & 0xFFFF);
        out.program.sections[0].bytes[relocation.offset] =
            static_cast<std::uint8_t>(value & 0xFF);
        out.program.sections[0].bytes[relocation.offset + 1] =
            static_cast<std::uint8_t>((value >> 8) & 0xFF);
    }
    out.program.relocations = std::move(unresolved);

    for (const auto& [name, offset] : labels) {
        const bool global = globals.count(name) != 0;
        out.program.symbols.push_back(
            Symbol{name, label_section[name], offset, global});
    }

    // The `.debug_info` bridge: NUL-terminated records `attach_kdbg` converts
    // into a `.kdbg`. Each line record names a label at the instruction's offset,
    // so a synthetic local symbol is emitted for every line.
    std::vector<std::uint8_t>& bridge = out.program.sections[2].bytes;
    const auto emit = [&](const std::string& record) {
        bridge.insert(bridge.end(), record.begin(), record.end());
        bridge.push_back(0);
    };
    emit("S 1 " + std::string(file_name));
    const std::uint32_t text_size = static_cast<std::uint32_t>(out.program.sections[0].bytes.size());
    out.program.symbols.push_back(Symbol{".Lend", 0, text_size, false});  // a symbol only, not a line
    for (std::size_t i = 0; i < out.lines.size(); ++i) {
        const DebugLine& line = out.lines[i];
        const std::string label = ".L" + std::to_string(i);
        out.program.symbols.push_back(Symbol{label, 0, line.offset, false});
        emit("L " + label + " 1 " + std::to_string(line.line) + " 0");
    }
    for (const auto& [name, offset] : labels) {
        if (globals.count(name) == 0) {
            continue;
        }
        emit("F " + name + " " + name + " .Lend 0 0");
        (void)offset;
    }
    if (bridge.empty()) {
        emit("S 1 .");
    }
    return true;
}

}  // namespace krc::asm_kr232
