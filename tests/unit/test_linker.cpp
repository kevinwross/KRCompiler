#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "doctest.h"
#include "linker/kro_object.h"
#include "linker/linker.h"

namespace {

krc::linker::KroObject make_object(std::vector<std::uint8_t> text, std::string symbol,
                                   bool has_relocation) {
    krc::linker::KroObject object;
    object.target_id = 1;
    object.pointer_width = 32;
    object.abi_version = 1;

    krc::linker::KroSection section;
    section.name = ".text";
    section.type = krc::linker::KroSectionType::Text;
    section.flags = 0x1 | 0x4;
    section.alignment = 4;
    section.size = text.size();
    section.data = std::move(text);
    object.sections.push_back(std::move(section));

    krc::linker::KroSymbol defined;
    defined.name = std::move(symbol);
    defined.binding = krc::linker::KroSymbolBinding::Global;
    defined.type = krc::linker::KroSymbolType::Function;
    defined.section = 0;
    defined.value = 0;
    object.symbols.push_back(std::move(defined));

    if (has_relocation) {
        krc::linker::KroSymbol external;
        external.name = "foo";
        external.binding = krc::linker::KroSymbolBinding::Global;
        external.section = 0xFFFFFFFFu;
        object.symbols.push_back(std::move(external));

        krc::linker::KroRelocation relocation;
        relocation.section = 0;
        relocation.type = krc::linker::KroRelocationType::Abs32;
        relocation.width = 4;
        relocation.offset = 0;
        relocation.symbol = 1;
        object.relocations.push_back(relocation);
    }
    return object;
}

}  // namespace

TEST_CASE("linker resolves symbols and patches absolute relocations") {
    std::vector<krc::linker::KroObject> objects;
    objects.push_back(make_object({0, 0, 0, 0}, "_start", true));
    objects.push_back(make_object({0xAA, 0xBB, 0xCC, 0xDD}, "foo", false));

    krc::linker::LinkOptions options;
    options.base = 0x00010000;
    options.entry_symbol = "_start";
    krc::linker::LinkedImage image;
    std::string error;
    REQUIRE_MESSAGE(krc::linker::link_objects(objects, options, image, error), error);
    REQUIRE(image.sections.size() == 1);
    REQUIRE(image.sections[0].data.size() == 8);
    CHECK(image.entry == 0x00010000);
    CHECK(image.sections[0].data[0] == 0x04);
    CHECK(image.sections[0].data[1] == 0x00);
    CHECK(image.sections[0].data[2] == 0x01);
    CHECK(image.sections[0].data[3] == 0x00);
    CHECK(image.sections[0].data[4] == 0xAA);
    CHECK(image.sections[0].data[5] == 0xBB);
    CHECK(image.sections[0].data[6] == 0xCC);
    CHECK(image.sections[0].data[7] == 0xDD);

    const auto find_symbol = [&](std::string_view name) -> std::uint64_t {
        for (const auto& symbol : image.symbols) {
            if (symbol.name == name) {
                return symbol.value;
            }
        }
        return 0;
    };
    CHECK(find_symbol("__heap_start") >= 0x00010000);
    CHECK(find_symbol("__stack_top") - find_symbol("__stack_limit") == 0x10000);
    CHECK(find_symbol("__heap_end") == find_symbol("__stack_limit"));
}

TEST_CASE("linker honors explicit section placement") {
    std::vector<krc::linker::KroObject> objects;
    objects.push_back(make_object({0, 0, 0, 0}, "_start", false));
    krc::linker::LinkOptions options;
    options.section_addresses[".text"] = 0x00020000;
    krc::linker::LinkedImage image;
    std::string error;
    REQUIRE_MESSAGE(krc::linker::link_objects(objects, options, image, error), error);
    REQUIRE_EQ(image.sections.size(), 1);
    CHECK_EQ(image.sections[0].address, 0x00020000);
    CHECK_EQ(image.entry, 0x00020000);
}

TEST_CASE("linker resolves a forward call within one object") {
    krc::linker::KroObject object;
    object.target_id = 1;
    object.pointer_width = 32;
    object.abi_version = 1;
    krc::linker::KroSection text;
    text.name = ".text";
    text.type = krc::linker::KroSectionType::Text;
    text.flags = 0x1 | 0x4;
    text.alignment = 4;
    text.data = {0, 0, 0, 0, 0, 0, 0, 0};
    text.size = 8;
    object.sections.push_back(text);
    krc::linker::KroSymbol start;
    start.name = "_start";
    start.binding = krc::linker::KroSymbolBinding::Global;
    start.type = krc::linker::KroSymbolType::Function;
    start.section = 0;
    start.value = 0;
    object.symbols.push_back(start);
    krc::linker::KroSymbol exit;
    exit.name = "exit";
    exit.binding = krc::linker::KroSymbolBinding::Global;
    exit.type = krc::linker::KroSymbolType::Function;
    exit.section = 0;
    exit.value = 4;
    object.symbols.push_back(exit);
    krc::linker::KroRelocation relocation;
    relocation.section = 0;
    relocation.type = krc::linker::KroRelocationType::Abs32;
    relocation.width = 4;
    relocation.offset = 0;
    relocation.symbol = 1;
    object.relocations.push_back(relocation);

    std::vector<krc::linker::KroObject> objects{object};
    krc::linker::LinkOptions options;
    krc::linker::LinkedImage image;
    std::string error;
    REQUIRE_MESSAGE(krc::linker::link_objects(objects, options, image, error), error);
    REQUIRE(image.sections[0].data.size() == 8);
    CHECK(image.sections[0].data[0] == 0x04);
    CHECK(image.sections[0].data[1] == 0x00);
    CHECK(image.sections[0].data[2] == 0x01);
    CHECK(image.sections[0].data[3] == 0x00);
}

TEST_CASE("linker rejects undefined entry symbols") {
    std::vector<krc::linker::KroObject> objects;
    objects.push_back(make_object({0, 0, 0, 0}, "main", false));

    krc::linker::LinkOptions options;
    krc::linker::LinkedImage image;
    std::string error;
    CHECK_FALSE(krc::linker::link_objects(objects, options, image, error));
    CHECK(error.find("entry symbol") != std::string::npos);
}

TEST_CASE("KR32 executable writer emits the native container") {
    std::vector<krc::linker::KroObject> objects;
    objects.push_back(make_object({0x2A, 0x08, 0x20, 0x00}, "_start", false));
    krc::linker::LinkOptions options;
    krc::linker::LinkedImage image;
    std::string error;
    REQUIRE_MESSAGE(krc::linker::link_objects(objects, options, image, error), error);

    std::vector<std::uint8_t> bytes;
    REQUIRE_MESSAGE(krc::linker::write_kr32_executable(image, bytes, error), error);
    REQUIRE(bytes.size() >= 64);
    CHECK(bytes[0] == 'K');
    CHECK(bytes[1] == '3');
    CHECK(bytes[2] == '2');
    CHECK(bytes[3] == 0);
    CHECK(bytes[4] == 1);
    CHECK(bytes[6] == 1);
    CHECK(bytes[24] == 0x00);
    CHECK(bytes[25] == 0x00);
    CHECK(bytes[26] == 0x01);
    CHECK(bytes[27] == 0x00);
}
