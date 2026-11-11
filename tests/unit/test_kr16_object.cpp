#include "asm/kr16_assembler.h"
#include "asm/kr16_object.h"
#include "linker/kro_object.h"

#include <cstdint>
#include <string>
#include <vector>

#include "doctest.h"

TEST_CASE("kr16 object writer carries external absolute relocations") {
    const std::string source = R"(
.text
.extern target
_start:
    MOV R0, #target
    RET
)";
    krc::asmkr16::AssemblyResult assembly;
    std::string error;
    REQUIRE_MESSAGE(krc::asmkr16::assemble(source, assembly, error), error);
    REQUIRE(assembly.relocations.size() == 1);
    CHECK(assembly.relocations[0].symbol == "target");
    CHECK(assembly.relocations[0].offset == 2);

    krc::linker::KroObject object;
    REQUIRE_MESSAGE(
        krc::asmkr16::make_object(assembly, krc::target::get_target(
                                                krc::target::TargetId::Kr16),
                                  object, error),
        error);
    REQUIRE(object.relocations.size() == 1);
    CHECK(object.relocations[0].type ==
          krc::linker::KroRelocationType::Abs16);
    CHECK(object.relocations[0].width == 2);
    CHECK(object.symbols[object.relocations[0].symbol].name == "target");
}


TEST_CASE("kr16 object writer emits a KRO object with text and symbols") {
    const std::string source = R"(
.text
.global _start
_start:
    MOV R0, #7
    RET
)";
    krc::asmkr16::AssemblyResult assembly;
    std::string error;
    REQUIRE_MESSAGE(krc::asmkr16::assemble(source, assembly, error), error);

    krc::linker::KroObject object;
    REQUIRE_MESSAGE(
        krc::asmkr16::make_object(assembly, krc::target::get_target(
                                                krc::target::TargetId::Kr16),
                                  object, error),
        error);
    CHECK(object.kind == krc::linker::KroKind::Object);
    CHECK(object.target_id == 2);
    CHECK(object.pointer_width == 16);
    REQUIRE(object.sections.size() == 1);
    CHECK(object.sections[0].type == krc::linker::KroSectionType::Text);
    REQUIRE_FALSE(object.symbols.empty());
    CHECK(object.symbols[0].name == "_start");
    CHECK(object.symbols[0].binding == krc::linker::KroSymbolBinding::Global);

    std::vector<std::uint8_t> bytes;
    REQUIRE_MESSAGE(krc::linker::write_object(object, bytes, error), error);
    krc::linker::KroObject decoded;
    REQUIRE_MESSAGE(krc::linker::read_object(bytes, decoded, error), error);
    CHECK(decoded.target_id == 2);
    CHECK(decoded.pointer_width == 16);
    CHECK(decoded.sections[0].data == assembly.text);
}
