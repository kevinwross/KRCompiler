#include "krc/listing.h"

#include <cstdio>
#include <string>
#include <vector>

namespace krc::listing {
namespace {

std::string hex(std::uint64_t value, int width) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%0*llX", width,
                  static_cast<unsigned long long>(value));
    return buffer;
}

// The encoded bytes as the ISA words them. A trailing partial word is printed as
// its bytes: showing padding would claim bytes the machine does not execute.
std::string encode_bytes(const std::vector<std::uint8_t>& bytes,
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

std::string row_address(const toolchain::ListingRow& row, const Document& document) {
    return hex(row.address + (document.linked ? document.base : 0), 8);
}

}  // namespace

std::string render(const std::vector<toolchain::ListingRow>& rows,
                   const std::vector<toolchain::SymbolInfo>& symbols,
                   const std::vector<toolchain::SectionInfo>& sections,
                   const Document& document) {
    std::string out;

    out += "; krc listing\n";
    out += "; " + document.title + "\n";
    if (!document.target.empty()) {
        out += "; target " + document.target + "\n";
    }
    if (document.linked) {
        out += "; linked image, base 0x" + hex(document.image_base, 8) + "\n";
    }
    out += "\n";

    if (document.linked && !sections.empty()) {
        out += "; sections\n";
        for (const auto& section : sections) {
            out += ";   " + section.name + "  0x" + hex(section.address, 8) +
                   "  size 0x" + hex(section.size, 4) + "\n";
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
        out += row_address(row, document) + "  " +
               encode_bytes(row.bytes, document.word_bytes);
        // Keep the text column aligned whatever the bytes were: the widest row in
        // these ISAs is four words separated by single spaces, and a statement with
        // none still lines up in the same column.
        const std::size_t width = 4 * (document.word_bytes * 2) + 3;
        const std::string encoded = encode_bytes(row.bytes, document.word_bytes);
        if (encoded.size() < width) {
            out += std::string(width - encoded.size(), ' ');
        }
        out += "  " + row.text + "\n";
    }

    if (!symbols.empty()) {
        out += "\n; symbols\n";
        for (const auto& symbol : symbols) {
            out += ";   " + hex(symbol.value + (document.linked ? document.base : 0), 8) +
                   "  " + (symbol.global ? "G" : "L") + "  " + symbol.name;
            if (symbol.size != 0) {
                out += "  size " + std::to_string(symbol.size);
            }
            out += "\n";
        }
    }

    return out;
}

}  // namespace krc::listing
