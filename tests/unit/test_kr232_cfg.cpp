#include "doctest.h"

#include <string>

#include "backend/kr232_cfg.h"

using krc::backend::eliminate_dead_frame_stores;
using krc::backend::eliminate_dead_register_definitions;
using krc::backend::fold_frame_addresses_to_lea;
using krc::backend::forward_frame_loads_across_blocks;
using krc::backend::reuse_frame_addresses_across_blocks;

TEST_CASE("KR232 keeps the target and arguments of an indirect jump") {
    const std::string input =
        ".Lf:\n"
        "  ld r2, [r13-28]\n"
        "  mov r8, r6\n"
        "  jmp r8\n";
    const auto output = eliminate_dead_register_definitions(input);
    CHECK(output.find("ld r2, [r13-28]") != std::string::npos);
    CHECK(output.find("mov r8, r6") != std::string::npos);
}

TEST_CASE("KR232 removes dead moves across a branch") {
    const std::string input =
        ".Lf:\n"
        "  mov r6, #1\n"
        "  mov r7, #2\n"
        "  jmp .Lend\n"
        ".Lend:\n"
        "  ret\n";
    const auto output = eliminate_dead_register_definitions(input);
    CHECK(output.find("mov r6") == std::string::npos);
    CHECK(output.find("mov r7") == std::string::npos);
    CHECK(output.find(".Lend:") != std::string::npos);
    CHECK(output.find("ret") != std::string::npos);
}

TEST_CASE("KR232 keeps a value stored in the frame") {
    const std::string input =
        ".Lf:\n"
        "  mov r6, #1\n"
        "  st [r13-4], r6\n"
        "  ret\n";
    const auto output = eliminate_dead_register_definitions(input);
    CHECK(output.find("mov r6, #1") != std::string::npos);
}

TEST_CASE("KR232 removes an unused frame load but not an arbitrary load") {
    const std::string input =
        ".Lf:\n"
        "  ld r6, [r13-4]\n"
        "  ld r7, [r8]\n"
        "  ret\n";
    const auto output = eliminate_dead_register_definitions(input);
    CHECK(output.find("ld r6, [r13-4]") == std::string::npos);
    CHECK(output.find("ld r7, [r8]") != std::string::npos);
}

TEST_CASE("KR232 keeps an argument move across a call") {
    const std::string input =
        ".Lf:\n"
        "  mov r2, #1\n"
        "  call target\n"
        "  ret\n";
    const auto output = eliminate_dead_register_definitions(input);
    CHECK(output.find("mov r2, #1") != std::string::npos);
}

TEST_CASE("KR232 removes a store overwritten before any read") {
    const std::string input =
        ".Lf:\n"
        "  st [r13-4], r6\n"
        "  jmp .Lend\n"
        ".Lend:\n"
        "  st [r13-4], r7\n"
        "  ld r6, [r13-4]\n"
        "  ret\n";
    const auto output = eliminate_dead_frame_stores(input);
    CHECK(output.find("st [r13-4], r6") == std::string::npos);
    CHECK(output.find("st [r13-4], r7") != std::string::npos);
}

TEST_CASE("KR232 keeps a frame store that is read") {
    const std::string input =
        ".Lf:\n"
        "  st [r13-4], r6\n"
        "  ld r7, [r13-4]\n"
        "  ret\n";
    const auto output = eliminate_dead_frame_stores(input);
    CHECK(output.find("st [r13-4], r6") != std::string::npos);
}

TEST_CASE("KR232 keeps a frame store across a pointer read or call") {
    const std::string pointer_read =
        ".Lf:\n"
        "  st [r13-4], r6\n"
        "  ld r7, [r8]\n"
        "  ret\n";
    const auto through_call =
        ".Lf:\n"
        "  st [r13-4], r6\n"
        "  call target\n"
        "  ret\n";
    CHECK(eliminate_dead_frame_stores(pointer_read).find("st [r13-4], r6") !=
          std::string::npos);
    CHECK(eliminate_dead_frame_stores(through_call).find("st [r13-4], r6") !=
          std::string::npos);
}

TEST_CASE("KR232 forwards a frame load across a block boundary") {
    const std::string input =
        ".Lf:\n"
        "  ld r6, [r13-4]\n"
        "  jmp .Luse\n"
        ".Luse:\n"
        "  ld r6, [r13-4]\n"
        "  ret\n";
    const auto output = forward_frame_loads_across_blocks(input);
    CHECK(output.find("ld r6, [r13-4]") != std::string::npos);
    CHECK(output.find("ld r6, [r13-4]") == output.rfind("ld r6, [r13-4]"));
}

TEST_CASE("KR232 keeps a frame load when a call may clobber the register") {
    const std::string input =
        ".Lf:\n"
        "  ld r6, [r13-4]\n"
        "  call target\n"
        "  ld r6, [r13-4]\n"
        "  ret\n";
    const auto output = forward_frame_loads_across_blocks(input);
    CHECK(output.find("ld r6, [r13-4]") != output.rfind("ld r6, [r13-4]"));
}

TEST_CASE("KR232 reuses a frame address across a block boundary") {
    const std::string input =
        ".Lf:\n"
        "  mov r8, r13\n"
        "  sub r8, #4\n"
        "  jmp .Luse\n"
        ".Luse:\n"
        "  mov r8, r13\n"
        "  sub r8, #4\n"
        "  ld r6, [r8]\n"
        "  ret\n";
    const auto output = reuse_frame_addresses_across_blocks(input);
    CHECK(output.find("mov r8, r13") != std::string::npos);
    CHECK(output.find("mov r8, r13") == output.rfind("mov r8, r13"));
    CHECK(output.find("sub r8, #4") == output.rfind("sub r8, #4"));
}

TEST_CASE("KR232 folds a large frame address into one SP-relative lea") {
    const std::string input =
        ".Lf:\n"
        "  enter #16, #0\n"
        "  mov r8, r13\n"
        "  sub r8, #8\n"
        "  ld r6, [r8]\n"
        "  ret\n";
    const auto output = fold_frame_addresses_to_lea(input);
    CHECK(output.find("lea r8, [sp+8]") != std::string::npos);
    CHECK(output.find("sub r8, #8") == std::string::npos);
}

TEST_CASE("KR232 accounts for pushed arguments in the SP-relative lea") {
    const std::string input =
        ".Lf:\n"
        "  enter #16, #0\n"
        "  push r6\n"
        "  mov r8, r13\n"
        "  sub r8, #8\n"
        "  ld r6, [r8]\n"
        "  ret\n";
    const auto output = fold_frame_addresses_to_lea(input);
    CHECK(output.find("lea r8, [sp+12]") != std::string::npos);
}

TEST_CASE("KR232 folds a large frame address with debug directives") {
    const std::string input =
        ".text\n"
        "Game_run:\n"
        ".LGame_run.begin:\n"
        "  enter #364, #0\n"
        ".debug_var Game_run this -8 0 4\n"
        "  mov r8, r13\n"
        "  sub r8, #364\n"
        "  ld r6, [r8]\n"
        "  ret\n";
    const auto output = fold_frame_addresses_to_lea(input);
    CHECK(output.find("lea r8, [sp]") != std::string::npos);
}

TEST_CASE("KR232 keeps the pair when predecessor SP offsets differ") {
    const std::string input =
        ".Lf:\n"
        "  enter #16, #0\n"
        "  br .Ljoin\n"
        "  push r6\n"
        ".Ljoin:\n"
        "  mov r8, r13\n"
        "  sub r8, #8\n"
        "  ld r6, [r8]\n"
        "  ret\n";
    const auto output = fold_frame_addresses_to_lea(input);
    CHECK(output.find("sub r8, #8") != std::string::npos);
}




