#include <doctest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "asm/kr16_assembler.h"
#include "asm/kr16_object.h"
#include "krc/target/target.h"
#include "krc/toolchain.h"
#include "linker/kro_object.h"

namespace {

constexpr std::string_view kTwoFunctions =
    ".text\n"
    ".global _start\n"
    "_start:\n"
    "    MOV R0, #used\n"
    "    CALL R0\n"
    "    HLT\n"
    "\n"
    ".section .text.used\n"
    ".global used\n"
    "used:\n"
    "    MOV R0, #1\n"
    "    RET\n"
    "\n"
    ".section .text.unused\n"
    ".global unused\n"
    "unused:\n"
    "    MOV R0, #2\n"
    "    RET\n";

bool has_section(const krc::toolchain::LinkResult& result, std::string_view name) {
    return std::any_of(result.sections.begin(), result.sections.end(),
                       [&](const krc::toolchain::SectionInfo& section) {
                           return section.name == name;
                       });
}

}  // namespace

TEST_CASE("The KR16 assembler keeps .section .text.<name> sections") {
    krc::asmkr16::AssemblyResult assembly;
    std::string error;
    REQUIRE_MESSAGE(krc::asmkr16::assemble(kTwoFunctions, assembly, error), error);
    // .text, .data, .bss, then the named sections in first-use order.
    REQUIRE_EQ(assembly.sections.size(), 5);
    CHECK_EQ(assembly.sections[0].name, ".text");
    CHECK_EQ(assembly.sections[3].name, ".text.used");
    CHECK_EQ(assembly.sections[4].name, ".text.unused");
    CHECK_EQ(assembly.sections[3].data.size(), 4);
    CHECK_EQ(assembly.sections[4].data.size(), 4);

    const auto symbol_section = [&](std::string_view name) {
        for (const auto& symbol : assembly.symbols) {
            if (symbol.name == name) {
                return symbol.section;
            }
        }
        return std::uint32_t{0};
    };
    CHECK_EQ(symbol_section("_start"), 0);
    CHECK_EQ(symbol_section("used"), 3);
    CHECK_EQ(symbol_section("unused"), 4);

    const auto& profile = krc::target::get_target(krc::target::TargetId::Kr16);
    krc::linker::KroObject object;
    REQUIRE_MESSAGE(krc::asmkr16::make_object(assembly, profile, object, error),
                    error);
    REQUIRE_EQ(object.sections.size(), 3);
    CHECK_EQ(object.sections[0].name, ".text");
    CHECK_EQ(object.sections[1].name, ".text.used");
    CHECK_EQ(object.sections[2].name, ".text.unused");
}

TEST_CASE("krld --gc-sections drops an unreferenced KR16 function section") {
    krc::toolchain::Options options;
    options.target = krc::toolchain::Target::Kr16;
    options.entry = "_start";
    const auto assembled =
        krc::toolchain::assemble(kTwoFunctions, std::string{}, options);
    REQUIRE_MESSAGE(assembled.ok, assembled.error);
    const std::vector<krc::toolchain::Object> objects{
        {"sections.kro", assembled.object}};

    const auto all = krc::toolchain::link(objects, options);
    REQUIRE_MESSAGE(all.ok, all.error);
    CHECK(has_section(all, ".text.used"));
    CHECK(has_section(all, ".text.unused"));
    std::uint64_t all_unused_size = 0;
    for (const auto& section : all.sections) {
        if (section.name == ".text.unused") {
            all_unused_size = section.size;
        }
    }
    CHECK_EQ(all_unused_size, 4);

    options.gc_sections = true;
    const auto gc = krc::toolchain::link(objects, options);
    REQUIRE_MESSAGE(gc.ok, gc.error);
    CHECK(has_section(gc, ".text.used"));
    CHECK_FALSE(has_section(gc, ".text.unused"));
    CHECK(gc.image.size() < all.image.size());
    // The surviving section keeps the address layout assigns it: `.text` is
    // still first, and `used` follows it.
    for (const auto& section : gc.sections) {
        if (section.name == ".text") {
            CHECK_EQ(section.address, 0x100);
        }
        if (section.name == ".text.used") {
            CHECK_EQ(section.address, 0x108);
        }
    }
}
