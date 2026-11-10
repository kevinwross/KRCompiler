#include <doctest.h>

#include "debug/kdbg.h"

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
