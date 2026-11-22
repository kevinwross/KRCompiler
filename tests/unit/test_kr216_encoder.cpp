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
TEST_CASE("kr216 Compact class emits the ISA bytes") {
    Encoding out;
    std::string error;
    REQUIRE(encode_compact(0, 2, 3, out, error));  // MOV r2, r3
    CHECK(out.size == 2); CHECK(out.bytes[0] == 0x80); CHECK(out.bytes[1] == 0x23);
    REQUIRE(encode_compact(1, 2, 3, out, error)); CHECK(out.bytes[0] == 0x81);
    REQUIRE(encode_compact(19, 2, 3, out, error)); CHECK(out.bytes[0] == 0x93);  // LD [r3]+
    REQUIRE(encode_compact(29, 2, 0, out, error)); CHECK(out.bytes[0] == 0x9D);  // NEG r2
    REQUIRE(encode_compact(31, 3, 0, out, error)); CHECK(out.bytes[0] == 0x9F);  // JMP r3
    CHECK_FALSE(encode_compact(32, 0, 0, out, error));
    CHECK_FALSE(encode_compact(0, 16, 0, out, error));

    REQUIRE(encode_branch(0, 6, out, error));  // BEQ +6
    CHECK(out.bytes[0] == 0xA0); CHECK(out.bytes[1] == 0x06);
    REQUIRE(encode_branch(15, -9, out, error));  // BRA -9
    CHECK(out.bytes[0] == 0xBF); CHECK(out.bytes[1] == 0xF7);
    REQUIRE(encode_branch(14, 0, out, error));  // AL
    CHECK(out.bytes[0] == 0xBC);
    CHECK_FALSE(encode_branch(0, 256, out, error));
}
TEST_CASE("kr216 Normal class emits the ISA bytes") {
    Encoding out;
    std::string error;
    REQUIRE(encode_normal_ld_d8(2, 3, 2, out, error));   // ld r2, [r3+2]
    CHECK(out.size == 3); CHECK(out.bytes[0] == 0xC0); CHECK(out.bytes[1] == 0x23); CHECK(out.bytes[2] == 0x02);
    REQUIRE(encode_normal_st_d8(2, 3, 2, out, error));   // st [r3+2], r2
    CHECK(out.bytes[0] == 0xC1); CHECK(out.bytes[1] == 0x23);
    REQUIRE(encode_normal_push_mask(0x0300, out, error)); // push #0x0300
    CHECK(out.bytes[0] == 0xD7); CHECK(out.bytes[1] == 0x00); CHECK(out.bytes[2] == 0x03);
    REQUIRE(encode_normal_pop_mask(0x0006, out, error));  // pop #0x0006
    CHECK(out.bytes[0] == 0xD8); CHECK(out.bytes[1] == 0x06);
    CHECK_FALSE(encode_normal_ld_d8(2, 3, 128, out, error));
    CHECK_FALSE(encode_normal_push_mask(0, out, error));
}
TEST_CASE("kr216 Normal class immediate, absolute jump and LEAVE") {
    Encoding out;
    std::string error;
    REQUIRE(encode_normal_add_imm8(2, 5, out, error));   // add r2, #5
    CHECK(out.size == 3); CHECK(out.bytes[0] == 0xC7); CHECK(out.bytes[1] == 0x20); CHECK(out.bytes[2] == 0x05);
    REQUIRE(encode_normal_jmp_absolute(0x0200, out));
    CHECK(out.bytes[0] == 0xCF); CHECK(out.bytes[1] == 0x00); CHECK(out.bytes[2] == 0x02);
    REQUIRE(encode_normal_leave(0x0300, out, error));
    CHECK(out.bytes[0] == 0xDC); CHECK(out.bytes[1] == 0x00); CHECK(out.bytes[2] == 0x03);
    CHECK_FALSE(encode_normal_add_imm8(2, 128, out, error));
    CHECK_FALSE(encode_normal_leave(0, out, error));
}
TEST_CASE("kr216 Normal class compare, SXTB, absolute CALL and shifts") {
    Encoding out;
    std::string error;
    REQUIRE(encode_normal_cmp_imm8(2, 5, out, error));   // cmp r2, #5
    CHECK(out.bytes[0] == 0xCA); CHECK(out.bytes[1] == 0x20); CHECK(out.bytes[2] == 0x05);
    REQUIRE(encode_normal_sxtb(2, out, error));          // sxtb r2
    CHECK(out.bytes[0] == 0xDB); CHECK(out.bytes[1] == 0x20); CHECK(out.bytes[2] == 0x00);
    REQUIRE(encode_normal_call_absolute(0x0300, out));   // call 0x0300
    CHECK(out.bytes[0] == 0xD0); CHECK(out.bytes[1] == 0x00); CHECK(out.bytes[2] == 0x03);
    REQUIRE(encode_normal_shift_imm(0, 2, 1, out, error));  // shl r2, #1
    CHECK(out.bytes[0] == 0xD4); CHECK(out.bytes[1] == 0x20); CHECK(out.bytes[2] == 0x01);
    REQUIRE(encode_normal_shift_imm(2, 2, 1, out, error));
    CHECK(out.bytes[0] == 0xD6);
    CHECK_FALSE(encode_normal_shift_imm(3, 2, 1, out, error));
    CHECK_FALSE(encode_normal_shift_imm(0, 2, 32, out, error));
}
TEST_CASE("kr216 F0 emits the canonical form") {
    Encoding out;
    std::string error;
    REQUIRE(encode_f0(0x00, 0x02, 0x03, nullptr, 0, out, error));  // mov r2, r3
    CHECK(out.size == 4); CHECK(out.bytes[0] == 0xF0); CHECK(out.bytes[1] == 0x00);
    CHECK(out.bytes[2] == 0x02); CHECK(out.bytes[3] == 0x03);

    const std::uint8_t imm[] = {0x05, 0x00};
    REQUIRE(encode_f0(0x01, 0x02, 0x10, imm, 2, out, error));      // add r2, #5
    CHECK(out.size == 6); CHECK(out.bytes[3] == 0x10); CHECK(out.bytes[4] == 0x05);

    const std::uint8_t disp[] = {0x02};
    REQUIRE(encode_f0(0x0E, 0x02, 0x53, disp, 1, out, error));     // ld r2, [r3+2]
    CHECK(out.size == 5); CHECK(out.bytes[2] == 0x02); CHECK(out.bytes[3] == 0x53);
    CHECK(out.bytes[4] == 0x02);

    CHECK_FALSE(encode_f0(0x1B, 0x00, 0x00, nullptr, 0, out, error));
    CHECK_FALSE(encode_f0(0x00, 0x70, 0x00, nullptr, 0, out, error));
}
TEST_CASE("kr216 F3 FP register ops and F7 ENTER") {
    Encoding out;
    std::string error;
    REQUIRE(encode_f3(0, 0, 1, out, error));   // fadd f0, f1 (FADD is opcode 0)
    CHECK(out.size == 2); CHECK(out.bytes[0] == 0xF3); CHECK(out.bytes[1] == 0x01);
    REQUIRE(encode_f3(8, 0, 1, out, error));   // fcmp f0, f1
    CHECK(out.bytes[1] == 0x81);
    REQUIRE(encode_f3(9, 1, 0, out, error));   // frnd f1
    CHECK(out.bytes[1] == 0x94);
    CHECK_FALSE(encode_f3(4, 0, 1, out, error));  // FNEG with Fs set
    CHECK_FALSE(encode_f3(1, 4, 0, out, error));  // f4

    REQUIRE(encode_f7_enter(8, 0x0300, out, error));
    CHECK(out.size == 4); CHECK(out.bytes[0] == 0xF7); CHECK(out.bytes[1] == 8);
    CHECK(out.bytes[2] == 0x00); CHECK(out.bytes[3] == 0x03);
    CHECK_FALSE(encode_f7_enter(7, 0x0300, out, error));
    CHECK_FALSE(encode_f7_enter(8, 0, out, error));
}
TEST_CASE("kr216 F1 long-integer forms") {
    Encoding out;
    std::string error;
    REQUIRE(encode_f1(0, 2, 0x4000, out, error));  // ldbu r2, [0x4000]
    CHECK(out.size == 4); CHECK(out.bytes[0] == 0xF1); CHECK(out.bytes[1] == 0x02);
    CHECK(out.bytes[2] == 0x00); CHECK(out.bytes[3] == 0x40);
    REQUIRE(encode_f1(2, 3, 0xFF00, out, error));  // stb [0xFF00], r3
    CHECK(out.bytes[1] == 0x23); CHECK(out.bytes[3] == 0xFF);
    REQUIRE(encode_f1(4, 2, 6, out, error));       // lea r2, [sp+6]
    CHECK(out.bytes[1] == 0x42); CHECK(out.bytes[2] == 0x06); CHECK(out.bytes[3] == 0x00);
    CHECK_FALSE(encode_f1(5, 0, 0, out, error));
    CHECK_FALSE(encode_f1(0, 16, 0, out, error));
}
