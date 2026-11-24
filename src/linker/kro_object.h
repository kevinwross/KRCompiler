#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "krc/target/target.h"

namespace krc::linker {

enum class KroKind : std::uint16_t {
    Object = 0,
    Executable = 1,
    ArchiveMember = 2,
};

enum class KroSectionType : std::uint16_t {
    None = 0,
    Text = 1,
    Rodata = 2,
    Data = 3,
    Bss = 4,
    InitArray = 5,
    FiniArray = 6,
    Tls = 7,
    Debug = 8,
    Note = 9,
    Other = 255,
};

enum class KroSymbolBinding : std::uint8_t {
    Local = 0,
    Global = 1,
    Weak = 2,
};

enum class KroSymbolType : std::uint8_t {
    NoType = 0,
    Object = 1,
    Function = 2,
    Section = 3,
    File = 4,
    Tls = 5,
};

enum class KroVisibility : std::uint8_t {
    Default = 0,
    Hidden = 1,
    Protected = 2,
};

enum class KroRelocationType : std::uint16_t {
    Abs8 = 0,
    Abs16 = 1,
    Abs32 = 2,
    Abs64 = 3,
    PcRel8 = 4,
    PcRel16 = 5,
    PcRel32 = 6,
    PcRel64 = 7,
    Kr32Abs32 = 8,
};

struct KroSection {
    std::string name;
    KroSectionType type = KroSectionType::None;
    std::uint16_t flags = 0;
    std::uint64_t address = 0;
    std::uint64_t alignment = 1;
    std::uint64_t size = 0;
    std::uint32_t link = 0;
    std::uint32_t info = 0;
    std::uint32_t entry_size = 0;
    std::vector<std::uint8_t> data;
};

struct KroSymbol {
    std::string name;
    KroSymbolBinding binding = KroSymbolBinding::Local;
    KroSymbolType type = KroSymbolType::NoType;
    KroVisibility visibility = KroVisibility::Default;
    std::uint8_t flags = 0;
    std::uint32_t section = 0;
    std::uint64_t value = 0;
    std::uint64_t size = 0;
};

struct KroRelocation {
    std::uint32_t section = 0;
    KroRelocationType type = KroRelocationType::Abs32;
    std::uint8_t width = 4;
    std::uint8_t flags = 0;
    std::uint64_t offset = 0;
    std::uint32_t symbol = 0;
    std::uint64_t addend = 0;
};

struct KroObject {
    KroKind kind = KroKind::Object;
    std::uint32_t target_id = 0;
    std::uint32_t abi_version = 1;
    std::uint8_t pointer_width = 32;
    std::uint8_t endianness = 0;
    std::uint16_t flags = 0;
    std::uint64_t entry = 0;
    std::uint64_t image_base = 0;
    std::array<std::uint8_t, 16> build_id{};
    std::vector<KroSection> sections;
    std::vector<KroSymbol> symbols;
    std::vector<KroRelocation> relocations;
};

[[nodiscard]] std::uint32_t kROTargetId(target::TargetId target);
[[nodiscard]] std::uint32_t crc32(std::span<const std::uint8_t> bytes);

[[nodiscard]] bool write_object(const KroObject& object, std::vector<std::uint8_t>& bytes,
                                std::string& error);
// True when the bytes begin with the KRO magic. A caller uses this instead of the
// file name: an explicit path names a file, not a format, and G7 of the build
// cleanup plan says a suffix never decides what a file is.
[[nodiscard]] bool is_object(std::span<const std::uint8_t> bytes);
[[nodiscard]] bool read_object(std::span<const std::uint8_t> bytes, KroObject& object,
                               std::string& error);

}  // namespace krc::linker
