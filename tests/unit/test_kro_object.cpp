#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "doctest.h"
#include "linker/kro_object.h"

namespace {

krc::linker::KroObject sample_object() {
    krc::linker::KroObject object;
    object.target_id = 1;
    object.pointer_width = 32;
    object.abi_version = 1;

    krc::linker::KroSection text;
    text.name = ".text";
    text.type = krc::linker::KroSectionType::Text;
    text.flags = 0x1 | 0x4;
    text.alignment = 4;
    text.data = {0x2A, 0x08, 0x20, 0x00, 0x00, 0x00, 0x00, 0x76};
    object.sections.push_back(text);

    krc::linker::KroSection data;
    data.name = ".data";
    data.type = krc::linker::KroSectionType::Data;
    data.flags = 0x1 | 0x2;
    data.alignment = 4;
    data.data = {72, 105, 0};
    object.sections.push_back(data);

    krc::linker::KroSymbol start;
    start.name = "_start";
    start.binding = krc::linker::KroSymbolBinding::Global;
    start.type = krc::linker::KroSymbolType::Function;
    start.section = 0;
    start.value = 0;
    start.size = 8;
    object.symbols.push_back(start);

    krc::linker::KroSymbol message;
    message.name = "msg";
    message.type = krc::linker::KroSymbolType::Object;
    message.section = 1;
    message.value = 0;
    message.size = 3;
    object.symbols.push_back(message);

    krc::linker::KroRelocation relocation;
    relocation.section = 0;
    relocation.type = krc::linker::KroRelocationType::Abs32;
    relocation.width = 4;
    relocation.offset = 4;
    relocation.symbol = 0;
    relocation.addend = 0;
    object.relocations.push_back(relocation);
    return object;
}

}  // namespace

TEST_CASE("KRO writer and reader round-trip an object") {
    const auto original = sample_object();
    std::vector<std::uint8_t> bytes;
    std::string error;
    REQUIRE_MESSAGE(krc::linker::write_object(original, bytes, error), error);
    REQUIRE(bytes.size() >= 120);
    CHECK(bytes[0] == 'K');
    CHECK(bytes[1] == 'R');
    CHECK(bytes[2] == 'O');
    CHECK(bytes[3] == 0);

    krc::linker::KroObject decoded;
    REQUIRE_MESSAGE(krc::linker::read_object(bytes, decoded, error), error);
    CHECK(decoded.target_id == 1);
    CHECK(decoded.pointer_width == 32);
    CHECK(decoded.sections.size() == 2);
    CHECK(decoded.symbols.size() == 2);
    CHECK(decoded.relocations.size() == 1);
    CHECK(decoded.sections[0].name == ".text");
    CHECK(decoded.sections[0].data == original.sections[0].data);
    CHECK(decoded.sections[1].name == ".data");
    CHECK(decoded.sections[1].data == original.sections[1].data);
    CHECK(decoded.symbols[0].name == "_start");
    CHECK(decoded.symbols[1].name == "msg");
    CHECK(decoded.relocations[0].offset == 4);
    CHECK(decoded.relocations[0].type == krc::linker::KroRelocationType::Abs32);
}

TEST_CASE("KRO output is deterministic and CRC-checked") {
    const auto object = sample_object();
    std::vector<std::uint8_t> first;
    std::vector<std::uint8_t> second;
    std::string error;
    REQUIRE_MESSAGE(krc::linker::write_object(object, first, error), error);
    REQUIRE_MESSAGE(krc::linker::write_object(object, second, error), error);
    CHECK(first == second);

    auto corrupt = first;
    corrupt[108] ^= 0xFF;
    krc::linker::KroObject decoded;
    CHECK_FALSE(krc::linker::read_object(corrupt, decoded, error));
    CHECK(error.find("CRC") != std::string::npos);
}

TEST_CASE("KRO reader rejects bad magic") {
    auto bytes = std::vector<std::uint8_t>(120, 0);
    krc::linker::KroObject decoded;
    std::string error;
    CHECK_FALSE(krc::linker::read_object(bytes, decoded, error));
    CHECK(error.find("magic") != std::string::npos);
}
