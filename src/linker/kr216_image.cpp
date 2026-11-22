#include "linker/kr216_image.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "linker/kro_object.h"

namespace krc::linker {

namespace {

constexpr std::size_t kHeaderSize = 64;
constexpr std::size_t kSectionRecordSize = 32;
constexpr std::uint16_t kContainerVersion = 1;
constexpr std::uint16_t kKindExecutable = 2;
constexpr std::uint16_t kAbiVersion = 1;
constexpr std::uint16_t kTargetId = 4;

constexpr std::uint16_t kTypeProgbits = 1;
constexpr std::uint16_t kTypeNobits = 8;

constexpr std::uint16_t kFlagAlloc = 0x0001;
constexpr std::uint16_t kFlagWrite = 0x0002;
constexpr std::uint16_t kFlagExec = 0x0004;
constexpr std::uint16_t kFlagDebug = 0x0008;

constexpr std::uint32_t kFirstUsable = 0x0100;
constexpr std::uint32_t kLastUsable = 0xFF00;  // exclusive
constexpr std::uint32_t kResetVector = 0x0100;

void write_u16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFF);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

void write_u32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    for (int index = 0; index < 4; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>((value >> (8 * index)) & 0xFF);
    }
}

std::uint64_t align_up(std::uint64_t value, std::uint64_t alignment) {
    if (alignment <= 1) return value;
    return (value + alignment - 1) / alignment * alignment;
}

struct WriteSection {
    std::string name;
    std::uint16_t type = kTypeProgbits;
    std::uint16_t flags = 0;
    std::uint16_t address = 0;
    std::uint32_t size = 0;
    std::uint16_t alignment = 1;
    std::vector<std::uint8_t> data;
};

class StringTable {
public:
    StringTable() : data_(1, '\0') {}

    std::uint32_t add(std::string_view name) {
        std::size_t position = 0;
        while (position < data_.size()) {
            const auto end = data_.find('\0', position);
            const auto candidate = std::string_view(data_).substr(position, end - position);
            if (candidate == name) return static_cast<std::uint32_t>(position);
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

bool is_power_of_two(std::uint16_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

}  // namespace

bool write_kr216_image(const LinkedImage& image, std::vector<std::uint8_t>& bytes,
                       std::string& error) {
    bytes.clear();
    if (image.target_id != kTargetId) {
        error = "kr216 image writer only accepts target ID 4";
        return false;
    }
    if (image.entry == 0 || image.entry > 0xFFFF) {
        error = "kr216 image entry must be a non-zero 16-bit address";
        return false;
    }

    std::vector<WriteSection> sections;
    sections.reserve(image.sections.size() + 1);
    WriteSection reset;
    reset.name = ".reset";
    reset.flags = kFlagAlloc;
    reset.address = static_cast<std::uint16_t>(kResetVector);
    reset.size = 2;
    reset.alignment = 2;
    reset.data = {static_cast<std::uint8_t>(image.entry & 0xFF),
                  static_cast<std::uint8_t>(image.entry >> 8)};
    sections.push_back(std::move(reset));

    for (const auto& section : image.sections) {
        WriteSection out;
        out.name = section.name;
        if (section.address > 0xFFFF) {
            error = "kr216 section '" + section.name + "' address exceeds 16 bits";
            return false;
        }
        out.address = static_cast<std::uint16_t>(section.address);
        if (section.alignment > 0x8000) {
            error = "kr216 section '" + section.name + "' alignment exceeds 16 bits";
            return false;
        }
        out.alignment = static_cast<std::uint16_t>(std::max<std::uint64_t>(1, section.alignment));
        if (!is_power_of_two(out.alignment)) {
            error = "kr216 section '" + section.name + "' alignment is not a power of two";
            return false;
        }
        switch (section.type) {
        case KroSectionType::Text:
            out.type = kTypeProgbits;
            out.flags = kFlagAlloc | kFlagExec;
            break;
        case KroSectionType::Rodata:
            out.type = kTypeProgbits;
            out.flags = kFlagAlloc;
            break;
        case KroSectionType::Data:
            out.type = kTypeProgbits;
            out.flags = kFlagAlloc | kFlagWrite;
            break;
        case KroSectionType::Bss:
            out.type = kTypeNobits;
            out.flags = kFlagAlloc | kFlagWrite;
            break;
        case KroSectionType::Debug:
            out.type = kTypeProgbits;
            out.flags = kFlagDebug;
            out.address = 0;
            break;
        case KroSectionType::Other:
            out.type = kTypeProgbits;
            out.flags = kFlagAlloc | kFlagWrite;
            break;
        default:
            error = "kr216 section '" + section.name + "' has an unsupported type";
            return false;
        }
        const std::uint64_t section_size =
            out.type == kTypeNobits ? section.size : section.data.size();
        if (section_size > 0xFFFFFFFFu) {
            error = "kr216 section '" + section.name + "' is too large";
            return false;
        }
        out.size = static_cast<std::uint32_t>(section_size);
        out.data = out.type == kTypeNobits ? std::vector<std::uint8_t>{} : section.data;
        if (out.name == ".reset") {
            error = "kr216 section '.reset' is reserved for the reset vector";
            return false;
        }
        if (out.address < kFirstUsable ||
            static_cast<std::uint32_t>(out.address) + out.size > kLastUsable) {
            if ((out.flags & kFlagAlloc) != 0) {
                error = "kr216 section '" + section.name +
                        "' is outside the allocated window";
                return false;
            }
        }
        sections.push_back(std::move(out));
    }

    if (sections.size() > 0xFFFF) {
        error = "kr216 image has too many sections";
        return false;
    }

    // Reject overlapping allocated sections before any payload is placed.
    for (std::size_t left = 0; left < sections.size(); ++left) {
        if ((sections[left].flags & kFlagAlloc) == 0) continue;
        const std::uint32_t left_start = sections[left].address;
        const std::uint32_t left_end = left_start + sections[left].size;
        for (std::size_t right = left + 1; right < sections.size(); ++right) {
            if ((sections[right].flags & kFlagAlloc) == 0) continue;
            const std::uint32_t right_start = sections[right].address;
            const std::uint32_t right_end = right_start + sections[right].size;
            if (left_start < right_end && right_start < left_end) {
                error = "kr216 allocated sections overlap: '" + sections[left].name +
                        "' and '" + sections[right].name + "'";
                return false;
            }
        }
    }

    bool entry_inside = false;
    for (const auto& section : sections) {
        if ((section.flags & (kFlagAlloc | kFlagExec)) != (kFlagAlloc | kFlagExec)) continue;
        if (section.type != kTypeProgbits) continue;
        if (image.entry >= section.address &&
            image.entry < static_cast<std::uint32_t>(section.address) + section.size) {
            entry_inside = true;
        }
    }
    if (!entry_inside) {
        error = "kr216 image entry is not inside an executable section";
        return false;
    }

    StringTable strings;
    std::vector<std::uint32_t> names;
    names.reserve(sections.size());
    for (const auto& section : sections) names.push_back(strings.add(section.name));

    const std::size_t section_table = kHeaderSize;
    std::size_t cursor = section_table + sections.size() * kSectionRecordSize;
    std::vector<std::uint32_t> offsets(sections.size(), 0);
    for (std::size_t index = 0; index < sections.size(); ++index) {
        if (sections[index].type == kTypeNobits) continue;
        cursor = static_cast<std::size_t>(align_up(cursor, sections[index].alignment));
        offsets[index] = static_cast<std::uint32_t>(cursor);
        cursor += sections[index].data.size();
    }
    const std::size_t string_offset = cursor;
    const std::size_t total = string_offset + strings.data().size();
    if (total > 0xFFFFFFFFu) {
        error = "kr216 image exceeds the 32-bit file-size field";
        return false;
    }
    bytes.assign(total, 0);

    bytes[0] = 'K';
    bytes[1] = '2';
    bytes[2] = '1';
    bytes[3] = '6';
    write_u16(bytes, 4, kContainerVersion);
    write_u16(bytes, 6, kKindExecutable);
    bool has_debug = false;
    for (const auto& section : sections) has_debug = has_debug || (section.flags & kFlagDebug) != 0;
    write_u32(bytes, 8, has_debug ? 1u : 0u);
    write_u32(bytes, 12, static_cast<std::uint32_t>(kHeaderSize));
    write_u16(bytes, 16, static_cast<std::uint16_t>(image.entry));
    write_u16(bytes, 18, kAbiVersion);
    write_u16(bytes, 20, static_cast<std::uint16_t>(sections.size()));
    write_u16(bytes, 22, kTargetId);
    write_u32(bytes, 24, static_cast<std::uint32_t>(image.base));
    write_u32(bytes, 28, static_cast<std::uint32_t>(section_table));
    write_u32(bytes, 32, 0);
    write_u32(bytes, 36, static_cast<std::uint32_t>(string_offset));
    write_u32(bytes, 40, static_cast<std::uint32_t>(strings.data().size()));
    write_u32(bytes, 44, static_cast<std::uint32_t>(total));
    write_u32(bytes, 48, 0);
    write_u32(bytes, 52, 0);
    write_u32(bytes, 56, 0);
    write_u32(bytes, 60, 0);

    for (std::size_t index = 0; index < sections.size(); ++index) {
        const auto& section = sections[index];
        const std::size_t record = section_table + index * kSectionRecordSize;
        write_u32(bytes, record, names[index]);
        write_u16(bytes, record + 4, section.type);
        write_u16(bytes, record + 6, section.flags);
        write_u16(bytes, record + 8, section.address);
        write_u16(bytes, record + 10, 0);
        write_u32(bytes, record + 12, offsets[index]);
        write_u32(bytes, record + 16, section.size);
        write_u16(bytes, record + 20, section.alignment);
        for (std::size_t reserved = 22; reserved < 32; ++reserved) {
            bytes[record + reserved] = 0;
        }
        if (section.type != kTypeNobits) {
            std::copy(section.data.begin(), section.data.end(),
                      bytes.begin() + static_cast<std::ptrdiff_t>(offsets[index]));
        }
    }
    std::copy(strings.data().begin(), strings.data().end(),
              bytes.begin() + static_cast<std::ptrdiff_t>(string_offset));
    write_u32(bytes, 48, crc32(bytes));
    return true;
}

}  // namespace krc::linker
