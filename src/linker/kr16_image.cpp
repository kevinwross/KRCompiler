#include "linker/kr16_image.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "linker/kro_object.h"

namespace krc::linker {

namespace {

constexpr std::size_t kHeaderSize = 64;
constexpr std::size_t kSectionHeaderSize = 32;
constexpr std::uint16_t kVersion = 0x0001;
constexpr std::uint32_t kContainerHasDebug = 1u << 1;
constexpr std::uint16_t kSectionAlloc = 1u << 0;
constexpr std::uint16_t kSectionExec = 1u << 1;
constexpr std::uint16_t kSectionWrite = 1u << 2;
constexpr std::uint16_t kSectionDebug = 1u << 3;
constexpr std::uint16_t kSectionProgbits = 1;
constexpr std::uint16_t kSectionNobits = 2;

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

std::uint16_t section_type(KroSectionType type) {
    return type == KroSectionType::Bss ? kSectionNobits : kSectionProgbits;
}

std::uint16_t section_flags(KroSectionType type) {
    switch (type) {
    case KroSectionType::Text:
        return kSectionAlloc | kSectionExec;
    case KroSectionType::Rodata:
        return kSectionAlloc;
    case KroSectionType::Data:
        return kSectionAlloc | kSectionWrite;
    case KroSectionType::Debug:
        return kSectionDebug;
    default:
        return 0;
    }
}

std::uint64_t align_up(std::uint64_t value, std::uint64_t alignment) {
    if (alignment <= 1) {
        return value;
    }
    return (value + alignment - 1) / alignment * alignment;
}

class StringTable {
public:
    std::uint32_t add(std::string_view name) {
        std::size_t position = 0;
        while (position < data_.size()) {
            const auto end = data_.find('\0', position);
            const auto candidate =
                std::string_view(data_).substr(position, end - position);
            if (candidate == name) {
                return static_cast<std::uint32_t>(position);
            }
            position = end + 1;
        }
        const auto offset = static_cast<std::uint32_t>(data_.size());
        data_.append(name);
        data_.push_back('\0');
        return offset;
    }

    [[nodiscard]] const std::string& data() const { return data_; }

private:
    std::string data_;
};

}  // namespace

bool write_kr16_image(const std::vector<std::uint8_t>& text,
                      std::uint16_t load_address, std::uint16_t entry,
                      std::span<const LinkedSection> extra_sections,
                      std::vector<std::uint8_t>& bytes, std::string& error) {
    if (text.size() > 0xFFFF) {
        error = "KR16 text section exceeds 64 KiB";
        return false;
    }
    if (text.size() % 2 != 0) {
        error = "KR16 text section is not word-aligned";
        return false;
    }

    StringTable strings;
    const auto text_name = strings.add(".text");
    const auto debug_str_name = strings.add(".debug_str");
    std::vector<std::uint32_t> extra_names;
    extra_names.reserve(extra_sections.size());
    for (const auto& section : extra_sections) {
        extra_names.push_back(strings.add(section.name));
    }

    const std::size_t section_count = 2 + extra_sections.size();
    const std::size_t section_table = kHeaderSize;
    const std::size_t text_offset = kHeaderSize + section_count * kSectionHeaderSize;

    std::vector<std::size_t> extra_offsets(extra_sections.size(), 0);
    std::size_t payload_cursor = text_offset + text.size();
    for (std::size_t index = 0; index < extra_sections.size(); ++index) {
        const auto& section = extra_sections[index];
        if (section.type == KroSectionType::Bss) {
            extra_offsets[index] = payload_cursor;
            continue;
        }
        payload_cursor = static_cast<std::size_t>(align_up(
            payload_cursor, std::max<std::uint64_t>(1, section.alignment)));
        extra_offsets[index] = payload_cursor;
        payload_cursor += section.data.size();
    }
    const std::size_t string_offset = payload_cursor;
    const std::size_t total = string_offset + strings.data().size();
    bytes.assign(total, 0);

    bool has_debug = false;
    for (const auto& section : extra_sections) {
        has_debug = has_debug || section.type == KroSectionType::Debug;
    }

    bytes[0] = 'K';
    bytes[1] = 'R';
    bytes[2] = '1';
    bytes[3] = '6';
    write_u16(bytes, 4, kVersion);
    write_u16(bytes, 6, 2);  // EXEC
    write_u32(bytes, 8, has_debug ? kContainerHasDebug : 0);
    write_u32(bytes, 12, static_cast<std::uint32_t>(kHeaderSize));
    write_u16(bytes, 16, entry);
    write_u16(bytes, 18, 0);
    write_u16(bytes, 20, static_cast<std::uint16_t>(section_count));
    write_u16(bytes, 22, 0);
    write_u32(bytes, 24, 0);
    write_u32(bytes, 28, static_cast<std::uint32_t>(section_table));
    write_u32(bytes, 32, 0);
    write_u32(bytes, 36, static_cast<std::uint32_t>(string_offset));
    write_u32(bytes, 40, static_cast<std::uint32_t>(strings.data().size()));
    write_u32(bytes, 44, 0);
    write_u32(bytes, 48, 0);

    write_section(bytes, section_table, text_name, kSectionProgbits,
                  kSectionAlloc | kSectionExec, load_address,
                  static_cast<std::uint32_t>(text_offset),
                  static_cast<std::uint32_t>(text.size()), 2);
    write_section(bytes, section_table + kSectionHeaderSize, debug_str_name,
                  kSectionProgbits, kSectionDebug, 0,
                  static_cast<std::uint32_t>(string_offset),
                  static_cast<std::uint32_t>(strings.data().size()), 1);
    for (std::size_t index = 0; index < extra_sections.size(); ++index) {
        const auto& section = extra_sections[index];
        if (section.address > 0xFFFF) {
            error = "KR16 section '" + section.name + "' address exceeds 16 bits";
            return false;
        }
        if (section.size > 0xFFFFFFFFull) {
            error = "KR16 section '" + section.name + "' size exceeds 32 bits";
            return false;
        }
        const auto alignment = static_cast<std::uint16_t>(
            std::min<std::uint64_t>(std::max<std::uint64_t>(1, section.alignment),
                                    0xFFFF));
        write_section(bytes,
                      section_table + (2 + index) * kSectionHeaderSize,
                      extra_names[index], section_type(section.type),
                      section_flags(section.type),
                      static_cast<std::uint16_t>(section.address),
                      static_cast<std::uint32_t>(extra_offsets[index]),
                      static_cast<std::uint32_t>(section.size), alignment);
    }

    for (std::size_t index = 0; index < text.size(); ++index) {
        bytes[text_offset + index] = text[index];
    }
    for (std::size_t index = 0; index < extra_sections.size(); ++index) {
        const auto& section = extra_sections[index];
        if (section.type == KroSectionType::Bss) {
            continue;
        }
        for (std::size_t byte = 0; byte < section.data.size(); ++byte) {
            bytes[extra_offsets[index] + byte] = section.data[byte];
        }
    }
    for (std::size_t index = 0; index < strings.data().size(); ++index) {
        bytes[string_offset + index] =
            static_cast<std::uint8_t>(strings.data()[index]);
    }
    write_u32(bytes, 52, 0);
    write_u32(bytes, 52, crc32(bytes));
    return true;
}

bool write_kr16_image(const std::vector<std::uint8_t>& text,
                      std::uint16_t load_address, std::uint16_t entry,
                      std::vector<std::uint8_t>& bytes, std::string& error) {
    return write_kr16_image(text, load_address, entry,
                            std::span<const LinkedSection>{}, bytes, error);
}

bool write_kr16_image(const std::vector<std::uint8_t>& text,
                      std::uint16_t entry,
                      std::vector<std::uint8_t>& bytes,
                      std::string& error) {
    return write_kr16_image(text, entry, entry,
                            std::span<const LinkedSection>{}, bytes, error);
}

}  // namespace krc::linker
