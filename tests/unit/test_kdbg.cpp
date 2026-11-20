#include <doctest.h>

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "krc/debug/kdbg.h"
#include "linker/kr16_image.h"
#include "linker/linker.h"

namespace {

std::string debug_bridge() {
    std::string records;
    records += "S 0 main.c";
    records.push_back('\0');
    records += "L start 0 3 1";
    records.push_back('\0');
    records += "F main start end 0 i32";
    records.push_back('\0');
    return records;
}

krc::linker::LinkedSection text_section() {
    krc::linker::LinkedSection text;
    text.name = ".text";
    text.type = krc::linker::KroSectionType::Text;
    text.address = 0x100;
    text.alignment = 2;
    text.data.assign(16, 0x5A);
    text.size = text.data.size();
    return text;
}

krc::linker::LinkedImage debug_image(std::uint32_t target_id) {
    krc::linker::LinkedImage image;
    image.target_id = target_id;
    image.base = target_id == 2 ? 0x100 : 0x10000;
    image.entry = image.base;
    image.sections.push_back(text_section());
    krc::linker::LinkedSection info;
    info.name = ".debug_info";
    info.type = krc::linker::KroSectionType::Debug;
    info.alignment = 1;
    const auto records = debug_bridge();
    info.data.assign(records.begin(), records.end());
    info.size = info.data.size();
    image.sections.push_back(std::move(info));
    image.symbols.push_back(
        {"start", image.base, 16, 0, krc::linker::KroSymbolBinding::Global});
    image.symbols.push_back(
        {"end", image.base + 16, 0, 0, krc::linker::KroSymbolBinding::Global});
    image.symbols.push_back(
        {"main", image.base, 0, 0, krc::linker::KroSymbolBinding::Global});
    return image;
}

std::vector<std::uint8_t> kr16_image_bytes(const krc::linker::LinkedImage& image,
                                           std::string& error) {
    const auto& text = image.sections.front();
    std::vector<krc::linker::LinkedSection> extra;
    for (const auto& section : image.sections) {
        if (section.name != ".text") {
            extra.push_back(section);
        }
    }
    std::vector<std::uint8_t> bytes;
    const auto ok = krc::linker::write_kr16_image(
        text.data, static_cast<std::uint16_t>(image.base),
        static_cast<std::uint16_t>(image.entry), extra, bytes, error);
    return ok ? bytes : std::vector<std::uint8_t>{};
}

}  // namespace

TEST_CASE("KDBG round-trips sources, functions, variables, and types") {
    krc::debug::KdbgImage image;
    image.build_id = 0x1122334455667788ull;
    image.compiler = "krc test";
    image.sources.push_back({0, "hello.c"});
    image.lines.push_back({0x1000, 0, 4, 5});
    image.functions.push_back({"main", 0x1000, 0x1040, 24, "i32"});
    image.symbols.push_back({"main", 0x1000, 0x40, "i32"});
    image.variables.push_back({"main", "value", 8, 4, "i32"});
    krc::debug::KdbgType type;
    type.name = "Pair";
    type.size = 8;
    type.kind = "struct";
    type.members.push_back({"a", 0, 4, "i32"});
    type.members.push_back({"b", 4, 4, "i32"});
    image.types.push_back(type);

    const auto bytes = krc::debug::encode_kdbg(image);
    REQUIRE(bytes.size() > 64);
    CHECK_EQ(bytes[0], 'K');
    CHECK_EQ(bytes[1], 'D');
    CHECK_EQ(bytes[2], 'B');
    CHECK_EQ(bytes[3], 'G');

    krc::debug::KdbgImage decoded;
    std::string error;
    REQUIRE_MESSAGE(krc::debug::decode_kdbg(bytes, decoded, error), error);
    CHECK_EQ(decoded.build_id, image.build_id);
    CHECK_EQ(decoded.compiler, image.compiler);
    REQUIRE_EQ(decoded.sources.size(), 1);
    CHECK_EQ(decoded.sources[0].path, "hello.c");
    REQUIRE_EQ(decoded.functions.size(), 1);
    CHECK_EQ(decoded.functions[0].name, "main");
    CHECK_EQ(decoded.functions[0].frame, 24);
    REQUIRE_EQ(decoded.variables.size(), 1);
    CHECK_EQ(decoded.variables[0].name, "value");
    CHECK_EQ(decoded.variables[0].offset, 8);
    REQUIRE_EQ(decoded.types.size(), 1);
    CHECK_EQ(decoded.types[0].name, "Pair");
    REQUIRE_EQ(decoded.types[0].members.size(), 2);
    CHECK_EQ(decoded.types[0].members[1].offset, 4);

    const auto text = krc::debug::dump_kdbg_text(decoded);
    CHECK(text.find("function main") != std::string::npos);
    CHECK(text.find("type Pair") != std::string::npos);
    const auto json = krc::debug::dump_kdbg_json(decoded);
    CHECK(json.find("\"Pair\"") != std::string::npos);
}

TEST_CASE("KDBG rejects a corrupted checksum") {
    krc::debug::KdbgImage image;
    image.sources.push_back({0, "hello.c"});
    auto bytes = krc::debug::encode_kdbg(image);
    REQUIRE(bytes.size() > 60);
    bytes[60] ^= 0xFF;
    krc::debug::KdbgImage decoded;
    std::string error;
    CHECK_FALSE(krc::debug::decode_kdbg(bytes, decoded, error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("KDBG stores the image base in the reserved header bytes") {
    krc::debug::KdbgImage image;
    image.image_base = 0x1122334455667788ull;
    const auto bytes = krc::debug::encode_kdbg(image);
    REQUIRE(bytes.size() > 64);
    CHECK_EQ(bytes[56], 0x88);
    CHECK_EQ(bytes[57], 0x77);
    CHECK_EQ(bytes[63], 0x11);

    krc::debug::KdbgImage decoded;
    std::string error;
    REQUIRE_MESSAGE(krc::debug::decode_kdbg(bytes, decoded, error), error);
    CHECK_EQ(decoded.image_base, image.image_base);
}

TEST_CASE("A KR16 image carries .kdbg with the base it was built for") {
    auto linked = debug_image(2);
    std::string error;
    REQUIRE_MESSAGE(krc::linker::attach_kdbg(linked, error), error);
    REQUIRE_EQ(linked.sections.back().name, ".kdbg");

    const auto bytes = kr16_image_bytes(linked, error);
    REQUIRE_MESSAGE(!bytes.empty(), error);

    krc::debug::KdbgSection section;
    REQUIRE_MESSAGE(krc::debug::find_kdbg_section(bytes, section, error), error);
    CHECK_EQ(section.container, "kex");
    CHECK_EQ(section.image_base, 0x100);

    krc::debug::KdbgImage decoded;
    REQUIRE_MESSAGE(krc::debug::decode_kdbg_image(bytes, decoded, error), error);
    CHECK_EQ(decoded.image_base, 0x100);
    CHECK_EQ(decoded.target_id, 2);
    REQUIRE_EQ(decoded.sources.size(), 1);
    CHECK_EQ(decoded.sources[0].path, "main.c");
    REQUIRE_EQ(decoded.functions.size(), 1);
    CHECK_EQ(decoded.functions[0].name, "main");
    CHECK_EQ(decoded.functions[0].start, 0x100);
    CHECK_EQ(decoded.functions[0].end, 0x110);
    REQUIRE_EQ(decoded.lines.size(), 1);
    CHECK_EQ(decoded.lines[0].line, 3);
}

TEST_CASE("A KR16 payload built for a different base is rejected") {
    auto linked = debug_image(2);
    std::string error;
    REQUIRE_MESSAGE(krc::linker::attach_kdbg(linked, error), error);

    krc::debug::KdbgImage other;
    other.target_id = 2;
    other.image_base = 0x200;
    other.sources.push_back({0, "main.c"});
    const auto payload = krc::debug::encode_kdbg(other);
    for (auto& section : linked.sections) {
        if (section.name == ".kdbg") {
            section.data = payload;
            section.size = payload.size();
        }
    }

    const auto bytes = kr16_image_bytes(linked, error);
    REQUIRE_MESSAGE(!bytes.empty(), error);
    krc::debug::KdbgImage decoded;
    CHECK_FALSE(krc::debug::decode_kdbg_image(bytes, decoded, error));
    CHECK(error.find("does not match") != std::string::npos);
}

TEST_CASE("A KR32 image carries .kdbg with the base it was built for") {
    auto linked = debug_image(1);
    std::string error;
    REQUIRE_MESSAGE(krc::linker::attach_kdbg(linked, error), error);

    std::vector<std::uint8_t> bytes;
    REQUIRE_MESSAGE(krc::linker::write_kr32_executable(linked, bytes, error), error);

    krc::debug::KdbgSection section;
    REQUIRE_MESSAGE(krc::debug::find_kdbg_section(bytes, section, error), error);
    CHECK_EQ(section.container, "k32");
    CHECK_EQ(section.image_base, 0x10000);

    krc::debug::KdbgImage decoded;
    REQUIRE_MESSAGE(krc::debug::decode_kdbg_image(bytes, decoded, error), error);
    CHECK_EQ(decoded.image_base, 0x10000);
    CHECK_EQ(decoded.target_id, 1);
    REQUIRE_EQ(decoded.functions.size(), 1);
    CHECK_EQ(decoded.functions[0].start, 0x10000);

    krc::debug::KdbgImage other;
    other.image_base = 0x20000;
    other.sources.push_back({0, "main.c"});
    const auto payload = krc::debug::encode_kdbg(other);
    for (auto& linked_section : linked.sections) {
        if (linked_section.name == ".kdbg") {
            linked_section.data = payload;
            linked_section.size = payload.size();
        }
    }
    std::vector<std::uint8_t> mismatched;
    REQUIRE_MESSAGE(krc::linker::write_kr32_executable(linked, mismatched, error),
                    error);
    krc::debug::KdbgImage rejected;
    CHECK_FALSE(krc::debug::decode_kdbg_image(mismatched, rejected, error));
    CHECK(error.find("does not match") != std::string::npos);
}
