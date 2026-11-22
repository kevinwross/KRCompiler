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
}TEST_CASE("kr216 Short class emits the ISA bytes") {
    Encoding out;
    std::string error;
    REQUIRE(encode_push_register(3, out, error));
    CHECK(out.size == 1); CHECK(out.bytes[0] == 0x03);
    REQUIRE(encode_pop_register(4, out, error)); CHECK(out.bytes[0] == 0x14);
    REQUIRE(encode_inc_register(2, out, error)); CHECK(out.bytes[0] == 0x22);
    REQUIRE(encode_dec_register(2, out, error)); CHECK(out.bytes[0] == 0x32);
    REQUIRE(encode_call_register(5, out, error)); CHECK(out.bytes[0] == 0x45);
    REQUIRE(encode_system(0x70, out, error)); CHECK(out.bytes[0] == 0x70);
    REQUIRE(encode_system(0x78, out, error)); CHECK(out.bytes[0] == 0x78);
    CHECK_FALSE(encode_system(0x79, out, error));
    CHECK_FALSE(encode_push_register(16, out, error));
}
