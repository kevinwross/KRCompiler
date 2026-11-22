#include "asm/kr216_object.h"

#include <cstdint>
#include <string>
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
        object.symbols.push_back(std::move(symbol));
    }
    return true;
}

}  // namespace krc::asmkr216
