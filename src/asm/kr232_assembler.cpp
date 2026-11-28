#include "kr232_assembler.h"

#include <cctype>
#include <cstdlib>
#include <algorithm>
#include <map>
#include <set>
#include <sstream>

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

// A register operand is not a label: `jmp r8` jumps through R8, and reading it as
// a symbol is how an indirect call became an undefined reference.
bool is_register_operand(const std::string& operand) {
    if (operand.size() < 2 || operand[0] != 'r') {
        return false;
    }
    for (std::size_t i = 1; i < operand.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(operand[i])) == 0) {
            return false;
        }
    }
    return true;
}

}  // namespace

namespace {

// Far calls. `call rel16` reaches ±32 KiB and the C runtime's object is larger, so
// a site that lands out of range is emitted as `ld r8, #symbol; call r8` -- the
// ISA's indirect call has no range limit -- and the object is laid out again,
// because the wider form moves everything after it. Sites are identified by their
// ordinal: the instruction sequence is the same on every pass, only the widths
// change.
struct Relaxation {
    std::set<std::size_t> far;                            // ordinals to widen
    std::map<std::uint32_t, std::size_t> site_of_offset;  // relocation -> ordinal
    std::set<std::size_t> too_far;                        // found this pass
    std::size_t next_site = 0;
};

bool assemble_once(std::string_view source, std::string_view file_name,
                   const std::set<std::size_t>& far, Relaxation& relaxation,
                   AssembledProgram& out, std::string& error) {
    out = AssembledProgram{};
    out.program.sections.push_back(Section{".text", {}, 4, 5});
    out.program.sections.push_back(Section{".data", {}, 4, 3});
    out.program.sections.push_back(Section{".debug_info", {}, 1, 8});

    std::map<std::string, std::uint32_t> labels;   // label -> offset in its section
    std::map<std::string, std::uint32_t> label_section;
    std::map<std::string, bool> globals;
    std::map<std::string, bool> weaks;
    std::vector<Fixup> fixups;
    std::vector<std::string> type_records;
    std::vector<std::string> variable_records;
    std::vector<std::string> varinfo_records;
    std::map<std::string, std::pair<std::string, std::uint32_t>> global_records;
    // `.debug_scope` names labels that are resolved once every label is placed, so
    // a scope record is kept whole until the bridge is written.
    struct DebugScopeRecord {
        std::string function;
        std::string name;
        std::string parent;
        std::string start_label;
        std::string end_label;
    };
    std::vector<DebugScopeRecord> scope_records;
    // A `.debug_line <path> <line>` directive introduces a second source file for
    // code generated from an include. The translation unit itself is source 1;
    // extra files take 2, 3, ... in first-seen order.
    std::vector<std::string> extra_sources;
    std::uint32_t current_source = 1;
    std::uint32_t data_offset = 0;
    bool in_data = false;
    // `.debug_line N` sets the source line the following instructions belong to,
    // and `.debug_line 0` says the following code has no source line at all: no L
    // record is then written for it. Until the first directive is seen (a
    // hand-written `.s`), the record falls back to the assembly text's own line
    // number, which is the source line for an assembly input.
    std::uint32_t debug_line = 0;
    bool debug_line_seen = false;

    std::size_t line_number = 0;
    // One line record for the instruction at `offset`. A directive in force names
    // the directive's source line and file; `.debug_line 0` suppresses the record
    // instead of falling back; with no directive yet (hand-written assembly) the
    // assembly text's physical line is the source line.
    const auto record_line = [&](std::uint32_t offset) {
        if (debug_line_seen && debug_line == 0) {
            return;
        }
        out.lines.push_back(
            DebugLine{offset,
                      debug_line_seen ? debug_line
                                      : static_cast<std::uint32_t>(line_number),
                      debug_line_seen ? current_source : 1u,
                      std::string(file_name)});
    };
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
        if (line.rfind(".weak", 0) == 0) {
            const std::string name = trim(line.substr(5));
            globals[name] = true;
            weaks[name] = true;
            continue;
        }
        // A variable record for the debug bridge: `V <function> <name> <offset>
        // <type> <size>`. The assembler passes it through; what a local's offset
        // means is the backend's contract with the debugger.
        if (line.rfind(".debug_var ", 0) == 0) {
            variable_records.push_back("V " + trim(line.substr(11)));
            continue;
        }
        // `W <function> <name> <kind> <scope> <flags>`: parameter/local kinds and
        // the scope a variable belongs to. krdbg reads the kind but not the flags.
        if (line.rfind(".debug_varinfo ", 0) == 0) {
            varinfo_records.push_back("W " + trim(line.substr(15)));
            continue;
        }
        // `K <function> <name> <parent> <start-label> <end-label>`: a lexical scope.
        // The function field is the symbol, matching V and W, so a consumer can join
        // the scope to its function; krdbg ignores K today.
        if (line.rfind(".debug_scope ", 0) == 0) {
            std::istringstream stream{trim(line.substr(13))};
            DebugScopeRecord record;
            stream >> record.function >> record.name >> record.parent >>
                record.start_label >> record.end_label;
            if (record.function.empty() || record.name.empty() ||
                record.start_label.empty() || record.end_label.empty()) {
                error = "line " + std::to_string(line_number) +
                        ": .debug_scope needs function, name, parent and two labels";
                return false;
            }
            scope_records.push_back(std::move(record));
            continue;
        }
        // Generated code names both the source file and the line:
        // `.debug_line <path> <line>`; a hand-written `.s` may use the single
        // `.debug_line <line>` form, which stays on the translation unit's own
        // source record, and `0` for either means "no source line": no L record is
        // written until the next directive. The path is one whitespace-free token,
        // like the S record it becomes.
        if (line.rfind(".debug_line ", 0) == 0) {
            const std::string rest = trim(line.substr(12));
            const std::size_t space = rest.find(' ');
            if (space == std::string::npos) {
                debug_line = static_cast<std::uint32_t>(
                    std::strtoul(rest.c_str(), nullptr, 10));
                current_source = 1;
            } else {
                const std::string path = rest.substr(0, space);
                debug_line = static_cast<std::uint32_t>(std::strtoul(
                    trim(rest.substr(space + 1)).c_str(), nullptr, 10));
                if (path.empty() || path == file_name) {
                    current_source = 1;
                } else {
                    const auto found =
                        std::find(extra_sources.begin(), extra_sources.end(), path);
                    if (found == extra_sources.end()) {
                        current_source = 2u + static_cast<std::uint32_t>(
                                                  extra_sources.size());
                        extra_sources.push_back(path);
                    } else {
                        current_source = 2u + static_cast<std::uint32_t>(
                                                  found - extra_sources.begin());
                    }
                }
            }
            debug_line_seen = true;
            continue;
        }
        if (line.rfind(".debug_type ", 0) == 0) {
            type_records.push_back(trim(line.substr(12)));
            continue;
        }
        if (line.rfind(".debug_global ", 0) == 0) {
            std::istringstream stream{trim(line.substr(14))};
            std::string name;
            std::string type;
            std::uint32_t size = 0;
            stream >> name >> type >> size;
            if (!name.empty()) {
                global_records[name] = {type, size};
            }
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
                    const bool numeric = stop != nullptr && *stop == '\0';
                    // A word may name a symbol: that is what a vtable element is, and
                    // the linker fills its address through an absolute relocation.
                    if (!numeric && word && is_label_operand(piece)) {
                        std::vector<std::uint8_t>& words = out.program.sections[1].bytes;
                        const std::uint32_t field = static_cast<std::uint32_t>(words.size());
                        for (unsigned i = 0; i < 4; ++i) {
                            words.push_back(0);
                        }
                        Program::Relocation relocation;
                        relocation.section = 1;
                        relocation.offset = field;
                        relocation.symbol = piece;
                        relocation.absolute = true;
                        out.program.relocations.push_back(std::move(relocation));
                        data_offset += 4;
                        if (comma == std::string::npos) {
                            break;
                        }
                        start = comma + 1;
                        continue;
                    }
                    if (!numeric) {
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
            if (is_label_operand(operand) && !is_register_operand(operand)) {
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
                    const std::size_t site = relaxation.next_site++;
                    if (mnemonic == "call" && far.count(site) != 0) {
                        // ld r8, #symbol  then  call r8: the address is absolute, so
                        // no distance is involved.
                        out.program.sections[0].bytes.push_back(0x58);
                        for (int byte = 0; byte < 4; ++byte) {
                            out.program.sections[0].bytes.push_back(0);
                        }
                        Program::Relocation wide;
                        wide.offset = offset + 1;
                        wide.symbol = operand;
                        wide.absolute = true;
                        wide.local_only = false;
                        out.program.relocations.push_back(std::move(wide));
                        out.program.sections[0].bytes.push_back(0x48);  // call r8
                        relaxation.site_of_offset[offset + 1] = site;
                        record_line(offset);
                        continue;
                    }
                    out.program.sections[0].bytes.push_back(mnemonic == "call" ? 0xD0 : 0xCF);
                    out.program.sections[0].bytes.push_back(0);
                    out.program.sections[0].bytes.push_back(0);
                    Program::Relocation relocation;
                    relocation.offset = offset + 1;
                    relocation.symbol = operand;
                    relocation.addend = -2;  // relative to the next instruction
                relocation.local_only = mnemonic != "call" && mnemonic != "jmp";
                    relaxation.site_of_offset[offset + 1] = site;
                    out.program.relocations.push_back(std::move(relocation));
                    record_line(offset);
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
                // A label branch always uses the rel16 form, which the linker can
                // reach at any distance. Only `bra`, `jmp` and `call` have that
                // form directly; a conditional branch reaches it through a skip
                // over the `bra`, because its own form is nine bits and a spilled
                // function is easily longer than that.
                const std::uint32_t offset =
                    static_cast<std::uint32_t>(out.program.sections[0].bytes.size());
                if (mnemonic == "bra" || mnemonic == "call" || mnemonic == "jmp") {
                    out.program.sections[0].bytes.push_back(mnemonic == "call" ? 0xD0 : 0xCF);
                } else {
                    const unsigned inverse = code ^ 1u;  // eq/ne, cs/cc, mi/pl, ...
                    const auto skip = encode_compact_branch(inverse, 3, encode_error);
                    if (!skip.has_value()) {
                        error = encode_error;
                        return false;
                    }
                    out.program.sections[0].bytes.push_back(skip->bytes[0]);
                    out.program.sections[0].bytes.push_back(skip->bytes[1]);
                    out.program.sections[0].bytes.push_back(0xCF);
                }
                out.program.sections[0].bytes.push_back(0);
                out.program.sections[0].bytes.push_back(0);
                Program::Relocation relocation;
                relocation.offset = static_cast<std::uint32_t>(
                    out.program.sections[0].bytes.size()) - 2u;
                relocation.symbol = operand;
                relocation.addend = -2;  // relative to the next instruction
                relocation.local_only = mnemonic != "call" && mnemonic != "jmp";
                out.program.relocations.push_back(std::move(relocation));
                record_line(offset);
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
                    // The wide form, because the relocation writes four bytes at the
                    // immediate and the encoder would otherwise pick a short form.
                    effective = line.substr(0, hash + 1) + "0x80000000";
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
        record_line(offset);
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
            if (relocation.local_only) {
                error = "undefined label '" + relocation.symbol + "'";
                return false;
            }
            unresolved.push_back(relocation);
            continue;
        }
        const auto displacement = static_cast<std::int64_t>(found->second) -
                                  static_cast<std::int64_t>(relocation.offset + 2);
        if (displacement < -32768 || displacement > 32767) {
            const auto site = relaxation.site_of_offset.find(relocation.offset);
            if (site != relaxation.site_of_offset.end()) {
                // Widen it on the next pass rather than refuse: only a call can be
                // widened, and only a call reaches this far in practice.
                relaxation.too_far.insert(site->second);
                continue;
            }
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

    // A symbol's size is the distance to the next symbol in its section, so a
    // function assembled from `.s` carries a span a debugger can bound instead of
    // a zero-length one.
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint64_t> symbol_sizes;
    {
        std::vector<std::pair<std::uint32_t, std::uint32_t>> starts;
        for (const auto& [name, offset] : labels) {
            starts.emplace_back(label_section[name], offset);
        }
        std::sort(starts.begin(), starts.end());
        for (std::size_t i = 0; i < starts.size(); ++i) {
            const std::uint32_t section = starts[i].first;
            const std::uint32_t start = starts[i].second;
            std::uint32_t end =
                section == 0 ? static_cast<std::uint32_t>(out.program.sections[0].bytes.size())
                             : static_cast<std::uint32_t>(data_offset);
            for (std::size_t j = i + 1; j < starts.size(); ++j) {
                if (starts[j].first == section) {
                    end = starts[j].second;
                    break;
                }
            }
            symbol_sizes[{section, start}] = end > start ? end - start : 0;
        }
    }
    // Line labels must be unique per source file. The linker merges every object's
    // bridge into one section and resolves a label by name, so two objects that
    // both call their first line `.L0` make each other's lines resolve to the wrong
    // address -- which showed up as an empty function span in a two-object image.
    std::uint32_t file_hash = 2166136261u;
    for (const char character : std::string(source)) {  // the text, not the name: every C input is assembled as "assembly"
        // A carriage return is a checkout artefact, not source: hashing it made the
        // same file assemble to different label prefixes on Windows (CRLF) and
        // Linux (LF), so an image was not reproducible across hosts.
        if (character == '\r') {
            continue;
        }
        file_hash = (file_hash ^ static_cast<std::uint8_t>(character)) * 16777619u;
    }
    std::ostringstream prefix_stream;
    prefix_stream << ".L" << std::hex << file_hash << "_";
    const std::string label_prefix = prefix_stream.str();
    const std::string end_label = label_prefix + "end";
    const std::uint32_t text_size =
        static_cast<std::uint32_t>(out.program.sections[0].bytes.size());

    // Functions are the text labels; data labels become sized globals. Every
    // global used to be an F record, which made `points[2]` look like a function
    // spanning the whole text and left the globals list empty.
    std::vector<std::pair<std::uint32_t, std::string>> text_labels;
    for (const auto& [name, offset] : labels) {
        // Only named functions bound a span: a `.Lbb` block label inside one would
        // end it early, and every line after the block would lose its function.
        if (label_section[name] == 0 && !name.empty() && name.front() != '.') {
            text_labels.emplace_back(offset, name);
        }
    }
    std::sort(text_labels.begin(), text_labels.end());

    // Symbols: every parsed label, then the generated line and end labels the
    // bridge names.
    for (const auto& [name, offset] : labels) {
        Symbol symbol;
        symbol.name = name;
        symbol.section = label_section[name];
        symbol.offset = offset;
        symbol.size = symbol_sizes[{symbol.section, static_cast<std::uint32_t>(offset)}];
        symbol.global = globals.count(name) != 0;
        symbol.weak = weaks.count(name) != 0;
        out.program.symbols.push_back(std::move(symbol));
    }
    out.program.symbols.push_back(Symbol{end_label, 0, text_size, false});
    for (std::size_t i = 0; i < out.lines.size(); ++i) {
        out.program.symbols.push_back(
            Symbol{label_prefix + std::to_string(i), 0, out.lines[i].offset, false});
    }

    // The `.debug_info` bridge: NUL-terminated records `attach_kdbg` converts into
    // a `.kdbg`. Each line record names a label at the instruction's offset, so a
    // synthetic local symbol is emitted for every line.
    std::vector<std::uint8_t>& bridge = out.program.sections[2].bytes;
    const auto emit = [&](const std::string& record) {
        bridge.insert(bridge.end(), record.begin(), record.end());
        bridge.push_back(0);
    };
    emit("S 1 " + std::string(file_name));
    // Extra sources come before any L record: the linker remaps an L record's
    // source id while it parses, and the mapping exists only once the S record has
    // been seen. The linker gives each distinct path one image-wide id.
    for (std::size_t index = 0; index < extra_sources.size(); ++index) {
        emit("S " + std::to_string(index + 2) + " " + extra_sources[index]);
    }
    for (const std::string& record : type_records) {
        emit(record);
    }
    for (std::size_t i = 0; i < out.lines.size(); ++i) {
        const DebugLine& line = out.lines[i];
        emit("L " + label_prefix + std::to_string(i) + " " +
             std::to_string(line.source) + " " + std::to_string(line.line) + " 0");
    }
    // F and G are synthesised from the `.globl`/`.weak` labels: task-13 makes every
    // function (including internal-linkage ones) carry an F record from a backend
    // directive. Until then this is the hand-written `.s` path and the external
    // function path. The end label must be a *global* name: the linker resolves a
    // bridge label through one name-keyed map where the first definition wins, so a
    // local or weak label that another object also defines can resolve to that
    // object's address and produce a reversed range (measured on startrekng: a
    // global function followed by a weak inline destructor). Falling forward to the
    // next global text label, or the per-object text end, keeps every synthesized
    // range well-formed.
    for (std::size_t index = 0; index < text_labels.size(); ++index) {
        const std::string& name = text_labels[index].second;
        if (globals.count(name) == 0) {
            continue;
        }
        std::string end = end_label;
        for (std::size_t next = index + 1; next < text_labels.size(); ++next) {
            const std::string& candidate = text_labels[next].second;
            if (text_labels[next].first <= text_labels[index].first) {
                continue;
            }
            // A weak name is defined by several objects and resolves to the first
            // definition, so it cannot bound this object's function.
            if (globals.count(candidate) != 0 && weaks.count(candidate) == 0) {
                end = candidate;
                break;
            }
        }
        emit("F " + name + " " + name + " " + end + " 0 0");
    }
    // One scope per function, after its end label. The function field is the
    // symbol, matching V and W so a consumer can join the records; krdbg ignores K
    // today.
    for (const DebugScopeRecord& record : scope_records) {
        emit("K " + record.function + " " + record.name + " " + record.parent + " " +
             record.start_label + " " + record.end_label);
    }
    for (const std::string& record : varinfo_records) {
        emit(record);
    }
    for (const std::string& record : variable_records) {
        emit(record);
    }
    for (const auto& [name, offset] : labels) {
        if (globals.count(name) == 0 || label_section[name] == 0) {
            continue;
        }
        const auto found = global_records.find(name);
        const std::string type =
            found != global_records.end() ? found->second.first : "0";
        const std::uint32_t size =
            found != global_records.end()
                ? found->second.second
                : static_cast<std::uint32_t>(
                      symbol_sizes[{label_section[name], offset}]);
        emit("G " + name + " " + name + " " + type + " " +
             std::to_string(size));
    }
    if (bridge.empty()) {
        emit("S 1 .");
    }
    return true;
}

}  // namespace

// The public entry: lay the file out, widen whatever landed out of range, and lay
// it out again. Six passes is far more than a real file needs -- one is typical,
// because the first pass finds every far site at once -- and bounds a case where
// widening one site pushes another out.
bool assemble(std::string_view source, std::string_view file_name, AssembledProgram& out,
              std::string& error, bool far_calls) {
    std::set<std::size_t> far;
    for (int pass = 0; pass < 6; ++pass) {
        Relaxation relaxation;
        AssembledProgram candidate;
        std::string pass_error;
        if (!assemble_once(source, file_name, far, relaxation, candidate, pass_error)) {
            error = pass_error;
            return false;
        }
        if (far_calls && pass == 0) {
            // Every call site in the file, so no relocation leaves for the linker
            // to overflow.
            for (std::size_t site = 0; site < relaxation.next_site; ++site) {
                far.insert(site);
            }
            continue;
        }
        if (relaxation.too_far.empty()) {
            out = std::move(candidate);
            // The listing is built here because this is the only place the source and
            // the final offsets are both in hand: `lines` gives each statement's
            // offset, the next statement's offset ends its bytes, and the source line
            // supplies the text.
            const auto source_lines = [&]() {
                std::vector<std::string_view> text;
                std::size_t start = 0;
                for (std::size_t index = 0; index <= source.size(); ++index) {
                    if (index == source.size() || source[index] == '\n') {
                        text.push_back(source.substr(start, index - start));
                        start = index + 1;
                    }
                }
                return text;
            }();
            const auto& text_bytes = out.program.sections.empty()
                                         ? std::vector<std::uint8_t>{}
                                         : out.program.sections[0].bytes;
            out.listing.reserve(out.lines.size());
            for (std::size_t index = 0; index < out.lines.size(); ++index) {
                const auto& entry = out.lines[index];
                const std::uint32_t end = index + 1 < out.lines.size()
                                              ? out.lines[index + 1].offset
                                              : static_cast<std::uint32_t>(text_bytes.size());
                toolchain::ListingRow row;
                row.section = ".text";
                row.address = entry.offset;
                if (entry.line >= 1 && entry.line <= source_lines.size()) {
                    row.text = std::string(source_lines[entry.line - 1]);
                }
                row.source_line = entry.line;
                if (end > entry.offset && end <= text_bytes.size()) {
                    row.bytes.assign(
                        text_bytes.begin() + static_cast<std::ptrdiff_t>(entry.offset),
                        text_bytes.begin() + static_cast<std::ptrdiff_t>(end));
                }
                out.listing.push_back(std::move(row));
            }
            return true;
        }
        far.insert(relaxation.too_far.begin(), relaxation.too_far.end());
    }
    error = "a call is out of range and could not be widened";
    return false;
}
}  // namespace krc::asm_kr232
