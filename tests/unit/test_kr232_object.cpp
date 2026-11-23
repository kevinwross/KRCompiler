#include "doctest.h"

#include "asm/kr232_object.h"
#include "krc/target/target.h"

using krc::asm_kr232::make_object;
using krc::asm_kr232::Program;
using krc::asm_kr232::Section;
using krc::asm_kr232::Symbol;

TEST_CASE("a kr232 program becomes an object with target id 5") {
    Program program;
    program.sections.push_back(Section{".text", {0x71}, 4});  // nop
    program.symbols.push_back(Symbol{"main", 0, 0, true});

    krc::linker::KroObject object;
    std::string error;
    const auto& profile = krc::target::get_target(krc::target::TargetId::Kr232);
    REQUIRE(make_object(program, profile, object, error));
    CHECK(object.kind == krc::linker::KroKind::Object);
    CHECK(object.target_id == 5);
    CHECK(object.abi_version == 1);
    CHECK(object.pointer_width == 32);
    CHECK(object.endianness == 0);
    REQUIRE(object.sections.size() == 1);
    CHECK(object.sections[0].name == ".text");
    CHECK(object.sections[0].size == 1);
    CHECK(object.sections[0].data == std::vector<std::uint8_t>{0x71});
    REQUIRE(object.symbols.size() == 1);
    CHECK(object.symbols[0].name == "main");
    CHECK(object.symbols[0].value == 0);
    CHECK(object.symbols[0].binding == krc::linker::KroSymbolBinding::Global);
}

TEST_CASE("the kr232 object writer refuses an empty program and a bad symbol") {
    const auto& profile = krc::target::get_target(krc::target::TargetId::Kr232);
    krc::linker::KroObject object;
    std::string error;

    Program empty;
    CHECK(!make_object(empty, profile, object, error));

    Program bad_symbol;
    bad_symbol.sections.push_back(Section{".text", {0x71}, 4});
    bad_symbol.symbols.push_back(Symbol{"ghost", 3, 0, false});
    CHECK(!make_object(bad_symbol, profile, object, error));

    // The writer is for kr232 only; another target's profile is refused.
    Program program;
    program.sections.push_back(Section{".text", {0x71}, 4});
    const auto& other = krc::target::get_target(krc::target::TargetId::Kr32);
    CHECK(!make_object(program, other, object, error));
}
