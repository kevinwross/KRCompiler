#include "asm/kr216_encoder.h"
#include "asm/kr216_object.h"
#include "linker/kro_object.h"

#include <cstdint>
#include <limits>
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

TEST_CASE("kr216 object writer records external absolute relocations") {
    const std::string source = R"(
.extern target
_start:
    call target
    ld r2, #target
.word target
)";
    krc::asmkr216::AssembledProgram assembly;
    std::string error;
    REQUIRE_MESSAGE(krc::asmkr216::assemble_text(source, assembly, error), error);
    REQUIRE(assembly.relocations.size() == 3);
    CHECK(assembly.relocations[0].offset == 1);
    CHECK(assembly.relocations[0].symbol == "target");
    CHECK(assembly.relocations[1].offset == 4);
    CHECK(assembly.relocations[1].symbol == "target");
    CHECK(assembly.relocations[2].offset == 6);
    CHECK(assembly.relocations[2].symbol == "target");

    krc::linker::KroObject object;
    REQUIRE_MESSAGE(krc::asmkr216::make_object(
                        assembly,
                        krc::target::get_target(krc::target::TargetId::Kr216), object,
                        error),
                    error);
    REQUIRE(object.relocations.size() == 3);
    CHECK(object.relocations[0].type == krc::linker::KroRelocationType::Abs16);
    CHECK(object.relocations[0].width == 2);
    CHECK(object.symbols[object.relocations[0].symbol].name == "target");
    CHECK(object.symbols[object.relocations[0].symbol].section ==
          std::numeric_limits<std::uint32_t>::max());
}

TEST_CASE("kr216 object writer carries data and bss sections") {
    const std::string source = R"(
.data
.global value
value:
    .word 0x1234
.bss
buffer:
    .space 8
.text
.global _start
_start:
    ld r2, #value
    ret
)";
    krc::asmkr216::AssembledProgram assembly;
    std::string error;
    REQUIRE_MESSAGE(krc::asmkr216::assemble_text(source, assembly, error), error);
    REQUIRE(assembly.data_sections.size() == 2);
    CHECK(assembly.data_sections[0].name == ".data");
    CHECK(assembly.data_sections[1].name == ".bss");
    REQUIRE(assembly.data_sections[0].labels.size() == 1);
    CHECK(assembly.data_sections[0].labels[0].first == "value");
    REQUIRE(assembly.data_sections[1].labels.size() == 1);
    CHECK(assembly.data_sections[1].labels[0].first == "buffer");
    CHECK(assembly.data_sections[1].size == 8);

    krc::linker::KroObject object;
    REQUIRE_MESSAGE(krc::asmkr216::make_object(
                        assembly,
                        krc::target::get_target(krc::target::TargetId::Kr216), object,
                        error),
                    error);
    REQUIRE(object.sections.size() == 3);
    CHECK(object.sections[0].type == krc::linker::KroSectionType::Text);
    CHECK(object.sections[1].type == krc::linker::KroSectionType::Data);
    CHECK(object.sections[2].type == krc::linker::KroSectionType::Bss);
    CHECK(object.sections[2].size == 8);
    bool saw_value = false;
    bool saw_buffer = false;
    for (const auto& symbol : object.symbols) {
        if (symbol.name == "value") {
            saw_value = true;
            CHECK(symbol.section == 1);
        }
        if (symbol.name == "buffer") {
            saw_buffer = true;
            CHECK(symbol.section == 2);
        }
    }
    CHECK(saw_value);
    CHECK(saw_buffer);
    REQUIRE(object.relocations.size() == 1);
    CHECK(object.relocations[0].section == 0);
    CHECK(object.symbols[object.relocations[0].symbol].name == "value");
}

TEST_CASE("kr216 object writer carries rodata and init/fini arrays") {
    const std::string source = R"(
.rodata
.word 0x1234
.init_array
.word _start
.fini_array
.word _start
.text
.global _start
_start:
    ret
)";
    krc::asmkr216::AssembledProgram assembly;
    std::string error;
    REQUIRE_MESSAGE(krc::asmkr216::assemble_text(source, assembly, error), error);
    REQUIRE(assembly.data_sections.size() == 3);
    CHECK(assembly.data_sections[0].type == 3);
    CHECK(assembly.data_sections[1].type == 5);
    CHECK(assembly.data_sections[2].type == 6);

    krc::linker::KroObject object;
    REQUIRE_MESSAGE(krc::asmkr216::make_object(
                        assembly,
                        krc::target::get_target(krc::target::TargetId::Kr216), object,
                        error),
                    error);
    REQUIRE(object.sections.size() == 4);
    CHECK(object.sections[1].type == krc::linker::KroSectionType::Rodata);
    CHECK(object.sections[2].type == krc::linker::KroSectionType::InitArray);
    CHECK(object.sections[3].type == krc::linker::KroSectionType::FiniArray);
    REQUIRE(object.relocations.size() == 2);
    CHECK(object.relocations[0].section == 2);
    CHECK(object.relocations[1].section == 3);
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
