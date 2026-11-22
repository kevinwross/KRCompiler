#include "linker/kr216_image.h"
#include "linker/kro_object.h"
#include "linker/linker.h"

#include <cstdint>
#include <string>
#include <vector>

#include "doctest.h"

namespace {

std::uint16_t read_u16(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint16_t>(data[offset] | (data[offset + 1] << 8));
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    std::uint32_t value = 0;
    for (int index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(data[offset + index]) << (8 * index);
    }
    return value;
}

std::string section_name(const std::vector<std::uint8_t>& data, std::size_t record) {
    const std::uint32_t string_offset = read_u32(data, 36);
    const std::uint32_t name_offset = read_u32(data, record);
    const char* name = reinterpret_cast<const char*>(data.data() + string_offset + name_offset);
    return std::string(name);
}

}  // namespace

TEST_CASE("kr216 image writer emits the ADR 0054 container") {
    krc::linker::LinkedImage image;
    image.target_id = 4;
    image.base = 0x0200;
    image.entry = 0x0200;
    image.sections.push_back({".text", krc::linker::KroSectionType::Text,
                              0x1 | 0x4, 0x0200, 2, {0x52, 0x2A, 0x00, 0x73}, 4});
    image.sections.push_back({".bss", krc::linker::KroSectionType::Bss,
                              0x1 | 0x2, 0x4100, 2, {}, 8});
    image.sections.push_back({".kdbg", krc::linker::KroSectionType::Debug,
                              0x8, 0, 1, {0xA5, 0xA5, 0xA5, 0xA5}, 4});

    std::vector<std::uint8_t> bytes;
    std::string error;
    REQUIRE_MESSAGE(krc::linker::write_kr216_image(image, bytes, error), error);
    REQUIRE(bytes.size() >= 64);
    CHECK(bytes[0] == 'K');
    CHECK(bytes[1] == '2');
    CHECK(bytes[2] == '1');
    CHECK(bytes[3] == '6');
    CHECK(read_u16(bytes, 4) == 1);
    CHECK(read_u16(bytes, 6) == 2);
    CHECK((read_u32(bytes, 8) & 1u) == 1u);
    CHECK(read_u32(bytes, 12) == 64);
    CHECK(read_u16(bytes, 16) == 0x0200);
    CHECK(read_u16(bytes, 18) == 1);
    CHECK(read_u16(bytes, 20) == 4);
    CHECK(read_u16(bytes, 22) == 4);
    CHECK(read_u32(bytes, 24) == 0x0200);
    CHECK(read_u32(bytes, 28) == 64);
    CHECK(read_u32(bytes, 44) == bytes.size());
    CHECK(read_u32(bytes, 48) != 0);

    const std::uint16_t section_count = read_u16(bytes, 20);
    REQUIRE(section_count == 4);
    bool saw_reset = false;
    bool saw_text = false;
    bool saw_bss = false;
    bool saw_debug = false;
    for (std::uint16_t index = 0; index < section_count; ++index) {
        const std::size_t record = 64 + static_cast<std::size_t>(index) * 32;
        const std::string name = section_name(bytes, record);
        const std::uint16_t type = read_u16(bytes, record + 4);
        const std::uint16_t flags = read_u16(bytes, record + 6);
        const std::uint16_t address = read_u16(bytes, record + 8);
        const std::uint32_t offset = read_u32(bytes, record + 12);
        const std::uint32_t size = read_u32(bytes, record + 16);
        if (name == ".reset") {
            saw_reset = true;
            CHECK(type == 1);
            CHECK(flags == 0x1);
            CHECK(address == 0x0100);
            CHECK(size == 2);
            CHECK(read_u16(bytes, offset) == 0x0200);
        } else if (name == ".text") {
            saw_text = true;
            CHECK(type == 1);
            CHECK(flags == (0x1 | 0x4));
            CHECK(address == 0x0200);
            CHECK(bytes[offset] == 0x52);
        } else if (name == ".bss") {
            saw_bss = true;
            CHECK(type == 8);
            CHECK(flags == (0x1 | 0x2));
            CHECK(address == 0x4100);
            CHECK(size == 8);
            CHECK(offset == 0);
        } else if (name == ".kdbg") {
            saw_debug = true;
            CHECK(type == 1);
            CHECK(flags == 0x8);
            CHECK(address == 0);
            CHECK(size == 4);
        }
    }
    CHECK(saw_reset);
    CHECK(saw_text);
    CHECK(saw_bss);
    CHECK(saw_debug);

    std::vector<std::uint8_t> zeroed = bytes;
    zeroed[48] = zeroed[49] = zeroed[50] = zeroed[51] = 0;
    CHECK(read_u32(bytes, 48) == krc::linker::crc32(zeroed));
}

TEST_CASE("kr216 image writer rejects an entry outside executable text") {
    krc::linker::LinkedImage image;
    image.target_id = 4;
    image.base = 0x0200;
    image.entry = 0x0300;
    image.sections.push_back({".text", krc::linker::KroSectionType::Text,
                              0x1 | 0x4, 0x0200, 2, {0x71}, 1});
    std::vector<std::uint8_t> bytes;
    std::string error;
    CHECK_FALSE(krc::linker::write_kr216_image(image, bytes, error));
    CHECK(error.find("entry") != std::string::npos);
}

TEST_CASE("kr216 image writer rejects overlapping allocated sections") {
    krc::linker::LinkedImage image;
    image.target_id = 4;
    image.base = 0x0200;
    image.entry = 0x0200;
    image.sections.push_back({".text", krc::linker::KroSectionType::Text,
                              0x1 | 0x4, 0x0200, 2, {0x71}, 1});
    image.sections.push_back({".data", krc::linker::KroSectionType::Data,
                              0x1 | 0x2, 0x0200, 2, {0x00}, 1});
    std::vector<std::uint8_t> bytes;
    std::string error;
    CHECK_FALSE(krc::linker::write_kr216_image(image, bytes, error));
    CHECK(error.find("overlap") != std::string::npos);
}
