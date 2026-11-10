#include "asm/kro_writer.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace krc::asmkr32 {

bool make_object(const AssemblyResult& assembly, const target::TargetProfile& profile,
                 linker::KroObject& object, std::string& error) {
    if (profile.id != target::TargetId::Kr32) {
        error = "the KR32 KRO writer only accepts the KR32 target";
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
    text.alignment = 4;
    text.size = assembly.text.size();
    text.data = assembly.text;
    object.sections.push_back(std::move(text));

    linker::KroSection data;
    data.name = ".data";
    data.type = linker::KroSectionType::Data;
    data.flags = 0x1 | 0x2;
    data.alignment = 4;
    data.size = assembly.data.size();
    data.data = assembly.data;
    object.sections.push_back(std::move(data));

    std::unordered_map<std::string, std::uint32_t> symbol_indices;
    for (const auto& symbol : assembly.symbols) {
        linker::KroSymbol output;
        output.name = symbol.name;
        output.binding = symbol.weak
                             ? linker::KroSymbolBinding::Weak
                             : symbol.global ? linker::KroSymbolBinding::Global
                                             : linker::KroSymbolBinding::Local;
        output.type = symbol.section == 0 && !symbol.name.empty() &&
                              symbol.name.front() != '.'
                          ? linker::KroSymbolType::Function
                          : linker::KroSymbolType::Object;
        output.section = symbol.section;
        output.value = symbol.offset;
        output.size = 0;
        symbol_indices.emplace(symbol.name, static_cast<std::uint32_t>(object.symbols.size()));
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
            found = symbol_indices.emplace(external.name,
                                           static_cast<std::uint32_t>(object.symbols.size()))
                        .first;
            object.symbols.push_back(std::move(external));
        }
        linker::KroRelocation output;
        output.section = 0;
        output.type = linker::KroRelocationType::Abs32;
        output.width = 4;
        output.offset = relocation.offset;
        output.symbol = found->second;
        output.addend = 0;
        object.relocations.push_back(output);
    }
    return true;
}

}  // namespace krc::asmkr32
