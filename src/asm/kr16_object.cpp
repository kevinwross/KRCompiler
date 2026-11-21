#include "asm/kr16_object.h"

#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>

namespace krc::asmkr16 {

bool make_object(const AssemblyResult& assembly,
                 const target::TargetProfile& profile,
                 linker::KroObject& object, std::string& error) {
    if (profile.id != target::TargetId::Kr16) {
        error = "the KR16 KRO writer only accepts the KR16 target";
        return false;
    }
    object = linker::KroObject{};
    object.kind = linker::KroKind::Object;
    object.target_id = linker::kROTargetId(profile.id);
    object.abi_version = 1;
    object.pointer_width = static_cast<std::uint8_t>(profile.pointer_bits);
    object.endianness = 0;

    // A section is present when the source entered it, when a label lives in it,
    // or when a relocation targets it. An otherwise empty section still owns an
    // address if a symbol names it. `.text` is always present; the named
    // `.text.<name>` sections follow it, then `.data` and `.bss`, so an assembly
    // that uses no named section produces exactly the object it always did.
    std::vector<bool> needed(assembly.sections.size(), false);
    if (!assembly.sections.empty()) {
        needed[0] = true;
    }
    for (std::size_t index = 0; index < assembly.sections.size(); ++index) {
        needed[index] = needed[index] || assembly.sections[index].present;
    }
    for (const auto& symbol : assembly.symbols) {
        if (!symbol.external && symbol.section < needed.size()) {
            needed[symbol.section] = true;
        }
    }
    for (const auto& relocation : assembly.relocations) {
        if (relocation.section < needed.size()) {
            needed[relocation.section] = true;
        }
    }

    std::vector<std::uint32_t> output_index(assembly.sections.size(),
                                            std::numeric_limits<std::uint32_t>::max());
    const auto add_section = [&](std::uint32_t id) {
        const auto& source = assembly.sections[id];
        linker::KroSection output;
        output.name = source.name;
        output.type = source.type == 2 ? linker::KroSectionType::Bss
                     : source.type == 1 ? linker::KroSectionType::Data
                                        : linker::KroSectionType::Text;
        output.flags = source.type == 0 ? 0x1 | 0x2 : 0x1 | 0x4;
        output.alignment = 2;
        output.size = source.type == 2 ? source.size : source.data.size();
        output.data = source.type == 2 ? std::vector<std::uint8_t>{}
                                       : source.data;
        output_index[id] = static_cast<std::uint32_t>(object.sections.size());
        object.sections.push_back(std::move(output));
    };
    if (!assembly.sections.empty()) {
        add_section(0);
    }
    for (std::uint32_t id = 3; id < assembly.sections.size(); ++id) {
        if (needed[id]) {
            add_section(id);
        }
    }
    for (const std::uint32_t id : {1u, 2u}) {
        if (id < assembly.sections.size() && needed[id]) {
            add_section(id);
        }
    }

    const auto output_section = [&](std::uint32_t section) -> std::uint32_t {
        if (section >= output_index.size() ||
            output_index[section] == std::numeric_limits<std::uint32_t>::max()) {
            return std::numeric_limits<std::uint32_t>::max();
        }
        return output_index[section];
    };
    const auto is_text_section = [&](std::uint32_t section) {
        return section < assembly.sections.size() &&
               assembly.sections[section].type == 0;
    };

    std::unordered_map<std::string, std::uint32_t> symbol_indices;
    for (const auto& symbol : assembly.symbols) {
        if (symbol.external) {
            continue;
        }
        if (symbol.section >= assembly.sections.size()) {
            error = "KR16 symbol '" + symbol.name + "' has an invalid section";
            return false;
        }
        linker::KroSymbol output;
        output.name = symbol.name;
        output.binding = symbol.weak
                             ? linker::KroSymbolBinding::Weak
                             : (symbol.global ? linker::KroSymbolBinding::Global
                                              : linker::KroSymbolBinding::Local);
        output.type = is_text_section(symbol.section)
                          ? linker::KroSymbolType::Function
                          : linker::KroSymbolType::Object;
        output.section = output_section(symbol.section);
        if (output.section == std::numeric_limits<std::uint32_t>::max()) {
            error = "KR16 symbol '" + symbol.name + "' is in a dropped section";
            return false;
        }
        output.value = symbol.offset;
        output.size = 0;
        symbol_indices.emplace(symbol.name,
                               static_cast<std::uint32_t>(object.symbols.size()));
        object.symbols.push_back(std::move(output));
    }
    for (const auto& relocation : assembly.relocations) {
        if (relocation.section >= assembly.sections.size() ||
            assembly.sections[relocation.section].type == 2) {
            error = "KR16 relocation in section " +
                    std::to_string(relocation.section) + " is not supported";
            return false;
        }
        auto found = symbol_indices.find(relocation.symbol);
        if (found == symbol_indices.end()) {
            linker::KroSymbol external;
            external.name = relocation.symbol;
            external.binding = linker::KroSymbolBinding::Global;
            external.type = linker::KroSymbolType::NoType;
            external.section = std::numeric_limits<std::uint32_t>::max();
            external.value = 0;
            found = symbol_indices
                        .emplace(external.name,
                                 static_cast<std::uint32_t>(
                                     object.symbols.size()))
                        .first;
            object.symbols.push_back(std::move(external));
        }
        linker::KroRelocation output;
        // The section comes from the assembler: a data word holding a symbol -- a
        // vtable's typeinfo or destructor pointer -- patches .data, not .text.
        output.section = output_section(relocation.section);
        if (output.section == std::numeric_limits<std::uint32_t>::max()) {
            error = "KR16 relocation is in a dropped section";
            return false;
        }
        output.type = linker::KroRelocationType::Abs16;
        output.width = 2;
        output.offset = relocation.offset;
        output.symbol = found->second;
        output.addend = 0;
        object.relocations.push_back(output);
    }
    return true;
}

}  // namespace krc::asmkr16
