#include "asm/kr216_object.h"

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace krc::asmkr216 {

namespace {

bool label_defined(const AssembledProgram& assembly, std::string_view name) {
    for (const auto& label : assembly.labels) {
        if (label.first == name) return true;
    }
    for (const auto& section : assembly.data_sections) {
        for (const auto& label : section.labels) {
            if (label.first == name) return true;
        }
    }
    return false;
}

bool label_global(const AssembledProgram& assembly, std::string_view name) {
    for (const auto& global : assembly.globals) {
        if (global == name) return true;
    }
    return false;
}

linker::KroSectionType section_type(std::uint16_t type) {
    switch (type) {
    case 1:
        return linker::KroSectionType::Text;
    case 3:
        return linker::KroSectionType::Rodata;
    case 4:
        return linker::KroSectionType::Bss;
    case 5:
        return linker::KroSectionType::InitArray;
    case 6:
        return linker::KroSectionType::FiniArray;
    default:
        return linker::KroSectionType::Data;
    }
}

}  // namespace

bool make_object(const AssembledProgram& assembly,
                 const target::TargetProfile& profile,
                 linker::KroObject& object, std::string& error) {
    if (profile.id != target::TargetId::Kr216) {
        error = "the KR216 KRO writer only accepts the KR216 target";
        return false;
    }
    for (const auto& name : assembly.globals) {
        if (!label_defined(assembly, name)) {
            error = "kr216 global symbol '" + name + "' is not defined";
            return false;
        }
    }

    object = linker::KroObject{};
    object.kind = linker::KroKind::Object;
    object.target_id = linker::kROTargetId(profile.id);
    object.abi_version = 1;
    object.pointer_width = static_cast<std::uint8_t>(profile.pointer_bits);
    object.endianness = 0;

    linker::KroSection text;
    text.name = ".text";
    text.type = linker::KroSectionType::Text;
    text.flags = 0x1 | 0x4;
    text.alignment = 2;
    text.size = assembly.bytes.size();
    text.data = assembly.bytes;
    object.sections.push_back(std::move(text));

    for (const auto& section : assembly.data_sections) {
        linker::KroSection output;
        output.name = section.name;
        output.type = section_type(section.type);
        output.flags = section.flags;
        output.alignment = section.alignment;
        if (section.type == 4) {
            output.size = section.size;
        } else {
            output.size = section.bytes.size();
            output.data = section.bytes;
        }
        object.sections.push_back(std::move(output));
    }

    std::unordered_map<std::string, std::uint32_t> symbol_indices;
    const auto add_symbol = [&](std::string_view name, std::uint32_t section,
                                std::uint32_t value, linker::KroSymbolType type) {
        if (symbol_indices.contains(std::string(name))) return;
        linker::KroSymbol symbol;
        symbol.name = std::string(name);
        symbol.binding = label_global(assembly, name)
                             ? linker::KroSymbolBinding::Global
                             : linker::KroSymbolBinding::Local;
        symbol.type = type;
        symbol.section = section;
        symbol.value = value;
        symbol_indices.emplace(symbol.name,
                               static_cast<std::uint32_t>(object.symbols.size()));
        object.symbols.push_back(std::move(symbol));
    };
    for (const auto& label : assembly.labels) {
        add_symbol(label.first, 0, label.second, linker::KroSymbolType::Function);
    }
    for (std::size_t index = 0; index < assembly.data_sections.size(); ++index) {
        for (const auto& label : assembly.data_sections[index].labels) {
            add_symbol(label.first, static_cast<std::uint32_t>(index + 1), label.second,
                       linker::KroSymbolType::Object);
        }
    }

    for (const auto& relocation : assembly.relocations) {
        if (relocation.width != 2) {
            error = "kr216 relocation width must be two bytes";
            return false;
        }
        if (relocation.section >= object.sections.size()) {
            error = "kr216 relocation has an invalid section";
            return false;
        }
        auto found = symbol_indices.find(relocation.symbol);
        if (found == symbol_indices.end()) {
            linker::KroSymbol external;
            external.name = relocation.symbol;
            external.binding = linker::KroSymbolBinding::Global;
            external.type = linker::KroSymbolType::NoType;
            external.section = std::numeric_limits<std::uint32_t>::max();
            found = symbol_indices
                        .emplace(external.name,
                                 static_cast<std::uint32_t>(object.symbols.size()))
                        .first;
            object.symbols.push_back(std::move(external));
        }
        linker::KroRelocation output;
        output.section = relocation.section;
        output.type = linker::KroRelocationType::Abs16;
        output.width = 2;
        output.offset = relocation.offset;
        output.symbol = found->second;
        output.addend = 0;
        object.relocations.push_back(output);
    }
    return true;
}

}  // namespace krc::asmkr216
