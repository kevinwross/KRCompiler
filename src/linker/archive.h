#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace krc::linker {

struct ArchiveMember {
    std::string name;
    std::span<const std::uint8_t> data;
};

[[nodiscard]] bool is_archive(std::span<const std::uint8_t> bytes);
[[nodiscard]] bool read_archive(std::span<const std::uint8_t> bytes,
                                std::vector<ArchiveMember>& members,
                                std::string& error);

}  // namespace krc::linker
