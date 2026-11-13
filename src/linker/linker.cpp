#include "linker/linker.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <span>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "debug/kdbg.h"

namespace krc::linker {

namespace {

constexpr std::uint32_t kUndefinedSection = 0xFFFFFFFFu;
constexpr std::uint32_t kK32HeaderSize = 64;
constexpr std::uint32_t kK32SectionSize = 32;
constexpr std::uint32_t kK32SymbolSize = 20;

std::uint64_t align_up(std::uint64_t value, std::uint64_t alignment) {
    if (alignment <= 1) {
        return value;
    }
    return (value + alignment - 1) / alignment * alignment;
}

void write_u16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset + 0] = static_cast<std::uint8_t>(value & 0xFF);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
}

void write_u32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned index = 0; index < 4; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>((value >> (index * 8)) & 0xFF);
    }
}

bool write_value(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t value,
                 std::uint8_t width, std::string& error) {
    if (offset + width > bytes.size()) {
        error = "link relocation is outside its section";
        return false;
    }
    if (width != 1 && width != 2 && width != 4 && width != 8) {
        error = "link relocation width is unsupported";
        return false;
    }
    if (width < 8) {
        const auto limit = (1ULL << (width * 8)) - 1;
        if (value > limit) {
            error = "link relocation overflows its width";
            return false;
        }
    }
    for (std::uint8_t index = 0; index < width; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>((value >> (index * 8)) & 0xFF);
    }
    return true;
}

int section_priority(KroSectionType type) {
    switch (type) {
    case KroSectionType::Text:
        return 1;
    case KroSectionType::Rodata:
        return 2;
    case KroSectionType::Data:
        return 3;
    case KroSectionType::Bss:
        return 4;
    default:
        return 5;
    }
}

std::uint16_t kr32_section_type(KroSectionType type) {
    switch (type) {
    case KroSectionType::Text:
        return 1;
    case KroSectionType::Rodata:
        return 2;
    case KroSectionType::Data:
        return 3;
    case KroSectionType::Bss:
        return 4;
    case KroSectionType::Debug:
        return 5;
    default:
        return 0;
    }
}

std::string merge_debug_records(std::span<const KroObject> objects) {
    std::string merged;
    std::unordered_map<std::string, std::uint32_t> sources;
    std::vector<std::unordered_map<std::uint32_t, std::uint32_t>> remaps(objects.size());
    std::uint32_t next_source = 0;
    for (std::size_t object_index = 0; object_index < objects.size(); ++object_index) {
        for (const auto& section : objects[object_index].sections) {
            if (section.name != ".debug_info" || section.data.empty()) {
                continue;
            }
            std::size_t position = 0;
            while (position < section.data.size()) {
                const auto begin = position;
                while (position < section.data.size() && section.data[position] != 0) {
                    ++position;
                }
                const std::string record(
                    reinterpret_cast<const char*>(section.data.data() + begin), position - begin);
                if (position < section.data.size()) {
                    ++position;
                }
                if (record.empty()) {
                    continue;
                }
                if (record.rfind("S ", 0) == 0) {
                    const auto id_end = record.find(' ', 2);
                    if (id_end == std::string::npos) {
                        merged += record;
                        merged.push_back('\0');
                        continue;
                    }
                    const auto path = record.substr(id_end + 1);
                    const auto found = sources.find(path);
                    std::uint32_t id = 0;
                    if (found == sources.end()) {
                        id = next_source++;
                        sources.emplace(path, id);
                    } else {
                        id = found->second;
                    }
                    const auto old_id = static_cast<std::uint32_t>(
                        std::stoul(record.substr(2, id_end - 2)));
                    remaps[object_index][old_id] = id;
                    merged += "S " + std::to_string(id) + " " + path;
                    merged.push_back('\0');
                    continue;
                }
                if (record.rfind("L ", 0) == 0) {
                    const auto label_end = record.find(' ', 2);
                    const auto id_end = label_end == std::string::npos
                                            ? std::string::npos
                                            : record.find(' ', label_end + 1);
                    if (label_end == std::string::npos || id_end == std::string::npos) {
                        merged += record;
                        merged.push_back('\0');
                        continue;
                    }
                    const auto old_id = static_cast<std::uint32_t>(
                        std::stoul(record.substr(label_end + 1, id_end - label_end - 1)));
                    const auto mapped = remaps[object_index].find(old_id);
                    const auto new_id = mapped == remaps[object_index].end() ? old_id
                                                                             : mapped->second;
                    merged += record.substr(0, label_end + 1) + std::to_string(new_id) +
                              record.substr(id_end);
                    merged.push_back('\0');
                    continue;
                }
                merged += record;
                merged.push_back('\0');
            }
        }
    }
    return merged;
}

}  // namespace

bool link_objects(std::span<const KroObject> objects, const LinkOptions& options,
                  LinkedImage& image, std::string& error) {
    image = LinkedImage{};
    if (objects.empty()) {
        error = "linker requires at least one object";
        return false;
    }
    const auto& first = objects.front();
    for (const auto& object : objects) {
        if (object.target_id != first.target_id) {
            error = "objects have different target IDs";
            return false;
        }
        if (object.abi_version != first.abi_version) {
            error = "objects have different ABI versions";
            return false;
        }
        if (object.pointer_width != first.pointer_width) {
            error = "objects have different pointer widths";
            return false;
        }
        if (object.endianness != first.endianness) {
            error = "objects have different endianness";
            return false;
        }
    }

    struct OutputSection {
        std::string name;
        KroSectionType type;
        std::uint16_t flags;
        std::uint64_t alignment;
        std::vector<std::uint8_t> data;
        std::uint64_t size;
    };
    std::vector<OutputSection> sections;
    std::unordered_map<std::string, std::size_t> section_index;
    std::vector<std::vector<std::uint32_t>> offsets(objects.size());
    for (std::size_t object_index = 0; object_index < objects.size(); ++object_index) {
        const auto& object = objects[object_index];
        offsets[object_index].assign(object.sections.size(), 0);
        for (std::size_t index = 0; index < object.sections.size(); ++index) {
            const auto& section = object.sections[index];
            auto found = section_index.find(section.name);
            if (found == section_index.end()) {
                OutputSection output;
                output.name = section.name;
                output.type = section.type;
                output.flags = section.flags;
                output.alignment = std::max<std::uint64_t>(1, section.alignment);
                output.data.clear();
                output.size = 0;
                found = section_index.emplace(section.name, sections.size()).first;
                sections.push_back(std::move(output));
            }
            auto& output = sections[found->second];
            if (output.type != section.type) {
                error = "section '" + section.name + "' has conflicting types";
                return false;
            }
            output.alignment = std::max(output.alignment, section.alignment);
            if (section.type == KroSectionType::Bss) {
                output.size = std::max(output.size, section.size);
            } else {
                const auto offset = static_cast<std::uint32_t>(
                    align_up(output.data.size(), std::max<std::uint64_t>(1, section.alignment)));
                output.data.resize(static_cast<std::size_t>(offset));
                output.data.insert(output.data.end(), section.data.begin(), section.data.end());
                output.size = output.data.size();
                offsets[object_index][index] = offset;
            }
        }
    }

    const auto debug_section = section_index.find(".debug_info");
    if (debug_section != section_index.end()) {
        const auto merged = merge_debug_records(objects);
        auto& output = sections[debug_section->second];
        output.data.assign(merged.begin(), merged.end());
        output.size = output.data.size();
    }

    std::vector<std::size_t> order(sections.size());
    for (std::size_t index = 0; index < order.size(); ++index) {
        order[index] = index;
    }
    std::sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
        const auto left_priority = section_priority(sections[left].type);
        const auto right_priority = section_priority(sections[right].type);
        if (left_priority != right_priority) {
            return left_priority < right_priority;
        }
        return sections[left].name < sections[right].name;
    });

    std::uint64_t cursor = options.base;
    for (const auto index : order) {
        auto& section = sections[index];
        const auto placement = options.section_addresses.find(section.name);
        const auto requested = placement == options.section_addresses.end()
                                   ? cursor
                                   : placement->second;
        cursor = align_up(requested, std::max<std::uint64_t>(1, section.alignment));
        LinkedSection output;
        output.name = section.name;
        output.type = section.type;
        output.flags = section.flags;
        output.alignment = section.alignment;
        output.address = cursor;
        output.data = section.data;
        output.size = section.size;
        image.sections.push_back(std::move(output));
        cursor += section.size;
    }
    image.base = options.base;

    std::vector<std::uint32_t> output_section_for(objects.size());
    for (std::size_t object_index = 0; object_index < objects.size(); ++object_index) {
        for (std::size_t index = 0; index < objects[object_index].sections.size(); ++index) {
            const auto& name = objects[object_index].sections[index].name;
            const auto found = section_index.find(name);
            if (found == section_index.end()) {
                error = "internal linker section lookup failed";
                return false;
            }
            static_cast<void>(found);
        }
    }

    // Map each source section to its merged section index and final address.
    std::vector<std::vector<std::uint32_t>> source_section(objects.size());
    for (std::size_t object_index = 0; object_index < objects.size(); ++object_index) {
        source_section[object_index].resize(objects[object_index].sections.size());
        for (std::size_t index = 0; index < objects[object_index].sections.size(); ++index) {
            const auto& name = objects[object_index].sections[index].name;
            const auto found = section_index.find(name);
            std::size_t output_index = 0;
            for (std::size_t candidate = 0; candidate < image.sections.size(); ++candidate) {
                if (image.sections[candidate].name == name) {
                    output_index = candidate;
                    break;
                }
            }
            static_cast<void>(found);
            source_section[object_index][index] = static_cast<std::uint32_t>(output_index);
        }
    }

    struct Definition {
        std::uint64_t address;
        bool weak;
        std::size_t object;
    };
    std::unordered_map<std::string, Definition> definitions;
    for (std::size_t object_index = 0; object_index < objects.size(); ++object_index) {
        const auto& object = objects[object_index];
        for (const auto& symbol : object.symbols) {
            if (symbol.section == kUndefinedSection) {
                continue;
            }
            if (symbol.section >= object.sections.size()) {
                error = "symbol '" + symbol.name + "' has an invalid section";
                return false;
            }
            if (symbol.binding == KroSymbolBinding::Local) {
                continue;
            }
            const auto output_section = source_section[object_index][symbol.section];
            const auto address = image.sections[output_section].address +
                                 offsets[object_index][symbol.section] + symbol.value;
            const bool weak = symbol.binding == KroSymbolBinding::Weak;
            const auto found = definitions.find(symbol.name);
            if (found != definitions.end()) {
                if (!found->second.weak && !weak) {
                    error = "duplicate symbol '" + symbol.name + "'";
                    return false;
                }
                if (found->second.weak && !weak) {
                    definitions[symbol.name] = {address, false, object_index};
                }
                continue;
            }
            definitions.emplace(symbol.name, Definition{address, weak, object_index});
        }
    }

    for (std::size_t object_index = 0; object_index < objects.size(); ++object_index) {
        const auto& object = objects[object_index];
        for (std::size_t symbol_index = 0; symbol_index < object.symbols.size(); ++symbol_index) {
            const auto& symbol = object.symbols[symbol_index];
            if (symbol.section == kUndefinedSection) {
                continue;
            }
            if (symbol.section >= object.sections.size()) {
                continue;
            }
            const auto output_section = source_section[object_index][symbol.section];
            LinkedSymbol linked;
            linked.name = symbol.name;
            linked.value = image.sections[output_section].address +
                           offsets[object_index][symbol.section] + symbol.value;
            linked.size = symbol.size;
            linked.section = output_section;
            linked.binding = symbol.binding;
            linked.type = symbol.type;
            image.symbols.push_back(std::move(linked));
        }
    }

    std::uint64_t image_end = options.base;
    for (const auto& section : image.sections) {
        image_end = std::max(image_end, section.address + section.size);
    }
    std::uint64_t eh_start = 0;
    std::uint64_t eh_end = 0;
    for (const auto& section : image.sections) {
        if (section.name == ".cpp_except") {
            eh_start = section.address;
            eh_end = section.address + section.size;
        }
    }
    const auto heap_start = align_up(image_end, 8);
    constexpr std::uint64_t stack_top = 0x08010000;
    constexpr std::uint64_t stack_limit = stack_top - 0x10000;
    const std::pair<std::string, std::uint64_t> synthetic[] = {
        {"__heap_start", heap_start},
        {"__heap_end", stack_limit},
        {"__stack_top", stack_top},
        {"__stack_limit", stack_limit},
        {"__krc_eh_start", eh_start},
        {"__krc_eh_end", eh_end},
    };
    for (const auto& [name, address] : synthetic) {
        if (definitions.find(name) != definitions.end()) {
            continue;
        }
        definitions.emplace(name, Definition{address, false, 0});
        LinkedSymbol linked;
        linked.name = name;
        linked.value = address;
        linked.section = 0;
        linked.binding = KroSymbolBinding::Global;
        linked.type = KroSymbolType::NoType;
        image.symbols.push_back(std::move(linked));
    }

    const auto resolve = [&](std::size_t object_index, std::uint32_t symbol_index,
                             std::uint64_t& address) -> bool {
        if (symbol_index >= objects[object_index].symbols.size()) {
            error = "relocation references an invalid symbol";
            return false;
        }
        const auto& symbol = objects[object_index].symbols[symbol_index];
        if (symbol.section != kUndefinedSection) {
            if (symbol.section >= objects[object_index].sections.size()) {
                error = "defined symbol has an invalid section";
                return false;
            }
            const auto output_section = source_section[object_index][symbol.section];
            address = image.sections[output_section].address +
                      offsets[object_index][symbol.section] + symbol.value;
            return true;
        }
        const auto found = definitions.find(symbol.name);
        if (found == definitions.end()) {
            error = "undefined symbol '" + symbol.name + "'";
            return false;
        }
        address = found->second.address;
        return true;
    };

    for (std::size_t object_index = 0; object_index < objects.size(); ++object_index) {
        const auto& object = objects[object_index];
        for (const auto& relocation : object.relocations) {
            if (relocation.section >= object.sections.size()) {
                error = "relocation has an invalid section";
                return false;
            }
            std::uint64_t target = 0;
            if (!resolve(object_index, relocation.symbol, target)) {
                return false;
            }
            const auto output_section = source_section[object_index][relocation.section];
            auto& section = image.sections[output_section];
            const auto offset = static_cast<std::size_t>(
                offsets[object_index][relocation.section] + relocation.offset);
            std::uint64_t value = target + relocation.addend;
            if (relocation.type == KroRelocationType::PcRel32 ||
                relocation.type == KroRelocationType::PcRel64 ||
                relocation.type == KroRelocationType::PcRel16 ||
                relocation.type == KroRelocationType::PcRel8) {
                value -= section.address + offset;
            }
            if (section.type == KroSectionType::Bss) {
                error = "relocation cannot target bss";
                return false;
            }
            if (!write_value(section.data, offset, value, relocation.width, error)) {
                return false;
            }
        }
    }

    const auto entry = definitions.find(options.entry_symbol);
    if (entry == definitions.end()) {
        error = "entry symbol '" + options.entry_symbol + "' is not defined";
        return false;
    }
    image.entry = entry->second.address;
    return true;
}

bool attach_kdbg(LinkedImage& image, std::string& error) {
    return attach_kdbg(image, std::string{}, error);
}

bool attach_kdbg(LinkedImage& image, const std::string& source_root,
                 std::string& error) {
    static_cast<void>(error);
    const auto debug_section =
        std::find_if(image.sections.begin(), image.sections.end(),
                     [](const LinkedSection& section) {
                         return section.name == ".debug_info";
                     });
    if (debug_section == image.sections.end()) {
        return true;
    }

    std::unordered_map<std::string, std::uint32_t> symbols;
    for (const auto& symbol : image.symbols) {
        symbols.emplace(symbol.name, static_cast<std::uint32_t>(symbol.value));
    }

    debug::KdbgImage kdbg;
    kdbg.target_id = 1;
    kdbg.abi_version = 1;
    kdbg.compiler = "krc";
    std::unordered_map<std::string, std::size_t> type_index;
    const auto ensure_type = [&](const std::string& name, std::uint32_t size,
                                 const std::string& kind) -> debug::KdbgType& {
        const auto found = type_index.find(name);
        if (found != type_index.end()) {
            return kdbg.types[found->second];
        }
        type_index.emplace(name, kdbg.types.size());
        debug::KdbgType type;
        type.name = name;
        type.size = size;
        type.kind = kind;
        kdbg.types.push_back(std::move(type));
        return kdbg.types.back();
    };

    const std::string_view data(
        reinterpret_cast<const char*>(debug_section->data.data()), debug_section->data.size());
    std::size_t position = 0;
    while (position < data.size()) {
        const auto end = data.find('\0', position);
        const auto record = data.substr(position, end == std::string_view::npos
                                                     ? data.size() - position
                                                     : end - position);
        position = end == std::string_view::npos ? data.size() : end + 1;
        if (record.empty()) {
            continue;
        }
        std::istringstream stream{std::string(record)};
        std::string tag;
        stream >> tag;
        if (tag == "S") {
            debug::KdbgSource source;
            stream >> source.id >> source.path;
            if (!source_root.empty()) {
                const std::filesystem::path path(source.path);
                const std::filesystem::path root(source_root);
                std::error_code ec;
                const auto relative = std::filesystem::relative(path, root, ec);
                const auto relative_text = relative.generic_string();
                if (!ec && !relative.empty() &&
                    relative_text.rfind("../", 0) != 0) {
                    source.path = relative_text;
                }
            }
            kdbg.sources.push_back(std::move(source));
        } else if (tag == "L") {
            std::string label;
            debug::KdbgLine line;
            stream >> label >> line.source >> line.line >> line.column;
            const auto found = symbols.find(label);
            if (found != symbols.end()) {
                line.address = found->second;
                kdbg.lines.push_back(line);
            }
        } else if (tag == "F") {
            debug::KdbgFunction function;
            std::string start;
            std::string finish;
            stream >> function.name >> start >> finish >> function.frame >> function.type;
            const auto start_found = symbols.find(start);
            const auto end_found = symbols.find(finish);
            if (start_found != symbols.end() && end_found != symbols.end()) {
                function.start = start_found->second;
                function.end = end_found->second;
                kdbg.functions.push_back(std::move(function));
            }
        } else if (tag == "G") {
            debug::KdbgSymbol symbol;
            std::string label;
            stream >> symbol.name >> label >> symbol.type >> symbol.size;
            const auto found = symbols.find(label);
            if (found != symbols.end()) {
                symbol.address = found->second;
                kdbg.symbols.push_back(std::move(symbol));
            }
        } else if (tag == "V") {
            debug::KdbgVariable variable;
            stream >> variable.function >> variable.name >> variable.offset >>
                variable.type >> variable.size;
            kdbg.variables.push_back(std::move(variable));
        } else if (tag == "T") {
            std::string name;
            std::uint32_t size = 0;
            stream >> name >> size;
            ensure_type(name, size, "builtin");
        } else if (tag == "C") {
            std::string name;
            std::uint32_t size = 0;
            stream >> name >> size;
            auto& type = ensure_type(name, size, "struct");
            type.kind = "struct";
            if (type.size == 0) {
                type.size = size;
            }
        } else if (tag == "M") {
            std::string type_name;
            debug::KdbgMember member;
            stream >> type_name >> member.name >> member.offset >> member.type >> member.size;
            auto& type = ensure_type(type_name, 0, "struct");
            type.kind = "struct";
            type.members.push_back(std::move(member));
        }
    }

    const auto bytes = debug::encode_kdbg(kdbg);
    LinkedSection section;
    section.name = ".kdbg";
    section.type = KroSectionType::Debug;
    section.flags = 0;
    section.address = 0;
    section.alignment = 1;
    section.data = bytes;
    section.size = bytes.size();

    // The debugger reports source paths from .debug_info, so that section has
    // to carry the same root-relative paths this function just wrote into
    // .kdbg. Its records are NUL-separated text; only the S records change.
    std::string rewritten;
    {
        const auto& text = debug_section->data;
        std::size_t start = 0;
        while (start < text.size()) {
            std::size_t end = start;
            while (end < text.size() && text[end] != 0) {
                ++end;
            }
            std::string record(
                reinterpret_cast<const char*>(text.data() + start), end - start);
            if (record.rfind("S ", 0) == 0) {
                const auto id_end = record.find(' ', 2);
                if (id_end != std::string::npos) {
                    const auto id = static_cast<std::uint32_t>(
                        std::stoul(record.substr(2, id_end - 2)));
                    for (const auto& source : kdbg.sources) {
                        if (source.id == id) {
                            record = record.substr(0, id_end + 1) + source.path;
                            break;
                        }
                    }
                }
            }
            rewritten += record;
            rewritten.push_back('\0');
            start = end + 1;
        }
    }
    if (!rewritten.empty()) {
        debug_section->data.assign(rewritten.begin(), rewritten.end());
    }
    image.sections.push_back(std::move(section));
    return true;
}

bool write_kr32_executable(const LinkedImage& image, std::vector<std::uint8_t>& bytes,
                           std::string& error) {
    bytes.clear();
    std::vector<LinkedSymbol> symbols = image.symbols;
    std::sort(symbols.begin(), symbols.end(), [](const LinkedSymbol& left,
                                                 const LinkedSymbol& right) {
        const bool left_local = left.binding == KroSymbolBinding::Local;
        const bool right_local = right.binding == KroSymbolBinding::Local;
        if (left_local != right_local) {
            return !left_local;
        }
        if (left.name != right.name) {
            return left.name < right.name;
        }
        return left.value < right.value;
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
    for (const auto& section : image.sections) {
        static_cast<void>(intern(section.name));
    }
    for (const auto& symbol : symbols) {
        static_cast<void>(intern(symbol.name));
    }

    const auto section_table = kK32HeaderSize;
    const auto symbol_table = section_table + image.sections.size() * kK32SectionSize;
    const auto string_table = symbol_table + symbols.size() * kK32SymbolSize;
    std::uint64_t payload = align_up(string_table + string_size, 16);
    if (payload > std::numeric_limits<std::uint32_t>::max()) {
        error = "KR32 executable exceeds 32-bit addressing";
        return false;
    }
    bytes.assign(static_cast<std::size_t>(payload), 0);
    bytes[0] = 'K';
    bytes[1] = '3';
    bytes[2] = '2';
    bytes[3] = 0;
    write_u16(bytes, 4, 1);
    write_u16(bytes, 6, 1);
    write_u16(bytes, 8, static_cast<std::uint16_t>(kK32HeaderSize));
    write_u16(bytes, 10, 0);
    write_u32(bytes, 12, static_cast<std::uint32_t>(image.sections.size()));
    write_u32(bytes, 16, static_cast<std::uint32_t>(symbols.size()));
    write_u32(bytes, 20, 0);
    if (image.entry > std::numeric_limits<std::uint32_t>::max() ||
        image.base > std::numeric_limits<std::uint32_t>::max()) {
        error = "KR32 executable entry/base exceeds 32 bits";
        return false;
    }
    write_u32(bytes, 24, static_cast<std::uint32_t>(image.entry));
    write_u32(bytes, 28, static_cast<std::uint32_t>(image.base));
    write_u32(bytes, 32, static_cast<std::uint32_t>(string_table));
    write_u32(bytes, 36, static_cast<std::uint32_t>(string_size));

    for (std::size_t index = 0; index < image.sections.size(); ++index) {
        const auto& section = image.sections[index];
        const auto base = section_table + index * kK32SectionSize;
        write_u32(bytes, base + 0, intern(section.name));
        write_u16(bytes, base + 4, kr32_section_type(section.type));
        write_u16(bytes, base + 6, section.flags);
        if (section.address > std::numeric_limits<std::uint32_t>::max()) {
            error = "KR32 section address exceeds 32 bits";
            return false;
        }
        write_u32(bytes, base + 8, static_cast<std::uint32_t>(section.address));
        write_u32(bytes, base + 12, 0);
        write_u32(bytes, base + 16, static_cast<std::uint32_t>(section.size));
        write_u32(bytes, base + 20, static_cast<std::uint32_t>(section.alignment));
    }

    for (std::size_t index = 0; index < symbols.size(); ++index) {
        const auto& symbol = symbols[index];
        const auto base = symbol_table + index * kK32SymbolSize;
        write_u32(bytes, base + 0, intern(symbol.name));
        if (symbol.value > std::numeric_limits<std::uint32_t>::max()) {
            error = "KR32 symbol value exceeds 32 bits";
            return false;
        }
        write_u32(bytes, base + 4, static_cast<std::uint32_t>(symbol.value));
        write_u32(bytes, base + 8, static_cast<std::uint32_t>(symbol.size));
        write_u16(bytes, base + 12,
                  static_cast<std::uint16_t>(symbol.section == kUndefinedSection
                                                 ? 0xFFFF
                                                 : symbol.section));
        bytes[base + 14] = static_cast<std::uint8_t>(symbol.binding);
        bytes[base + 15] = 0;
    }

    bytes[static_cast<std::size_t>(string_table)] = 0;
    std::size_t cursor = 1;
    for (const auto& name : strings) {
        std::memcpy(bytes.data() + string_table + cursor, name.data(), name.size());
        cursor += name.size() + 1;
    }

    std::uint64_t payload_cursor = payload;
    for (std::size_t index = 0; index < image.sections.size(); ++index) {
        const auto& section = image.sections[index];
        const auto base = section_table + index * kK32SectionSize;
        if (section.data.empty()) {
            write_u32(bytes, base + 12, 0);
            continue;
        }
        payload_cursor = align_up(payload_cursor, std::max<std::uint64_t>(16, section.alignment));
        bytes.resize(static_cast<std::size_t>(payload_cursor + section.data.size()));
        std::memcpy(bytes.data() + payload_cursor, section.data.data(), section.data.size());
        write_u32(bytes, base + 12, static_cast<std::uint32_t>(payload_cursor));
        payload_cursor += section.data.size();
    }
    bytes.resize(static_cast<std::size_t>(payload_cursor));
    if (bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
        error = "KR32 executable exceeds 32-bit file size";
        return false;
    }
    write_u32(bytes, 40, static_cast<std::uint32_t>(bytes.size()));
    write_u32(bytes, 44, 0);
    write_u32(bytes, 44, crc32(std::span<const std::uint8_t>(bytes).subspan(kK32HeaderSize)));
    return true;
}

}  // namespace krc::linker
