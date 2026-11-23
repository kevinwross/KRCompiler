#include "krc/image/k232.h"

namespace krc::image {
namespace {

constexpr std::uint32_t kHeaderSize = 64;
constexpr std::uint32_t kSectionRecordSize = 32;

std::uint32_t read_u32(std::span<const std::uint8_t> bytes, std::size_t at) {
    return static_cast<std::uint32_t>(bytes[at]) |
           (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[at + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
}

}  // namespace

bool read_k232_section(std::span<const std::uint8_t> container, std::string_view name,
                       std::span<const std::uint8_t>& payload, std::string& error) {
    if (container.size() < kHeaderSize + 4 || container[0] != 'K' || container[1] != '2' ||
        container[2] != '3' || container[3] != '2') {
        error = "the file is not a K232 container";
        return false;
    }
    const auto count = static_cast<std::uint16_t>(container[22] | (container[23] << 8));
    const std::uint32_t table = read_u32(container, 32);
    const std::uint32_t strings = read_u32(container, 36);
    for (std::uint16_t i = 0; i < count; ++i) {
        const std::size_t at = table + static_cast<std::size_t>(i) * kSectionRecordSize;
        if (at + kSectionRecordSize > container.size()) {
            error = "the section table runs past the container";
            return false;
        }
        const std::size_t name_at = strings + read_u32(container, at);
        if (name_at >= container.size()) {
            continue;
        }
        const std::string_view section_name(reinterpret_cast<const char*>(container.data() + name_at));
        if (section_name != name) {
            continue;
        }
        const std::uint32_t offset = read_u32(container, at + 12);
        const std::uint32_t size = read_u32(container, at + 16);
        if (static_cast<std::uint64_t>(offset) + size > container.size()) {
            error = "the section payload runs past the container";
            return false;
        }
        payload = container.subspan(offset, size);
        return true;
    }
    error = "the container has no section named " + std::string(name);
    return false;
}

}  // namespace krc::image
