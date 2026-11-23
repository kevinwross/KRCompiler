#include "doctest.h"

#include "asm/kr232_object.h"
#include "krc/toolchain.h"
#include "linker/kr232_image.h"
#include "linker/linker.h"
#include "linker/kro_object.h"

#include <cstdint>
#include <string>
#include <vector>

using krc::linker::Kr232Image;
using krc::linker::Kr232Section;
using krc::linker::write_kr232_image;

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

Kr232Image kernel_image() {
    Kr232Image image;
    image.entry = 0x0200;
    image.image_base = 0;
    image.sections.push_back(Kr232Section{".text", 1, 5 /* alloc | execute */, 0x0200, 4, {0x71}});
    image.sections.push_back(Kr232Section{".kdbg", 1, 8 /* debug */, 0, 1, {0xAA}});
    return image;
}

}  // namespace

TEST_CASE("a kr232 image carries the ADR 0058 header") {
    std::vector<std::uint8_t> bytes;
    std::string error;
    REQUIRE(write_kr232_image(kernel_image(), bytes, error));

    REQUIRE(bytes.size() >= 64);
    CHECK(bytes[0] == 'K');
    CHECK(bytes[1] == '2');
    CHECK(bytes[2] == '3');
    CHECK(bytes[3] == '2');
    CHECK(read16(bytes, 4) == 1);   // container version
    CHECK(read16(bytes, 6) == 2);   // kind executable
    CHECK(read32(bytes, 8) == 0);   // no debug payload flagged here
    CHECK(read32(bytes, 12) == 64);
    CHECK(read32(bytes, 16) == 0x0200);
    CHECK(read16(bytes, 20) == 1);  // ABI version
    CHECK(read16(bytes, 22) == 2);  // two sections
    CHECK(read16(bytes, 24) == 5);  // target id
    CHECK(read32(bytes, 28) == 0);  // kernel base
    CHECK(read32(bytes, 44) == bytes.size());
    CHECK(read32(bytes, 48) != 0);
}

TEST_CASE("the section records and CRC match the payload") {
    std::vector<std::uint8_t> bytes;
    std::string error;
    REQUIRE(write_kr232_image(kernel_image(), bytes, error));

    const std::uint32_t table = read32(bytes, 32);
    const std::uint32_t strings = read32(bytes, 36);
    REQUIRE(table + 64 <= bytes.size());

    // First record: .text at 0x0200, one byte, alloc|execute.
    CHECK(read16(bytes, table + 4) == 1);
    CHECK(read16(bytes, table + 6) == 5);
    CHECK(read32(bytes, table + 8) == 0x0200);
    CHECK(read32(bytes, table + 16) == 1);
    const std::uint32_t text_name = strings + read32(bytes, table);
    CHECK(std::string(reinterpret_cast<const char*>(bytes.data() + text_name)) == ".text");
    CHECK(bytes[read32(bytes, table + 12)] == 0x71);

    // Second record: .kdbg unallocated, debug.
    const std::uint32_t second = table + 32;
    CHECK(read16(bytes, second + 6) == 8);
    CHECK(read32(bytes, second + 8) == 0);
    const std::uint32_t kdbg_name = strings + read32(bytes, second);
    CHECK(std::string(reinterpret_cast<const char*>(bytes.data() + kdbg_name)) == ".kdbg");
    CHECK(strings + 10 <= bytes.size());
    CHECK(read32(bytes, 40) == bytes.size() - strings);

    // The CRC is over the whole file with its own field zeroed.
    std::vector<std::uint8_t> copy = bytes;
    copy[48] = copy[49] = copy[50] = copy[51] = 0;
    CHECK(krc::linker::crc32(copy) == read32(bytes, 48));
}

TEST_CASE("the writer refuses an entry outside text and overlapping sections") {
    std::string error;
    std::vector<std::uint8_t> bytes;

    Kr232Image outside = kernel_image();
    outside.entry = 0x0300;  // past the one-byte .text
    CHECK(!write_kr232_image(outside, bytes, error));
    CHECK(!error.empty());

    Kr232Image overlap;
    overlap.entry = 0x100;
    overlap.sections.push_back(Kr232Section{".a", 1, 5, 0x100, 4, {0x71, 0x71}});
    overlap.sections.push_back(Kr232Section{".b", 1, 5, 0x101, 4, {0x71}});
    CHECK(!write_kr232_image(overlap, bytes, error));

    Kr232Image user_out_of_range = kernel_image();
    user_out_of_range.image_base = 0x08000000;  // .text at 0x0200 is now outside user RAM
    CHECK(!write_kr232_image(user_out_of_range, bytes, error));
}

TEST_CASE("a linked image maps onto the kr232 container") {
    krc::linker::LinkedImage linked;
    linked.target_id = 5;
    linked.base = 0;
    linked.entry = 0x0200;
    krc::linker::LinkedSection text;
    text.name = ".text";
    text.type = static_cast<krc::linker::KroSectionType>(1);
    text.flags = 5;  // alloc | execute
    text.address = 0x0200;
    text.alignment = 4;
    text.data = {0x71};
    text.size = 1;
    linked.sections.push_back(text);

    std::vector<std::uint8_t> bytes;
    std::string error;
    REQUIRE(krc::linker::write_kr232_container(linked, bytes, error));
    CHECK(read16(bytes, 24) == 5);        // target id
    CHECK(read32(bytes, 16) == 0x0200);   // entry
    CHECK(read16(bytes, 22) == 1);        // one section
}
TEST_CASE("the linker writes a kr232 container for a kr232 object") {
    krc::asm_kr232::Program program;
    program.sections.push_back(krc::asm_kr232::Section{".text", {0x73}, 4});  // sys
    program.symbols.push_back(krc::asm_kr232::Symbol{"main", 0, 0, true});

    const auto& profile = krc::target::get_target(krc::target::TargetId::Kr232);
    krc::linker::KroObject object;
    std::string error;
    REQUIRE(krc::asm_kr232::make_object(program, profile, object, error));
    std::vector<std::uint8_t> object_bytes;
    REQUIRE(krc::linker::write_object(object, object_bytes, error));

    std::vector<krc::toolchain::Object> objects(1);
    objects[0].name = "start.o";
    objects[0].bytes = object_bytes;
    krc::toolchain::Options options;  // the target is inferred from the object
    const krc::toolchain::LinkResult linked = krc::toolchain::link(objects, options);
    REQUIRE_MESSAGE(linked.ok, linked.error);
    REQUIRE(linked.image.size() >= 64);
    CHECK(linked.image[0] == 'K');
    CHECK(linked.image[1] == '2');
    CHECK(linked.image[2] == '3');
    CHECK(linked.image[3] == '2');
    CHECK(read16(linked.image, 24) == 5);   // target id
    CHECK(read32(linked.image, 28) == 0);   // kernel base
    CHECK(read32(linked.image, 16) != 0);   // an entry inside text
}
TEST_CASE("a section payload is readable by name from a container") {
    Kr232Image image;
    image.entry = 0x0200;
    image.sections.push_back(Kr232Section{".text", 1, 5, 0x0200, 4, {0x71}});
    image.sections.push_back(Kr232Section{".kdbg", 1, 8, 0, 1, {0xDE, 0xAD}});
    std::vector<std::uint8_t> bytes;
    std::string error;
    REQUIRE(write_kr232_image(image, bytes, error));

    std::span<const std::uint8_t> payload;
    REQUIRE(krc::linker::read_k232_section(bytes, ".kdbg", payload, error));
    REQUIRE(payload.size() == 2);
    CHECK(payload[0] == 0xDE);
    CHECK(payload[1] == 0xAD);

    std::span<const std::uint8_t> missing;
    CHECK(!krc::linker::read_k232_section(bytes, ".nope", missing, error));
    CHECK(error.find(".nope") != std::string::npos);

    const std::uint8_t not_container[] = {'n', 'o', 'p', 'e'};
    CHECK(!krc::linker::read_k232_section(not_container, ".kdbg", missing, error));
}