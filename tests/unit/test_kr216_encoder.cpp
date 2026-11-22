#include "doctest.h"
#include "asm/kr216_encoder.h"

using namespace krc::asmkr216;

TEST_CASE("kr216 Register class emits the ISA bytes") {
    Encoding out;
    std::string error;

    REQUIRE(encode_ld_immediate(2, 0x1234, out, error));
    CHECK(out.size == 3);
    CHECK(out.bytes[0] == 0x52);
    CHECK(out.bytes[1] == 0x34);
    CHECK(out.bytes[2] == 0x12);

    REQUIRE(encode_ld_absolute(2, 0x4000, out, error));
    CHECK(out.bytes[0] == 0x62);
    CHECK(out.bytes[1] == 0x00);
    CHECK(out.bytes[2] == 0x40);

    REQUIRE(encode_st_absolute(2, 0xFF00, out, error));
    CHECK(out.bytes[0] == 0xE2);
    CHECK(out.bytes[1] == 0x00);
    CHECK(out.bytes[2] == 0xFF);
}

TEST_CASE("kr216 Register class rejects a register outside 0-15") {
    Encoding out;
    std::string error;
    CHECK_FALSE(encode_ld_immediate(16, 0, out, error));
    CHECK(error.find("out of range") != std::string::npos);
}