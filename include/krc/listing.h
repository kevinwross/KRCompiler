// One listing format for every tool that shows encoded statements.
//
// The assemblers already record what each statement became -- a ListingRow per
// statement, with its section, address, encoded bytes and source text -- and the
// linker reports sections and symbols. This renders those into `.lst` text, so a
// byte of the machine has one description rather than one per host.
//
// The renderer is deliberately a free function over data: it reads no files, owns
// no state and writes nothing, which is what lets a host, a test and a drift row
// compare the same bytes.
#ifndef KRC_LISTING_H
#define KRC_LISTING_H

#include <cstdint>
#include <string>
#include <vector>

#include "krc/toolchain.h"

namespace krc::listing {

struct Document {
    // What the listing is of: a source path for an assembly listing, an image
    // name for a linked one.
    std::string title;
    // The image base, added to each row's address when `linked` is set. An
    // assembly listing has no base: its addresses are offsets in their section.
    // Added to each row's address. Zero for a linked listing, whose rows the
    // linker already placed at their final addresses.
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

// Renders a listing. A symbol table is printed after the statements, sections in
// placement order, with the entry named when the document is linked.
[[nodiscard]] std::string render(const std::vector<toolchain::ListingRow>& rows,
                                 const std::vector<toolchain::SymbolInfo>& symbols,
                                 const std::vector<toolchain::SectionInfo>& sections,
                                 const Document& document);

}  // namespace krc::listing

#endif  // KRC_LISTING_H
