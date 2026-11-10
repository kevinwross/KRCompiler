#include "linker/kr16_image.h"

#include <cstdint>
#include <string>
#include <vector>

#include "linker/kro_object.h"

namespace krc::linker {

namespace {

constexpr std::size_t kHeaderSize = 64;
constexpr std::size_t kSectionHeaderSize = 32;
constexpr std::uint16_t kVersion = 0x0001;

void write_u16(std::vector<std::uint8_t>& bytes, std::size_t offset,
               std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFF);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

void write_u32(std::vector<std::uint8_t>& bytes, std::size_t offset,
               std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFF);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
}

void write_section(std::vector<std::uint8_t>& bytes, std::size_t offset,
                   std::uint32_t name, std::uint16_t type, std::uint16_t flags,
                   std::uint16_t address, std::uint32_t file_offset,
                   std::uint32_t size, std::uint16_t alignment) {
    write_u32(bytes, offset, name);
    write_u16(bytes, offset + 4, type);
    write_u16(bytes, offset + 6, flags);
    write_u16(bytes, offset + 8, address);
    write_u16(bytes, offset + 10, 0);
    write_u32(bytes, offset + 12, file_offset);
    write_u32(bytes, offset + 16, size);
    write_u16(bytes, offset + 20, alignment);
    write_u16(bytes, offset + 22, 0);
    write_u16(bytes, offset + 24, 0);
    write_u16(bytes, offset + 26, 0);
    write_u32(bytes, offset + 28, 0);
}

}  // namespace

bool write_kr16_image(const std::vector<std::uint8_t>& text,
                      std::uint16_t entry, std::vector<std::uint8_t>& bytes,
                      std::string& error) {
    if (text.size() > 0xFFFF) {
        error = "KR16 text section exceeds 64 KiB";
        return false;
    }
    if (text.size() % 2 != 0) {
        error = "KR16 text section is not word-aligned";
        return false;
    }
    static const std::string strings(".text\0.debug_str\0", 17);
    const std::size_t section_table = kHeaderSize;
    const std::size_t text_offset = kHeaderSize + 2 * kSectionHeaderSize;
    const std::size_t string_offset = text_offset + text.size();
    const std::size_t total = string_offset + strings.size();
    bytes.assign(total, 0);

    bytes[0] = 'K';
    bytes[1] = 'R';
    bytes[2] = '1';
    bytes[3] = '6';
    write_u16(bytes, 4, kVersion);
    write_u16(bytes, 6, 2);  // EXEC
    write_u32(bytes, 8, 0);
    write_u32(bytes, 12, static_cast<std::uint32_t>(kHeaderSize));
    write_u16(bytes, 16, entry);
    write_u16(bytes, 18, 0);
    write_u16(bytes, 20, 2);
    write_u16(bytes, 22, 0);
    write_u32(bytes, 24, 0);
    write_u32(bytes, 28, static_cast<std::uint32_t>(section_table));
    write_u32(bytes, 32, 0);
    write_u32(bytes, 36, static_cast<std::uint32_t>(string_offset));
    write_u32(bytes, 40, static_cast<std::uint32_t>(strings.size()));
    write_u32(bytes, 44, 0);
    write_u32(bytes, 48, 0);

    write_section(bytes, section_table, 0, 1, 0x1 | 0x2, entry,
                  static_cast<std::uint32_t>(text_offset),
                  static_cast<std::uint32_t>(text.size()), 2);
    write_section(bytes, section_table + kSectionHeaderSize, 6, 1, 0x8, 0,
                  static_cast<std::uint32_t>(string_offset),
                  static_cast<std::uint32_t>(strings.size()), 1);
    for (std::size_t index = 0; index < text.size(); ++index) {
        bytes[text_offset + index] = text[index];
    }
    for (std::size_t index = 0; index < strings.size(); ++index) {
        bytes[string_offset + index] =
            static_cast<std::uint8_t>(strings[index]);
    }
    write_u32(bytes, 52, 0);
    write_u32(bytes, 52, crc32(bytes));
    return true;
}

}  // namespace krc::linker
