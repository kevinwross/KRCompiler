#include "asm/kr216_encoder.h"
#include "asm/kr216_object.h"
#include "linker/kro_object.h"

#include <cstdint>
#include <string>
#include <vector>

#include "doctest.h"

TEST_CASE("kr216 object writer emits a target-4 KRO object") {
    const std::string source = R"(
.text
.global _start
_start:
    call helper
    nop
helper:
    ret
)";
    krc::asmkr216::AssembledProgram assembly;
    std::string error;
    REQUIRE_MESSAGE(krc::asmkr216::assemble_text(source, assembly, error), error);

    krc::linker::KroObject object;
    REQUIRE_MESSAGE(krc::asmkr216::make_object(
                        assembly,
                        krc::target::get_target(krc::target::TargetId::Kr216), object,
                        error),
                    error);
    CHECK(object.kind == krc::linker::KroKind::Object);
    CHECK(object.target_id == 4);
    CHECK(object.abi_version == 1);
    CHECK(object.pointer_width == 16);
    REQUIRE(object.sections.size() == 1);
    CHECK(object.sections[0].name == ".text");
    CHECK(object.sections[0].type == krc::linker::KroSectionType::Text);
    CHECK(object.sections[0].flags == (0x1 | 0x4));
    CHECK(object.sections[0].alignment == 2);
    CHECK(object.sections[0].data == assembly.bytes);
    REQUIRE(object.symbols.size() == 2);
    CHECK(object.symbols[0].name == "_start");
    CHECK(object.symbols[0].binding == krc::linker::KroSymbolBinding::Global);
    CHECK(object.symbols[0].type == krc::linker::KroSymbolType::Function);
    CHECK(object.symbols[0].value == 0);
    CHECK(object.symbols[1].name == "helper");
    CHECK(object.symbols[1].binding == krc::linker::KroSymbolBinding::Local);
    CHECK(object.symbols[1].value == 4);

    std::vector<std::uint8_t> bytes;
    REQUIRE_MESSAGE(krc::linker::write_object(object, bytes, error), error);
    krc::linker::KroObject decoded;
    REQUIRE_MESSAGE(krc::linker::read_object(bytes, decoded, error), error);
    CHECK(decoded.target_id == 4);
    CHECK(decoded.pointer_width == 16);
    REQUIRE(decoded.sections.size() == 1);
    CHECK(decoded.sections[0].data == assembly.bytes);
}

TEST_CASE("kr216 object writer rejects a global symbol with no definition") {
    krc::asmkr216::AssembledProgram assembly;
    std::string error;
    REQUIRE_MESSAGE(krc::asmkr216::assemble_text(".global missing\nnop\n", assembly, error),
                    error);
    krc::linker::KroObject object;
    CHECK_FALSE(krc::asmkr216::make_object(
        assembly, krc::target::get_target(krc::target::TargetId::Kr216), object, error));
    CHECK(error.find("missing") != std::string::npos);
}
