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
    // address if a symbol names it.
    bool need_data = assembly.has_data;
    bool need_bss = assembly.has_bss;
    for (const auto& symbol : assembly.symbols) {
        need_data = need_data || symbol.section == 1;
        need_bss = need_bss || symbol.section == 2;
    }
    for (const auto& relocation : assembly.relocations) {
        need_data = need_data || relocation.section == 1;
    }
    constexpr std::uint32_t kTextIndex = 0;
    const std::uint32_t kDataIndex = 1;
    const std::uint32_t kBssIndex = need_data ? 2 : 1;

    linker::KroSection text;
    text.name = ".text";
    text.type = linker::KroSectionType::Text;
    text.flags = 0x1 | 0x2;
    text.alignment = 2;
    text.size = assembly.text.size();
    text.data = assembly.text;
    object.sections.push_back(std::move(text));

    if (need_data) {
        linker::KroSection data;
        data.name = ".data";
        data.type = linker::KroSectionType::Data;
        data.flags = 0x1 | 0x4;
        data.alignment = 2;
        data.size = assembly.data.size();
        data.data = assembly.data;
        object.sections.push_back(std::move(data));
    }
    if (need_bss) {
        // NOBITS: the linker reserves the address range and the image records a
        // size with no bytes, so the loader zero-fills it.
        linker::KroSection bss;
        bss.name = ".bss";
        bss.type = linker::KroSectionType::Bss;
        bss.flags = 0x1 | 0x4;
        bss.alignment = 2;
        bss.size = assembly.bss_size;
        object.sections.push_back(std::move(bss));
    }

    const auto output_section = [&](std::uint32_t section) -> std::uint32_t {
        if (section == 1) {
            return kDataIndex;
        }
        if (section == 2) {
            return kBssIndex;
        }
        return kTextIndex;
    };

    std::unordered_map<std::string, std::uint32_t> symbol_indices;
    for (const auto& symbol : assembly.symbols) {
        if (symbol.external) {
            continue;
        }
        if (symbol.section > 2) {
            error = "KR16 symbol '" + symbol.name + "' has an invalid section";
            return false;
        }
        linker::KroSymbol output;
        output.name = symbol.name;
        output.binding = symbol.weak
                             ? linker::KroSymbolBinding::Weak
                             : (symbol.global ? linker::KroSymbolBinding::Global
                                              : linker::KroSymbolBinding::Local);
        output.type = symbol.section == 0 ? linker::KroSymbolType::Function
                                          : linker::KroSymbolType::Object;
        output.section = output_section(symbol.section);
        output.value = symbol.offset;
        output.size = 0;
        symbol_indices.emplace(symbol.name,
                               static_cast<std::uint32_t>(object.symbols.size()));
        object.symbols.push_back(std::move(output));
    }
    for (const auto& relocation : assembly.relocations) {
        if (relocation.section > 1) {
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
