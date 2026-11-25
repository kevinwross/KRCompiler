#include "doctest.h"

#include <string>

#include "backend/kr232_cfg.h"

using krc::backend::eliminate_dead_register_definitions;

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

TEST_CASE("KR232 keeps a value used by a successor block") {
    const std::string input =
        ".Lf:\n"
        "  mov r6, #1\n"
        "  jmp .Luse\n"
        ".Luse:\n"
        "  st [r13-4], r6\n"
        "  ret\n";
    const auto output = eliminate_dead_register_definitions(input);
    CHECK(output.find("mov r6, #1") != std::string::npos);
}
