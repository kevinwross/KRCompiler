#include "doctest.h"

#include "asm/kr232_assembler.h"
#include "krc/debug/kdbg.h"
#include "krc/toolchain.h"
#include "asm/kr232_object.h"

#include <string>
#include <vector>

namespace {
std::uint32_t read32(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    return static_cast<std::uint32_t>(bytes[at]) |
           (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[at + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
}
std::uint16_t read16(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    return static_cast<std::uint16_t>(bytes[at] | (bytes[at + 1] << 8));
}
}  // namespace
using krc::asm_kr232::assemble;
using krc::asm_kr232::AssembledProgram;

TEST_CASE("kr232 assembly places instructions, data and labels") {
    const std::string source =
        ".globl main\n"
        ".text\n"
        "main:\n"
        "  push r2\n"
        "  ld r2, #1\n"
        "  beq done\n"
        "done:\n"
        "  pop r2\n"
        "  sys\n"
        ".data\n"
        "message:\n"
        "  .word 0x12345678\n"
        "  .byte 0xAA, 0xBB\n";

    AssembledProgram program;
    std::string error;
    REQUIRE_MESSAGE(assemble(source, "fixture.s", program, error), error);

    REQUIRE(program.program.sections.size() == 3);  // text, data, debug bridge
    const std::vector<std::uint8_t>& text = program.program.sections[0].bytes;
    const std::vector<std::uint8_t>& data = program.program.sections[1].bytes;

    // push r2, ld r2,#1, then a conditional label branch: it is a two-byte skip
    // over a three-byte bra, because a label branch must reach any distance.
    REQUIRE(text.size() == 1 + 3 + 2 + 3 + 1 + 1);
    CHECK(text[0] == 0x02);              // push r2
    CHECK(text[1] == 0xC6);              // ld r2, #1
    CHECK(text[2] == 0x20);
    CHECK(text[3] == 0x01);
    CHECK(text[4] == 0xA2);             // bne +3, skipping the jmp
    CHECK(text[5] == 0x03);
    CHECK(text[6] == 0xCF);             // jmp +2, patched to the label
    // The bra's displacement depends on where the label lands, so it is not
    // asserted here; the two bytes after it are the patched offset.
    CHECK(text[text.size() - 2] == 0x12);  // pop r2
    CHECK(text[text.size() - 1] == 0x73);  // sys

    REQUIRE(data.size() == 6);
    CHECK(data[0] == 0x78);
    CHECK(data[3] == 0x12);
    CHECK(data[4] == 0xAA);
    CHECK(data[5] == 0xBB);

    // Symbols: main in .text at 0, done at 6, message in .data at 0.
    bool saw_main = false;
    bool saw_done = false;
    bool saw_message = false;
    for (const auto& symbol : program.program.symbols) {
        if (symbol.name == "main") {
            saw_main = true;
            CHECK(symbol.section == 0);
            CHECK(symbol.offset == 0);
            CHECK(symbol.global);
        }
        if (symbol.name == "done") {
            saw_done = true;
            CHECK(symbol.section == 0);
            CHECK(symbol.offset == 9);
        }
        if (symbol.name == "message") {
            saw_message = true;
            CHECK(symbol.section == 1);
            CHECK(symbol.offset == 0);
        }
    }
    CHECK(saw_main);
    CHECK(saw_done);
    CHECK(saw_message);

    // One line record per statement, in source order.
    REQUIRE(program.lines.size() == 5);  // one per instruction, not per label
    CHECK(program.lines[0].line == 4);
    CHECK(program.lines[0].offset == 0);
    CHECK(program.lines[0].file == "fixture.s");
}

TEST_CASE("kr232 assembly reports an undefined label and a bad directive") {
    AssembledProgram program;
    std::string error;
    CHECK(!assemble("main:\n  bra nowhere\n", "x.s", program, error));
    CHECK(error.find("nowhere") != std::string::npos);

    CHECK(!assemble(".bogus\n", "x.s", program, error));
    CHECK(!assemble(".text\n  frobnicate r2\n", "x.s", program, error));
}

TEST_CASE("kr232 assembly emits a debug bridge the linker turns into .kdbg") {
    const std::string source =
        ".globl main\n"
        ".text\n"
        "main:\n"
        "  push r2\n"
        "  sys\n";
    krc::asm_kr232::AssembledProgram assembled;
    std::string error;
    REQUIRE_MESSAGE(assemble(source, "fixture.s", assembled, error), error);

    const auto& profile = krc::target::get_target(krc::target::TargetId::Kr232);
    krc::linker::KroObject object;
    REQUIRE(krc::asm_kr232::make_object(assembled.program, profile, object, error));
    std::vector<std::uint8_t> object_bytes;
    REQUIRE(krc::linker::write_object(object, object_bytes, error));

    std::vector<krc::toolchain::Object> objects(1);
    objects[0].name = "fixture.o";
    objects[0].bytes = object_bytes;
    krc::toolchain::Options options;
    const krc::toolchain::LinkResult linked = krc::toolchain::link(objects, options);
    REQUIRE_MESSAGE(linked.ok, linked.error);

    const std::uint32_t table = read32(linked.image, 32);
    const std::uint32_t strings = read32(linked.image, 36);
    const std::uint16_t count = read16(linked.image, 22);
    std::vector<std::uint8_t> kdbg;
    for (std::uint16_t i = 0; i < count; ++i) {
        const std::uint32_t at = table + 32u * i;
        const std::string name(reinterpret_cast<const char*>(linked.image.data() + strings +
                                                              read32(linked.image, at)));
        if (name == ".kdbg") {
            const std::uint32_t offset = read32(linked.image, at + 12);
            const std::uint32_t size = read32(linked.image, at + 16);
            kdbg.assign(linked.image.begin() + offset, linked.image.begin() + offset + size);
        }
    }
    REQUIRE(!kdbg.empty());
    krc::debug::KdbgImage image;
    REQUIRE_MESSAGE(krc::debug::decode_kdbg(kdbg, image, error), error);
    CHECK(image.target_id == 5);
    CHECK(image.image_base == 0);  // a kernel image records base 0 (ADR 0058)
    REQUIRE(!image.sources.empty());
    CHECK(image.sources.front().path == "fixture.s");
    CHECK(image.lines.size() == 2);
    REQUIRE(!image.functions.empty());
    CHECK(image.functions.front().name == "main");
}