#include "linker/kro_object.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace krc::linker {

namespace {

constexpr std::uint32_t kHeaderSize = 120;
constexpr std::uint32_t kSectionSize = 64;
constexpr std::uint32_t kSymbolSize = 40;
constexpr std::uint32_t kRelocationSize = 32;
constexpr std::uint32_t kVersionMajor = 1;
constexpr std::uint32_t kVersionMinor = 0;

void write_u16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset + 0] = static_cast<std::uint8_t>(value & 0xFF);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
}

void write_u32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned index = 0; index < 4; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>((value >> (index * 8)) & 0xFF);
    }
}

void write_u64(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t value) {
    for (unsigned index = 0; index < 8; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>((value >> (index * 8)) & 0xFF);
    }
}

std::uint16_t read_u16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(bytes[offset + 1]) << 8;
}

std::uint32_t read_u32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    std::uint32_t value = 0;
    for (unsigned index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8);
    }
    return value;
}

std::uint64_t read_u64(std::span<const std::uint8_t> bytes, std::size_t offset) {
    std::uint64_t value = 0;
    for (unsigned index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8);
    }
    return value;
}

std::uint64_t fnv1a(std::span<const std::uint8_t> bytes, std::uint64_t seed) {
    std::uint64_t hash = seed;
    for (const auto byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

void append_u64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (unsigned index = 0; index < 8; ++index) {
        bytes.push_back(static_cast<std::uint8_t>((value >> (index * 8)) & 0xFF));
    }
}

std::uint64_t align_up(std::uint64_t value, std::uint64_t alignment) {
    if (alignment <= 1) {
        return value;
    }
    return (value + alignment - 1) / alignment * alignment;
}

struct SymbolOrder {
    std::vector<KroSymbol> symbols;
    std::vector<std::uint32_t> old_to_new;
};

SymbolOrder sorted_symbols(const KroObject& object) {
    SymbolOrder order;
    order.symbols = object.symbols;
    order.old_to_new.resize(object.symbols.size());
    for (std::size_t index = 0; index < order.old_to_new.size(); ++index) {
        order.old_to_new[index] = static_cast<std::uint32_t>(index);
    }
    std::vector<std::uint32_t> indices(object.symbols.size());
    for (std::size_t index = 0; index < indices.size(); ++index) {
        indices[index] = static_cast<std::uint32_t>(index);
    }
    std::sort(indices.begin(), indices.end(), [&](std::uint32_t left, std::uint32_t right) {
        const auto& a = object.symbols[left];
        const auto& b = object.symbols[right];
        const bool a_local = a.binding == KroSymbolBinding::Local;
        const bool b_local = b.binding == KroSymbolBinding::Local;
        if (a_local != b_local) {
            return !a_local;
        }
        if (!a_local && a.name != b.name) {
            return a.name < b.name;
        }
        if (a.section != b.section) {
            return a.section < b.section;
        }
        if (a.value != b.value) {
            return a.value < b.value;
        }
        return a.name < b.name;
    });
    for (std::size_t index = 0; index < indices.size(); ++index) {
        order.symbols[index] = object.symbols[indices[index]];
        order.old_to_new[indices[index]] = static_cast<std::uint32_t>(index);
    }
    return order;
}

}  // namespace

std::uint32_t kROTargetId(target::TargetId target) {
    switch (target) {
    case target::TargetId::Kr32:
        return 1;
    case target::TargetId::Kr16:
        return 2;
    case target::TargetId::Kr64:
        return 3;
    case target::TargetId::Kr216:
        return 4;
    case target::TargetId::Kr232:
        return 5;
    }
    return 0;
}

std::uint32_t crc32(std::span<const std::uint8_t> bytes) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const auto byte : bytes) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

bool write_object(const KroObject& object, std::vector<std::uint8_t>& bytes,
                  std::string& error) {
    bytes.clear();
    if (object.target_id == 0) {
        error = "KRO object has no target ID";
        return false;
    }
    if (object.pointer_width != 16 && object.pointer_width != 32 &&
        object.pointer_width != 64) {
        error = "KRO pointer width must be 16, 32, or 64";
        return false;
    }
    if (object.endianness != 0) {
        error = "KRO v1 writer supports little-endian targets only";
        return false;
    }
    if (object.sections.size() > 0xFFFFFFFFu || object.relocations.size() > 0xFFFFFFFFu) {
        error = "KRO object has too many sections or relocations";
        return false;
    }

    const auto order = sorted_symbols(object);
    std::vector<KroRelocation> relocations = object.relocations;
    for (auto& relocation : relocations) {
        if (relocation.symbol >= order.old_to_new.size()) {
            error = "KRO relocation references an out-of-range symbol";
            return false;
        }
        relocation.symbol = order.old_to_new[relocation.symbol];
    }
    std::sort(relocations.begin(), relocations.end(),
              [](const KroRelocation& left, const KroRelocation& right) {
                  if (left.section != right.section) return left.section < right.section;
                  if (left.offset != right.offset) return left.offset < right.offset;
                  return left.symbol < right.symbol;
              });

    std::vector<std::string> strings;
    std::unordered_map<std::string, std::uint32_t> string_offsets;
    std::size_t string_size = 1;
    const auto intern = [&](const std::string& name) {
        if (name.empty()) {
            return std::uint32_t{0};
        }
        const auto found = string_offsets.find(name);
        if (found != string_offsets.end()) {
            return found->second;
        }
        const auto offset = static_cast<std::uint32_t>(string_size);
        string_offsets.emplace(name, offset);
        strings.push_back(name);
        string_size += name.size() + 1;
        return offset;
    };
    for (const auto& section : object.sections) {
        static_cast<void>(intern(section.name));
    }
    for (const auto& symbol : order.symbols) {
        static_cast<void>(intern(symbol.name));
    }

    const auto section_table = kHeaderSize;
    const auto symbol_table = section_table + object.sections.size() * kSectionSize;
    const auto relocation_table = symbol_table + order.symbols.size() * kSymbolSize;
    const auto string_table = relocation_table + relocations.size() * kRelocationSize;
    std::uint64_t payload = align_up(static_cast<std::uint64_t>(string_table) + string_size, 8);

    bytes.assign(static_cast<std::size_t>(payload), 0);
    bytes[0] = 'K';
    bytes[1] = 'R';
    bytes[2] = 'O';
    bytes[3] = 0;
    bytes[4] = static_cast<std::uint8_t>(kVersionMajor);
    bytes[5] = static_cast<std::uint8_t>(kVersionMinor);
    write_u16(bytes, 6, static_cast<std::uint16_t>(object.kind));
    write_u16(bytes, 8, static_cast<std::uint16_t>(kHeaderSize));
    write_u16(bytes, 10, object.flags);
    write_u32(bytes, 12, object.target_id);
    write_u32(bytes, 16, object.abi_version);
    bytes[20] = object.pointer_width;
    bytes[21] = object.endianness;
    write_u16(bytes, 22, 0);
    write_u32(bytes, 24, static_cast<std::uint32_t>(object.sections.size()));
    write_u32(bytes, 28, static_cast<std::uint32_t>(order.symbols.size()));
    write_u32(bytes, 32, static_cast<std::uint32_t>(relocations.size()));
    write_u64(bytes, 36, string_table);
    write_u64(bytes, 44, string_size);
    write_u64(bytes, 52, 0);
    write_u64(bytes, 60, 0);
    write_u64(bytes, 68, object.entry);
    write_u64(bytes, 76, object.image_base);

    for (std::size_t index = 0; index < object.sections.size(); ++index) {
        const auto& section = object.sections[index];
        const auto base = section_table + index * kSectionSize;
        write_u32(bytes, base + 0, intern(section.name));
        write_u16(bytes, base + 4, static_cast<std::uint16_t>(section.type));
        write_u16(bytes, base + 6, section.flags);
        write_u64(bytes, base + 8, section.address);
        write_u64(bytes, base + 16, 0);
        write_u64(bytes, base + 24, section.size == 0 ? section.data.size() : section.size);
        write_u32(bytes, base + 32, static_cast<std::uint32_t>(section.alignment));
        write_u32(bytes, base + 36, section.link);
        write_u32(bytes, base + 40, section.info);
        write_u32(bytes, base + 44, section.entry_size);
    }

    for (std::size_t index = 0; index < order.symbols.size(); ++index) {
        const auto& symbol = order.symbols[index];
        const auto base = symbol_table + index * kSymbolSize;
        write_u32(bytes, base + 0, intern(symbol.name));
        bytes[base + 4] = static_cast<std::uint8_t>(symbol.binding);
        bytes[base + 5] = static_cast<std::uint8_t>(symbol.type);
        bytes[base + 6] = static_cast<std::uint8_t>(symbol.visibility);
        bytes[base + 7] = symbol.flags;
        write_u32(bytes, base + 8, symbol.section);
        write_u64(bytes, base + 16, symbol.value);
        write_u64(bytes, base + 24, symbol.size);
    }

    for (std::size_t index = 0; index < relocations.size(); ++index) {
        const auto& relocation = relocations[index];
        const auto base = relocation_table + index * kRelocationSize;
        write_u32(bytes, base + 0, relocation.section);
        write_u16(bytes, base + 4, static_cast<std::uint16_t>(relocation.type));
        bytes[base + 6] = relocation.width;
        bytes[base + 7] = relocation.flags;
        write_u64(bytes, base + 8, relocation.offset);
        write_u32(bytes, base + 16, relocation.symbol);
        write_u64(bytes, base + 24, relocation.addend);
    }

    bytes[static_cast<std::size_t>(string_table)] = 0;
    std::size_t cursor = 1;
    for (const auto& name : strings) {
        std::memcpy(bytes.data() + string_table + cursor, name.data(), name.size());
        cursor += name.size() + 1;
    }

    std::uint64_t payload_cursor = payload;
    for (std::size_t index = 0; index < object.sections.size(); ++index) {
        auto& section = object.sections[index];
        const auto base = section_table + index * kSectionSize;
        if (section.data.empty()) {
            write_u64(bytes, base + 16, 0);
            continue;
        }
        const auto alignment = std::max<std::uint64_t>(8, section.alignment);
        payload_cursor = align_up(payload_cursor, alignment);
        if (payload_cursor + section.data.size() > bytes.size()) {
            bytes.resize(static_cast<std::size_t>(payload_cursor + section.data.size()));
        }
        std::memcpy(bytes.data() + payload_cursor, section.data.data(), section.data.size());
        write_u64(bytes, base + 16, payload_cursor);
        write_u64(bytes, base + 24, section.data.size());
        payload_cursor += section.data.size();
    }
    bytes.resize(static_cast<std::size_t>(payload_cursor));

    std::vector<std::uint8_t> build_material;
    for (const auto& section : object.sections) {
        build_material.insert(build_material.end(), section.name.begin(), section.name.end());
        append_u64(build_material, section.size == 0 ? section.data.size() : section.size);
        build_material.insert(build_material.end(), section.data.begin(), section.data.end());
    }
    for (const auto& symbol : order.symbols) {
        build_material.insert(build_material.end(), symbol.name.begin(), symbol.name.end());
        append_u64(build_material, symbol.value);
        build_material.push_back(static_cast<std::uint8_t>(symbol.binding));
    }
    const auto hash1 = fnv1a(build_material, 14695981039346656037ULL);
    for (const auto& relocation : relocations) {
        append_u64(build_material, relocation.offset);
        append_u64(build_material, relocation.addend);
        build_material.push_back(static_cast<std::uint8_t>(relocation.type));
    }
    const auto hash2 = fnv1a(build_material, 14695981039346656037ULL);
    write_u64(bytes, 92, hash1);
    write_u64(bytes, 100, hash2);

    write_u64(bytes, 84, bytes.size());
    write_u32(bytes, 108, 0);
    const auto checksum = crc32(bytes);
    write_u32(bytes, 108, checksum);
    return true;
}

bool is_object(std::span<const std::uint8_t> bytes) {
    return bytes.size() >= 4 && bytes[0] == 'K' && bytes[1] == 'R' && bytes[2] == 'O' &&
           bytes[3] == 0;
}
bool read_object(std::span<const std::uint8_t> bytes, KroObject& object, std::string& error) {
    object = KroObject{};
    if (bytes.size() < kHeaderSize) {
        error = "KRO file is shorter than the header";
        return false;
    }
    if (bytes[0] != 'K' || bytes[1] != 'R' || bytes[2] != 'O' || bytes[3] != 0) {
        error = "KRO magic mismatch";
        return false;
    }
    if (bytes[4] != kVersionMajor || bytes[5] > kVersionMinor) {
        error = "unsupported KRO version";
        return false;
    }
    if (read_u16(bytes, 8) != kHeaderSize) {
        error = "unsupported KRO header size";
        return false;
    }
    if (read_u64(bytes, 84) != bytes.size()) {
        error = "KRO file size field does not match the file";
        return false;
    }
    if (bytes[21] != 0) {
        error = "KRO v1 reader supports little-endian targets only";
        return false;
    }

    std::vector<std::uint8_t> checked(bytes.begin(), bytes.end());
    const auto stored_crc = read_u32(bytes, 108);
    write_u32(checked, 108, 0);
    if (crc32(checked) != stored_crc) {
        error = "KRO CRC mismatch";
        return false;
    }

    object.kind = static_cast<KroKind>(read_u16(bytes, 6));
    object.target_id = read_u32(bytes, 12);
    object.abi_version = read_u32(bytes, 16);
    object.pointer_width = bytes[20];
    object.endianness = bytes[21];
    object.flags = read_u16(bytes, 10);
    // Validate the identity fields the reader exposes. The writer checks them on
    // the way out; a reader that trusted a foreign or truncated object would let
    // the linker patch bytes against the wrong target (documentation audit A16c).
    if (object.kind != KroKind::Object && object.kind != KroKind::Executable) {
        error = "KRO kind is unsupported";
        return false;
    }
    if (object.target_id == 0 || object.target_id > 5) {
        error = "KRO target id is unsupported";
        return false;
    }
    if (object.abi_version != 1u) {
        error = "KRO ABI version is unsupported";
        return false;
    }
    if (object.pointer_width != 16 && object.pointer_width != 32 &&
        object.pointer_width != 64) {
        error = "KRO pointer width is unsupported";
        return false;
    }
    object.entry = read_u64(bytes, 68);
    object.image_base = read_u64(bytes, 76);
    std::copy(bytes.begin() + 92, bytes.begin() + 108, object.build_id.begin());

    const auto section_count = read_u32(bytes, 24);
    const auto symbol_count = read_u32(bytes, 28);
    const auto relocation_count = read_u32(bytes, 32);
    const auto section_table = kHeaderSize;
    const auto symbol_table = section_table + static_cast<std::uint64_t>(section_count) * kSectionSize;
    const auto relocation_table = symbol_table + static_cast<std::uint64_t>(symbol_count) * kSymbolSize;
    const auto string_table = relocation_table +
                              static_cast<std::uint64_t>(relocation_count) * kRelocationSize;
    if (string_table > bytes.size()) {
        error = "KRO tables exceed the file";
        return false;
    }
    if (read_u64(bytes, 36) != string_table ||
        read_u64(bytes, 44) + string_table > bytes.size()) {
        error = "KRO string table is out of bounds";
        return false;
    }
    const auto string_size = read_u64(bytes, 44);
    if (string_size == 0 || bytes[static_cast<std::size_t>(string_table)] != 0) {
        error = "KRO string table is invalid";
        return false;
    }
    const auto string_at = [&](std::uint32_t offset) -> std::optional<std::string> {
        if (offset >= string_size) {
            return std::nullopt;
        }
        const auto begin = string_table + offset;
        std::size_t end = begin;
        while (end < string_table + string_size && bytes[static_cast<std::size_t>(end)] != 0) {
            ++end;
        }
        if (end >= string_table + string_size) {
            return std::nullopt;
        }
        return std::string(reinterpret_cast<const char*>(bytes.data() + begin), end - begin);
    };

    for (std::uint32_t index = 0; index < section_count; ++index) {
        const auto base = section_table + index * kSectionSize;
        if (base + kSectionSize > bytes.size()) {
            error = "KRO section table is out of bounds";
            return false;
        }
        KroSection section;
        auto name = string_at(read_u32(bytes, base + 0));
        if (!name) {
            error = "KRO section name is out of bounds";
            return false;
        }
        section.name = std::move(*name);
        section.type = static_cast<KroSectionType>(read_u16(bytes, base + 4));
        section.flags = read_u16(bytes, base + 6);
        section.address = read_u64(bytes, base + 8);
        section.link = read_u32(bytes, base + 36);
        section.info = read_u32(bytes, base + 40);
        section.entry_size = read_u32(bytes, base + 44);
        section.alignment = read_u32(bytes, base + 32);
        section.size = read_u64(bytes, base + 24);
        const auto payload = read_u64(bytes, base + 16);
        if (payload != 0) {
            if (payload + section.size > bytes.size()) {
                error = "KRO section payload is out of bounds";
                return false;
            }
            section.data.assign(bytes.begin() + static_cast<std::size_t>(payload),
                               bytes.begin() + static_cast<std::size_t>(payload + section.size));
        } else if (section.size != 0 && section.type != KroSectionType::Bss) {
            error = "KRO non-bss section has no payload";
            return false;
        }
        object.sections.push_back(std::move(section));
    }

    for (std::uint32_t index = 0; index < symbol_count; ++index) {
        const auto base = symbol_table + index * kSymbolSize;
        if (base + kSymbolSize > bytes.size()) {
            error = "KRO symbol table is out of bounds";
            return false;
        }
        KroSymbol symbol;
        auto name = string_at(read_u32(bytes, base + 0));
        if (!name) {
            error = "KRO symbol name is out of bounds";
            return false;
        }
        symbol.name = std::move(*name);
        symbol.binding = static_cast<KroSymbolBinding>(bytes[base + 4]);
        symbol.type = static_cast<KroSymbolType>(bytes[base + 5]);
        symbol.visibility = static_cast<KroVisibility>(bytes[base + 6]);
        symbol.flags = bytes[base + 7];
        symbol.section = read_u32(bytes, base + 8);
        symbol.value = read_u64(bytes, base + 16);
        symbol.size = read_u64(bytes, base + 24);
        object.symbols.push_back(std::move(symbol));
    }

    for (std::uint32_t index = 0; index < relocation_count; ++index) {
        const auto base = relocation_table + index * kRelocationSize;
        if (base + kRelocationSize > bytes.size()) {
            error = "KRO relocation table is out of bounds";
            return false;
        }
        KroRelocation relocation;
        relocation.section = read_u32(bytes, base + 0);
        relocation.type = static_cast<KroRelocationType>(read_u16(bytes, base + 4));
        relocation.width = bytes[base + 6];
        relocation.flags = bytes[base + 7];
        relocation.offset = read_u64(bytes, base + 8);
        relocation.symbol = read_u32(bytes, base + 16);
        relocation.addend = read_u64(bytes, base + 24);
        if (relocation.section >= object.sections.size() ||
            relocation.symbol >= object.symbols.size()) {
            error = "KRO relocation index is out of range";
            return false;
        }
        object.relocations.push_back(relocation);
    }
    return true;
}

}  // namespace krc::linker
