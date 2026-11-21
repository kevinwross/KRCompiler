#include <doctest.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "krc/debug/kdbg.h"
#include "krc/toolchain.h"
#include "linker/kr16_image.h"
#include "linker/linker.h"

namespace {

std::uint16_t read16(std::span<const std::uint8_t> data, std::size_t offset) {
    return static_cast<std::uint16_t>(data[offset]) |
           static_cast<std::uint16_t>(data[offset + 1] << 8);
}

std::uint32_t read32(std::span<const std::uint8_t> data, std::size_t offset) {
    std::uint32_t value = 0;
    for (int byte = 0; byte < 4; ++byte) {
        value |= static_cast<std::uint32_t>(data[offset + static_cast<std::size_t>(byte)])
                 << (byte * 8);
    }
    return value;
}

// A v1 reader: the previous decoder's dispatch, with only tags 1..8. It must
// skip every v1.1 tag and section without an error and still count the v1
// records; that is the additivity promise the format makes.
struct V1Counts {
    std::size_t sources = 0;
    std::size_t lines = 0;
    std::size_t functions = 0;
    std::size_t symbols = 0;
    std::size_t variables = 0;
    std::size_t types = 0;
};

bool decode_as_v1(std::span<const std::uint8_t> data, V1Counts& counts) {
    counts = V1Counts{};
    if (data.size() < 64 || data[0] != 'K' || data[1] != 'D' || data[2] != 'B' ||
        data[3] != 'G') {
        return false;
    }
    if (read16(data, 4) != 1) {
        return false;
    }
    const auto section_count = read16(data, 36);
    const auto table = read32(data, 40);
    const auto strings = read32(data, 44);
    if (table + section_count * 32 > data.size() || strings > data.size()) {
        return false;
    }
    for (std::size_t index = 0; index < section_count; ++index) {
        const auto entry = table + index * 32;
        const auto offset = read32(data, entry + 8);
        const auto size = read32(data, entry + 12);
        if (offset > data.size() || size > data.size() - offset) {
            return false;
        }
        std::size_t position = 0;
        while (position + 8 <= size) {
            const auto tag = read16(data.subspan(offset, size), position);
            const auto length = read32(data.subspan(offset, size), position + 4);
            position += 8;
            if (length > size - position) {
                return false;
            }
            position += length;
            switch (tag) {
            case 1: ++counts.sources; break;
            case 2: ++counts.lines; break;
            case 3: ++counts.functions; break;
            case 4: ++counts.symbols; break;
            case 5: ++counts.variables; break;
            case 6: ++counts.types; break;
            default: break;
            }
        }
    }
    return true;
}

}  // namespace

TEST_CASE("KDBG v1.1 round-trips locations, scopes, flags, base, arrays and refs") {
    krc::debug::KdbgImage image;
    image.minor_version = 1;
    image.sources.push_back({0, "main.c"});
    image.variables.push_back({"f", "x", 4, 4, "i32"});
    image.variable_info.push_back(
        {"f", "x", krc::debug::KdbgVariableKind::Local, 0x1, "f"});
    image.locations.push_back({"f", "x", 9, false, 0x1000, 0x1010});
    image.scopes.push_back({"f", "f", "", 0x1000, 0x1010});
    krc::debug::KdbgType base;
    base.name = "Base";
    base.size = 4;
    base.kind = "struct";
    image.types.push_back(base);
    krc::debug::KdbgType derived;
    derived.name = "Derived";
    derived.size = 4;
    derived.kind = "struct";
    derived.base = "Base";
    image.types.push_back(derived);
    krc::debug::KdbgType array;
    array.name = "[2_x_i32]";
    array.size = 8;
    array.kind = "array";
    array.element = "i32";
    array.element_count = 2;
    image.types.push_back(array);
    krc::debug::KdbgType pointer;
    pointer.name = "ptr<i32>";
    pointer.size = 4;
    pointer.kind = "pointer";
    pointer.target = "i32";
    image.types.push_back(pointer);

    const auto bytes = krc::debug::encode_kdbg(image);
    krc::debug::KdbgImage decoded;
    std::string error;
    REQUIRE_MESSAGE(krc::debug::decode_kdbg(bytes, decoded, error), error);
    CHECK_EQ(decoded.minor_version, 1);
    REQUIRE_EQ(decoded.locations.size(), 1);
    CHECK_EQ(decoded.locations[0].function, "f");
    CHECK_EQ(decoded.locations[0].name, "x");
    CHECK_EQ(decoded.locations[0].reg, 9);
    CHECK_EQ(decoded.locations[0].start, 0x1000u);
    CHECK_EQ(decoded.locations[0].end, 0x1010u);
    REQUIRE_EQ(decoded.scopes.size(), 1);
    CHECK_EQ(decoded.scopes[0].name, "f");
    REQUIRE_EQ(decoded.variable_info.size(), 1);
    CHECK_EQ(decoded.variable_info[0].kind,
             krc::debug::KdbgVariableKind::Local);
    CHECK_EQ(decoded.variable_info[0].flags, 1);
    REQUIRE_EQ(decoded.types.size(), 4);
    CHECK_EQ(decoded.types[1].base, "Base");
    CHECK_EQ(decoded.types[2].element, "i32");
    CHECK_EQ(decoded.types[2].element_count, 2);
    CHECK_EQ(decoded.types[3].target, "i32");

    const auto text = krc::debug::dump_kdbg_text(decoded);
    CHECK(text.find("minor=1") != std::string::npos);
    CHECK(text.find("location f.x reg=R9") != std::string::npos);
    CHECK(text.find("scope f:f parent=-") != std::string::npos);
    CHECK(text.find("varinfo f.x kind=local flags=0x1 scope=f") != std::string::npos);
    CHECK(text.find("type Derived size=4 kind=struct base=Base") != std::string::npos);
    CHECK(text.find("type [2_x_i32] size=8 kind=array element=i32 count=2") !=
          std::string::npos);
    CHECK(text.find("target=i32") != std::string::npos);

    const auto json = krc::debug::dump_kdbg_json(decoded);
    CHECK(json.find("\"minor\":1") != std::string::npos);
    CHECK(json.find("\"locations\":[") != std::string::npos);
    CHECK(json.find("\"reg\":\"R9\"") != std::string::npos);
    CHECK(json.find("\"scopes\":[") != std::string::npos);
    CHECK(json.find("\"variableInfo\":[") != std::string::npos);
    CHECK(json.find("\"base\":\"Base\"") != std::string::npos);
    CHECK(json.find("\"element\":\"i32\",\"count\":2") != std::string::npos);
}

TEST_CASE("KDBG v1.1 writes and reads 64-bit address variants") {
    krc::debug::KdbgImage image;
    image.minor_version = 1;
    image.pointer_width = 8;
    image.target_id = 3;
    image.lines.push_back({0x1'0000'0004ull, 0, 3, 1});
    image.functions.push_back({"wide", 0x1'0000'0000ull, 0x1'0000'0040ull, 8, "i64"});
    image.symbols.push_back({"wide_global", 0x2'0000'0008ull, 0x10, "i64"});
    image.locations.push_back({"wide", "x", 10, false, 0x3'0000'0000ull,
                               0x3'0000'0010ull});

    const auto bytes = krc::debug::encode_kdbg(image);
    krc::debug::KdbgImage decoded;
    std::string error;
    REQUIRE_MESSAGE(krc::debug::decode_kdbg(bytes, decoded, error), error);
    CHECK_EQ(decoded.pointer_width, 8);
    CHECK_EQ(decoded.target_id, 3);
    REQUIRE_EQ(decoded.lines.size(), 1);
    CHECK_EQ(decoded.lines[0].address, 0x1'0000'0004ull);
    REQUIRE_EQ(decoded.functions.size(), 1);
    CHECK_EQ(decoded.functions[0].start, 0x1'0000'0000ull);
    CHECK_EQ(decoded.functions[0].end, 0x1'0000'0040ull);
    REQUIRE_EQ(decoded.symbols.size(), 1);
    CHECK_EQ(decoded.symbols[0].address, 0x2'0000'0008ull);
    REQUIRE_EQ(decoded.locations.size(), 1);
    CHECK_EQ(decoded.locations[0].start, 0x3'0000'0000ull);
    CHECK_EQ(decoded.locations[0].end, 0x3'0000'0010ull);

    std::string feature_error;
    CHECK(krc::debug::require_kdbg_feature(
        decoded, krc::debug::KdbgFeature::WideAddresses, feature_error));
}

TEST_CASE("A v1 payload decodes in a v1.1 reader with empty new fields") {
    krc::debug::KdbgImage image;
    image.minor_version = 0;
    image.sources.push_back({0, "old.c"});
    image.variables.push_back({"f", "x", 4, 4, "i32"});
    const auto bytes = krc::debug::encode_kdbg(image);

    krc::debug::KdbgImage decoded;
    std::string error;
    REQUIRE_MESSAGE(krc::debug::decode_kdbg(bytes, decoded, error), error);
    CHECK_EQ(decoded.minor_version, 0);
    REQUIRE_EQ(decoded.variables.size(), 1);
    CHECK(decoded.locations.empty());
    CHECK(decoded.scopes.empty());
    CHECK(decoded.variable_info.empty());

    std::string feature_error;
    CHECK_FALSE(krc::debug::require_kdbg_feature(
        decoded, krc::debug::KdbgFeature::Locations, feature_error));
    CHECK(feature_error.find("predates") != std::string::npos);
}

TEST_CASE("A v1 reader skips a v1.1 payload without an error") {
    krc::debug::KdbgImage image;
    image.minor_version = 1;
    image.sources.push_back({0, "new.c"});
    image.lines.push_back({0x1000, 0, 1, 1});
    image.functions.push_back({"f", 0x1000, 0x1010, 8, "i32"});
    image.variables.push_back({"f", "x", 4, 4, "i32"});
    image.locations.push_back({"f", "x", 9, false, 0x1000, 0x1010});
    image.scopes.push_back({"f", "f", "", 0x1000, 0x1010});
    image.variable_info.push_back(
        {"f", "x", krc::debug::KdbgVariableKind::Local, 0, "f"});
    image.types.push_back({"Derived", 4, "struct", "Base", "", 0, "", {}});
    const auto bytes = krc::debug::encode_kdbg(image);

    V1Counts counts;
    REQUIRE_MESSAGE(decode_as_v1(bytes, counts), "v1 reader rejected the payload");
    CHECK_EQ(counts.sources, 1);
    CHECK_EQ(counts.lines, 1);
    CHECK_EQ(counts.functions, 1);
    CHECK_EQ(counts.variables, 1);
    CHECK_EQ(counts.types, 1);
}

TEST_CASE("A newer minor is reported rather than guessed") {
    krc::debug::KdbgImage image;
    image.minor_version = 2;
    const auto bytes = krc::debug::encode_kdbg(image);
    krc::debug::KdbgImage decoded;
    std::string error;
    REQUIRE_MESSAGE(krc::debug::decode_kdbg(bytes, decoded, error), error);
    CHECK_FALSE(krc::debug::require_kdbg_feature(
        decoded, krc::debug::KdbgFeature::Locations, error));
    CHECK(error.find("newer KDBG") != std::string::npos);
}

TEST_CASE("Truncated payloads and corrupted sections are rejected") {
    krc::debug::KdbgImage image;
    image.sources.push_back({0, "x.c"});
    const auto bytes = krc::debug::encode_kdbg(image);
    krc::debug::KdbgImage decoded;
    std::string error;
    CHECK_FALSE(krc::debug::decode_kdbg(
        std::span<const std::uint8_t>(bytes.data(), 32), decoded, error));

    auto corrupted = bytes;
    // Flip a payload byte; the section CRC must catch it.
    corrupted.back() ^= 0xFF;
    CHECK_FALSE(krc::debug::decode_kdbg(corrupted, decoded, error));
}

TEST_CASE("The bridge parser emits location, scope, flags, base, array and ref") {
    krc::linker::LinkedImage image;
    image.target_id = 1;
    image.base = 0x1000;
    image.entry = 0x1000;
    krc::linker::LinkedSection text;
    text.name = ".text";
    text.type = krc::linker::KroSectionType::Text;
    text.address = 0x1000;
    text.alignment = 4;
    text.data.assign(16, 0x5A);
    text.size = text.data.size();
    image.sections.push_back(text);

    std::string records;
    const auto add = [&](const std::string& record) {
        records += record;
        records.push_back('\0');
    };
    add("S 0 main.c");
    add("L start 0 2 1");
    add("F f start finish 8 i32");
    add("V f x 4 i32 4");
    add("P f x R9 start finish");
    add("K f f - start finish");
    add("W f x local f 0x1");
    add("T i32 4");
    add("C S 4 -");
    add("A [2_x_i32] i32 2");
    add("R ptr<i32> i32");
    krc::linker::LinkedSection info;
    info.name = ".debug_info";
    info.type = krc::linker::KroSectionType::Debug;
    info.alignment = 1;
    info.data.assign(records.begin(), records.end());
    info.size = info.data.size();
    image.sections.push_back(info);
    image.symbols.push_back(
        {"start", 0x1000, 16, 0, krc::linker::KroSymbolBinding::Global});
    image.symbols.push_back(
        {"finish", 0x1010, 0, 0, krc::linker::KroSymbolBinding::Global});

    std::string error;
    REQUIRE_MESSAGE(krc::linker::attach_kdbg(image, error), error);
    REQUIRE(image.sections.back().name == ".kdbg");
    krc::debug::KdbgImage decoded;
    REQUIRE_MESSAGE(
        krc::debug::decode_kdbg(image.sections.back().data, decoded, error), error);
    REQUIRE_EQ(decoded.locations.size(), 1);
    CHECK_EQ(decoded.locations[0].function, "f");
    CHECK_EQ(decoded.locations[0].name, "x");
    CHECK_EQ(decoded.locations[0].reg, 9);
    CHECK_EQ(decoded.locations[0].start, 0x1000u);
    CHECK_EQ(decoded.locations[0].end, 0x1010u);
    REQUIRE_EQ(decoded.scopes.size(), 1);
    CHECK_EQ(decoded.scopes[0].name, "f");
    CHECK_EQ(decoded.scopes[0].start, 0x1000u);
    REQUIRE_EQ(decoded.variable_info.size(), 1);
    CHECK_EQ(decoded.variable_info[0].flags, 1);
    CHECK_EQ(decoded.variable_info[0].scope, "f");
    bool saw_array = false;
    bool saw_ref = false;
    for (const auto& type : decoded.types) {
        saw_array = saw_array ||
                    (type.name == "[2_x_i32]" && type.element == "i32" &&
                     type.element_count == 2);
        saw_ref = saw_ref || (type.name == "ptr<i32>" && type.target == "i32");
    }
    CHECK(saw_array);
    CHECK(saw_ref);
}

TEST_CASE("A compiled derived class carries its base and arrays carry elements") {
    krc::toolchain::Options options;
    options.cxx = true;
    const std::string_view derived =
        "struct Base { int x; };\n"
        "struct Derived : Base { };\n"
        "int main() { Derived d; d.x = 7; return d.x; }\n";
    const auto built = krc::toolchain::build(
        std::vector<krc::toolchain::Source>{{"derived.cpp", derived}}, options);
    REQUIRE_MESSAGE(built.ok, built.error);
    krc::debug::KdbgImage image;
    std::string error;
    REQUIRE_MESSAGE(krc::debug::decode_kdbg_image(built.image, image, error), error);
    CHECK_EQ(image.target_id, 1);
    bool derived_ok = false;
    bool base_ok = false;
    for (const auto& type : image.types) {
        derived_ok = derived_ok ||
                     (type.name == "Derived" && type.base == "Base");
        base_ok = base_ok || type.name == "Base";
    }
    CHECK(derived_ok);
    CHECK(base_ok);

    krc::toolchain::Options c_options;
    const std::string_view c_source =
        "int f(int n) { int a[3]; int *p = a; a[0] = n; return *p; }\n"
        "int main() { return f(7); }\n";
    const auto c_built = krc::toolchain::build(
        std::vector<krc::toolchain::Source>{{"array.c", c_source}}, c_options);
    REQUIRE_MESSAGE(c_built.ok, c_built.error);
    krc::debug::KdbgImage c_image;
    REQUIRE_MESSAGE(krc::debug::decode_kdbg_image(c_built.image, c_image, error),
                    error);
    bool array_ok = false;
    bool pointer_ok = false;
    for (const auto& type : c_image.types) {
        array_ok = array_ok || (type.element == "i32" && type.element_count == 3);
        pointer_ok = pointer_ok ||
                     (type.kind == "pointer" && type.target == "i32");
    }
    CHECK(array_ok);
    CHECK(pointer_ok);

    krc::toolchain::Options kr16;
    kr16.target = krc::toolchain::Target::Kr16;
    const auto kex = krc::toolchain::build(
        std::vector<krc::toolchain::Source>{{"t.c", "int main() { return 0; }"}},
        kr16);
    REQUIRE_MESSAGE(kex.ok, kex.error);
    krc::debug::KdbgImage kex_image;
    REQUIRE_MESSAGE(krc::debug::decode_kdbg_image(kex.image, kex_image, error),
                    error);
    CHECK_EQ(kex_image.target_id, 2);
}
