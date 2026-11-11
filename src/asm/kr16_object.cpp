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

    linker::KroSection text;
    text.name = ".text";
    text.type = linker::KroSectionType::Text;
    text.flags = 0x1 | 0x4;
    text.alignment = 2;
    text.size = assembly.text.size();
    text.data = assembly.text;
    object.sections.push_back(std::move(text));

    std::unordered_map<std::string, std::uint32_t> symbol_indices;
    for (const auto& symbol : assembly.symbols) {
        if (symbol.external) {
            continue;
        }
        linker::KroSymbol output;
        output.name = symbol.name;
        output.binding = symbol.global ? linker::KroSymbolBinding::Global
                                       : linker::KroSymbolBinding::Local;
        output.type = linker::KroSymbolType::Function;
        output.section = 0;
        output.value = symbol.offset;
        output.size = 0;
        symbol_indices.emplace(symbol.name,
                               static_cast<std::uint32_t>(object.symbols.size()));
        object.symbols.push_back(std::move(output));
    }
    for (const auto& relocation : assembly.relocations) {
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

}  // namespace krc::asmkr16
