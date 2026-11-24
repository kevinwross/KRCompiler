// One listing format for every tool that shows encoded statements.
//
// The assemblers record what each statement became -- a ListingRow per statement,
// with its section, address, encoded bytes and source text -- and the linker reports
// sections and symbols. This renders those into `.lst` text, so a byte of the machine
// has one description rather than one per host.
//
// The renderer is a header, and deliberately so: it reads no files, owns no state and
// writes nothing, and as inline code each consumer compiles it with its own runtime
// rather than linking an archive built against someone else's. A host that ships a
// static-CRT binary can call it without inheriting a dynamic CRT, which is the whole
// reason it is not a library.
#ifndef KRC_LISTING_H
#define KRC_LISTING_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace krc::listing {

// One statement: the section it belongs to, its address in that section, the bytes
// it encoded, and the source text. `source_line` is the source line a preceding
// `.Ldbg` label attributed it to, or 0 when none did.
struct ListingRow {
    std::string section;
    std::uint64_t address = 0;
    std::vector<std::uint8_t> bytes;
    std::string text;
    std::uint32_t source_line = 0;
};

// A section the linker placed. `bytes` is empty for a section that exists only in the
// object file, such as debug information.
struct SectionInfo {
    std::string name;
    std::uint64_t address = 0;
    std::uint64_t size = 0;
    std::uint64_t alignment = 1;
    std::vector<std::uint8_t> bytes;
};

struct SymbolInfo {
    std::string name;
    std::uint64_t value = 0;
    std::uint64_t size = 0;
    bool global = false;
};

struct Document {
    // What the listing is of: a source path for an assembly listing, an image
    // name for a linked one.
    std::string title;
    // Added to each row's address when `linked` is set. An assembly listing has no
    // base: its addresses are offsets in their section.
    std::uint64_t base = 0;
    // What the header says the image was placed at. Separate from `base` because
    // a linked listing's rows are already final: adding the base twice is exactly
    // the bug this field exists to prevent.
    std::uint64_t image_base = 0;
    // Linked listings carry the section table, the placement summary and the
    // entry point; an assembly listing is statements and symbols only.
    bool linked = false;
    // The target the statements were encoded for, printed in the header so a
    // reader knows which ISA the bytes are.
    std::string target;
    // Encode bytes as the ISA's words: kr32 and kr232 in 4-byte words, kr16 in
    // 2-byte words, kr216 in bytes. A row whose bytes are not a whole number of
    // words is printed as bytes rather than padded, because a padded listing
    // would show bytes the machine does not have.
    std::uint32_t word_bytes = 4;
};

namespace detail {

inline std::string hex(std::uint64_t value, int width) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%0*llX", width,
                  static_cast<unsigned long long>(value));
    return buffer;
}

// The encoded bytes as the ISA words them. A trailing partial word is printed as its
// bytes: showing padding would claim bytes the machine does not execute.
inline std::string encode_bytes(const std::vector<std::uint8_t>& bytes,
                                std::uint32_t word_bytes) {
    if (bytes.empty()) {
        return {};
    }
    const std::size_t words = bytes.size() / word_bytes;
    const std::size_t remainder = bytes.size() % word_bytes;
    std::string text;
    for (std::size_t index = 0; index < words; ++index) {
        if (!text.empty()) {
            text += ' ';
        }
        std::uint64_t word = 0;
        for (std::uint32_t byte = 0; byte < word_bytes; ++byte) {
            word = (word << 8) | bytes[index * word_bytes + byte];
        }
        text += hex(word, static_cast<int>(word_bytes * 2));
    }
    if (remainder != 0) {
        if (!text.empty()) {
            text += ' ';
        }
        for (std::size_t index = words * word_bytes; index < bytes.size(); ++index) {
            text += hex(bytes[index], 2);
        }
    }
    return text;
}

}  // namespace detail

// Renders a listing. A symbol table is printed after the statements, sections in
// placement order, with the image base in the header when the document is linked.
[[nodiscard]] inline std::string render(const std::vector<ListingRow>& rows,
                                        const std::vector<SymbolInfo>& symbols,
                                        const std::vector<SectionInfo>& sections,
                                        const Document& document) {
    std::string out;

    out += "; krc listing\n";
    out += "; " + document.title + "\n";
    if (!document.target.empty()) {
        out += "; target " + document.target + "\n";
    }
    if (document.linked) {
        out += "; linked image, base 0x" + detail::hex(document.image_base, 8) + "\n";
    }
    out += "\n";

    if (document.linked && !sections.empty()) {
        out += "; sections\n";
        for (const auto& section : sections) {
            out += ";   " + section.name + "  0x" + detail::hex(section.address, 8) +
                   "  size 0x" + detail::hex(section.size, 4) + "\n";
        }
    }

    // Statements, with a heading whenever the section changes. A label is its own
    // row with no bytes, and it keeps the address of the statement it names.
    std::string section;
    for (const auto& row : rows) {
        if (row.section != section) {
            section = row.section;
            // One blank line before a section heading, whether or not the section
            // table already ended with one.
            if (out.size() < 2 || out.compare(out.size() - 2, 2, "\n\n") != 0) {
                out += "\n";
            }
            out += "; " + section + "\n";
        }
        const std::uint64_t address = row.address + (document.linked ? document.base : 0);
        out += detail::hex(address, 8) + "  " +
               detail::encode_bytes(row.bytes, document.word_bytes);
        // Keep the text column aligned whatever the bytes were: the widest row in
        // these ISAs is four words separated by single spaces, and a statement with
        // none still lines up in the same column.
        const std::size_t width = 4 * (document.word_bytes * 2) + 3;
        const std::string encoded = detail::encode_bytes(row.bytes, document.word_bytes);
        if (encoded.size() < width) {
            out += std::string(width - encoded.size(), ' ');
        }
        out += "  " + row.text + "\n";
    }

    if (!symbols.empty()) {
        out += "\n; symbols\n";
        for (const auto& symbol : symbols) {
            const std::uint64_t value =
                symbol.value + (document.linked ? document.base : 0);
            out += ";   " + detail::hex(value, 8) + "  " +
                   (symbol.global ? "G" : "L") + "  " + symbol.name;
            if (symbol.size != 0) {
                out += "  size " + std::to_string(symbol.size);
            }
            out += "\n";
        }
    }

    return out;
}

}  // namespace krc::listing

namespace krc::toolchain {

// The facade's spelling of the three listing types, so a caller that already says
// `toolchain::ListingRow` keeps compiling while the type itself lives where the
// renderer can reach it without the facade's dependencies.
using listing::ListingRow;
using listing::SectionInfo;
using listing::SymbolInfo;

}  // namespace krc::toolchain

#endif  // KRC_LISTING_H
