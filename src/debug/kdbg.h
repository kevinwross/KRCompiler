#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace krc::debug {

struct KdbgSource {
    std::uint32_t id = 0;
    std::string path;
};

struct KdbgLine {
    std::uint32_t address = 0;
    std::uint32_t source = 0;
    std::uint32_t line = 0;
    std::uint32_t column = 0;
};

struct KdbgFunction {
    std::string name;
    std::uint32_t start = 0;
    std::uint32_t end = 0;
    std::uint32_t frame = 0;
    std::string type;
};

struct KdbgSymbol {
    std::string name;
    std::uint32_t address = 0;
    std::uint32_t size = 0;
    std::string type;
};

struct KdbgVariable {
    std::string function;
    std::string name;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    std::string type;
};

struct KdbgMember {
    std::string name;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    std::string type;
};

struct KdbgType {
    std::string name;
    std::uint32_t size = 0;
    std::string kind;
    std::vector<KdbgMember> members;
};

struct KdbgImage {
    std::uint32_t target_id = 1;
    std::uint32_t abi_version = 1;
    std::uint8_t pointer_width = 4;
    std::uint8_t endianness = 1;
    std::uint64_t build_id = 0;
    std::string compiler = "krc";
    std::vector<KdbgSource> sources;
    std::vector<KdbgLine> lines;
    std::vector<KdbgFunction> functions;
    std::vector<KdbgSymbol> symbols;
    std::vector<KdbgVariable> variables;
    std::vector<KdbgType> types;
};

[[nodiscard]] std::vector<std::uint8_t> encode_kdbg(const KdbgImage& image);
[[nodiscard]] bool decode_kdbg(std::span<const std::uint8_t> data, KdbgImage& image,
                               std::string& error);
[[nodiscard]] std::string dump_kdbg_text(const KdbgImage& image);
[[nodiscard]] std::string dump_kdbg_json(const KdbgImage& image);

// Finds a named section in a KR32 `.k32` image.
[[nodiscard]] bool find_k32_section(std::span<const std::uint8_t> image,
                                    std::string_view name,
                                    std::span<const std::uint8_t>& section,
                                    std::string& error);

}  // namespace krc::debug
