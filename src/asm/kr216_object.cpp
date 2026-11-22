#include "asm/kr216_object.h"

#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>

namespace krc::asmkr216 {

bool make_object(const AssembledProgram& assembly,
                 const target::TargetProfile& profile,
                 linker::KroObject& object, std::string& error) {
    if (profile.id != target::TargetId::Kr216) {
        error = "the KR216 KRO writer only accepts the KR216 target";
        return false;
    }
    for (const auto& name : assembly.globals) {
        bool defined = false;
        for (const auto& label : assembly.labels) {
            if (label.first == name) {
                defined = true;
                break;
            }
        }
        if (!defined) {
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

    std::unordered_map<std::string, std::uint32_t> symbol_indices;
    for (const auto& label : assembly.labels) {
        bool global = false;
        for (const auto& name : assembly.globals) {
            if (name == label.first) {
                global = true;
                break;
            }
        }
        linker::KroSymbol symbol;
        symbol.name = label.first;
        symbol.binding = global ? linker::KroSymbolBinding::Global
                                : linker::KroSymbolBinding::Local;
        symbol.type = linker::KroSymbolType::Function;
        symbol.section = 0;
        symbol.value = label.second;
        symbol_indices.emplace(symbol.name,
                               static_cast<std::uint32_t>(object.symbols.size()));
        object.symbols.push_back(std::move(symbol));
    }
    for (const auto& relocation : assembly.relocations) {
        if (relocation.width != 2) {
            error = "kr216 relocation width must be two bytes";
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
        output.section = 0;
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
