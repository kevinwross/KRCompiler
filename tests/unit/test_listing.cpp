// The listing format, one shape for every target.
//
// The expected text below is the specification: a reader can see what a listing
// looks like without running the tool, and the renderer has to produce exactly
// this. Rows are hand-built so the test fails for a format change rather than for
// an assembler change.

#include <string>
#include <vector>

#include "doctest.h"
#include "krc/listing.h"

namespace {

std::vector<krc::toolchain::ListingRow> rows() {
    return {
        {".text", 0, {0x11, 0x22, 0x33, 0x44}, "MOV R1, #0", 0},
        {".text", 4, {}, "loop:", 0},
        {".text", 4, {0x00, 0x00}, "BRK", 0},
    };
}

std::vector<krc::toolchain::SymbolInfo> symbols() {
    krc::toolchain::SymbolInfo entry;
    entry.name = "entry";
    entry.value = 0;
    entry.global = true;
    krc::toolchain::SymbolInfo local;
    local.name = "loop";
    local.value = 4;
    local.size = 2;
    local.global = false;
    return {entry, local};
}

std::vector<krc::toolchain::SectionInfo> sections() {
    krc::toolchain::SectionInfo text;
    text.name = ".text";
    text.address = 0x1000;
    text.size = 8;
    text.alignment = 4;
    text.bytes.assign(8, 0);
    return {text};
}

}  // namespace

TEST_CASE("A listing of assembly prints statements, labels and the symbol table") {
    krc::listing::Document document;
    document.title = "test.s";
    document.target = "kr32";
    document.word_bytes = 4;

    const std::string expected =
        "; krc listing\n"
        "; test.s\n"
        "; target kr32\n"
        "\n"
        "; .text\n"
        "00000000  11223344                             MOV R1, #0\n"
        "00000004                                       loop:\n"
        "00000004  0000                                 BRK\n"
        "\n"
        "; symbols\n"
        ";   00000000  G  entry\n"
        ";   00000004  L  loop  size 2\n";

    CHECK(krc::listing::render(rows(), symbols(), sections(), document) == expected);
}

TEST_CASE("A linked listing adds the base, the section table and the placement") {
    krc::listing::Document document;
    document.title = "test.kr32";
    document.target = "kr32";
    document.word_bytes = 4;
    document.linked = true;
    document.base = 0x1'0000;

    const std::string expected =
        "; krc listing\n"
        "; test.kr32\n"
        "; target kr32\n"
        "; linked image, base 0x00010000\n"
        "\n"
        "; sections\n"
        ";   .text  0x00001000  size 0x0008\n"
        "\n"
        "; .text\n"
        "00010000  11223344                             MOV R1, #0\n"
        "00010004                                       loop:\n"
        "00010004  0000                                 BRK\n"
        "\n"
        "; symbols\n"
        ";   00010000  G  entry\n"
        ";   00010004  L  loop  size 2\n";

    CHECK(krc::listing::render(rows(), symbols(), sections(), document) == expected);
}

TEST_CASE("Encoded bytes are worded as the target words them") {
    krc::listing::Document document;
    document.title = "test.s";
    document.target = "kr16";
    document.word_bytes = 2;

    const std::vector<krc::toolchain::ListingRow> narrow{
        {".text", 0, {0x11, 0x22, 0x33, 0x44}, "MOV R1, #0", 0}};

    const std::string expected =
        "; krc listing\n"
        "; test.s\n"
        "; target kr16\n"
        "\n"
        "; .text\n"
        "00000000  1122 3344            MOV R1, #0\n";

    CHECK(krc::listing::render(narrow, {}, sections(), document) == expected);
}
