#include "kr232_image.h"

#include <algorithm>
#include <map>

#include "linker/kro_object.h"

namespace krc::linker {
namespace {

constexpr std::uint32_t kHeaderSize = 64;
constexpr std::uint32_t kSectionRecordSize = 32;
constexpr std::uint32_t kKernelLimit = 0x08000000u;
constexpr std::uint32_t kUserLimit = 0x10000000u;
constexpr std::uint16_t kKindExecutable = 2;
constexpr std::uint16_t kTargetId = 5;
constexpr std::uint16_t kAbiVersion = 1;
constexpr std::uint32_t kFlagDebug = 1;

void put32(std::vector<std::uint8_t>& out, std::size_t at, std::uint32_t value) {
    out[at] = static_cast<std::uint8_t>(value & 0xFF);
    out[at + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    out[at + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    out[at + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
}

void put16(std::vector<std::uint8_t>& out, std::size_t at, std::uint16_t value) {
    out[at] = static_cast<std::uint8_t>(value & 0xFF);
    out[at + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
}

bool is_power_of_two(std::uint16_t value) { return value != 0 && (value & (value - 1)) == 0; }

}  // namespace

bool write_kr232_image(const Kr232Image& image, std::vector<std::uint8_t>& out,
                       std::string& error) {
    out.clear();
    if (image.sections.empty()) {
        error = "an image needs at least one section";
        return false;
    }
    if (image.image_base != 0 && image.image_base != 0x08000000u) {
        error = "the image base is 0 for a kernel image or 0x08000000 for a user image";
        return false;
    }
    const std::uint32_t low = image.image_base;
    const std::uint32_t high = image.image_base == 0 ? kKernelLimit : kUserLimit;

    // Allocated sections must not overlap, and the entry must be executable.
    std::vector<std::pair<std::uint64_t, std::uint64_t>> allocated;
    bool entry_is_executable = false;
    for (const Kr232Section& section : image.sections) {
        if (!is_power_of_two(section.alignment)) {
            error = "a section alignment is not a power of two";
            return false;
        }
        if (section.flags == 0) {
            continue;  // unallocated, such as a debug section
        }
        const std::uint64_t start = section.address;
        const std::uint64_t end = start + section.data.size();
        if (start < low || end > high) {
            error = "a section leaves the image's half of the memory map";
            return false;
        }
        allocated.emplace_back(start, end);
        if ((section.flags & 4u) != 0 && image.entry >= start && image.entry < end) {
            entry_is_executable = true;
        }
    }
    if (!entry_is_executable) {
        error = "the entry is outside every executable section";
        return false;
    }
    std::sort(allocated.begin(), allocated.end());
    for (std::size_t i = 1; i < allocated.size(); ++i) {
        if (allocated[i].first < allocated[i - 1].second) {
            error = "two allocated sections overlap";
            return false;
        }
    }

    // String table: one NUL-terminated name per section, offsets from its start.
    std::vector<std::uint8_t> strings;
    std::vector<std::uint32_t> name_offsets;
    for (const Kr232Section& section : image.sections) {
        name_offsets.push_back(static_cast<std::uint32_t>(strings.size()));
        strings.insert(strings.end(), section.name.begin(), section.name.end());
        strings.push_back(0);
    }

    // Payloads follow the header; the table and string table follow the payloads.
    std::vector<std::uint32_t> file_offsets;
    std::uint32_t cursor = kHeaderSize;
    for (const Kr232Section& section : image.sections) {
        file_offsets.push_back(cursor);
        cursor += static_cast<std::uint32_t>(section.data.size());
    }
    const std::uint32_t table_offset = cursor;
    const auto record_bytes = static_cast<std::uint32_t>(kSectionRecordSize * image.sections.size());
    const std::uint32_t strings_offset = table_offset + record_bytes;
    const std::uint32_t file_size = strings_offset + static_cast<std::uint32_t>(strings.size());

    out.assign(file_size, 0);
    out[0] = 'K';
    out[1] = '2';
    out[2] = '3';
    out[3] = '2';
    put16(out, 4, 1);  // container version
    put16(out, 6, kKindExecutable);
    put32(out, 8, image.debug_present ? kFlagDebug : 0);
    put32(out, 12, kHeaderSize);
    put32(out, 16, image.entry);
    put16(out, 20, kAbiVersion);
    put16(out, 22, static_cast<std::uint16_t>(image.sections.size()));
    put16(out, 24, kTargetId);
    put16(out, 26, 0);
    put32(out, 28, image.image_base);
    put32(out, 32, table_offset);
    put32(out, 36, strings_offset);
    put32(out, 40, static_cast<std::uint32_t>(strings.size()));
    put32(out, 44, file_size);

    for (std::size_t i = 0; i < image.sections.size(); ++i) {
        const Kr232Section& section = image.sections[i];
        std::copy(section.data.begin(), section.data.end(), out.begin() + file_offsets[i]);
        const std::uint32_t at = table_offset + static_cast<std::uint32_t>(i) * kSectionRecordSize;
        put32(out, at, name_offsets[i]);
        put16(out, at + 4, section.type);
        put16(out, at + 6, section.flags);
        put32(out, at + 8, section.address);
        put32(out, at + 12, file_offsets[i]);
        put32(out, at + 16, static_cast<std::uint32_t>(section.data.size()));
        put16(out, at + 20, section.alignment);
    }
    std::copy(strings.begin(), strings.end(), out.begin() + strings_offset);

    put32(out, 48, 0);  // the CRC field is zero while the CRC is computed
    put32(out, 48, crc32(out));
    return true;
}

}  // namespace krc::linker
