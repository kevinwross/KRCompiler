#include "kr232_object.h"

#include <limits>

#include "linker/kro_object.h"

namespace krc::asm_kr232 {

bool make_object(const Program& program, const target::TargetProfile& profile,
                 linker::KroObject& object, std::string& error) {
    if (profile.id != target::TargetId::Kr232) {
        error = "the kr232 object writer was given another target's profile";
        return false;
    }
    if (program.sections.empty()) {
        error = "a program needs at least one section";
        return false;
    }
    for (const Symbol& symbol : program.symbols) {
        if (symbol.section >= program.sections.size()) {
            error = "a symbol names a section that does not exist";
            return false;
        }
    }

    object = linker::KroObject{};
    object.kind = linker::KroKind::Object;
    object.target_id = linker::kROTargetId(profile.id);
    object.abi_version = 1;
    object.pointer_width = static_cast<std::uint8_t>(profile.pointer_bits);
    object.endianness = 0;  // little

    for (const Section& section : program.sections) {
        linker::KroSection out;
        out.name = section.name;
        out.alignment = section.alignment;
        out.flags = section.flags;
        out.size = section.bytes.size();
        out.data = section.bytes;
        object.sections.push_back(std::move(out));
    }
    for (const Symbol& symbol : program.symbols) {
        linker::KroSymbol out;
        out.name = symbol.name;
        out.section = symbol.section;
        out.value = symbol.offset;
        out.size = symbol.size;
        out.binding =
            symbol.global ? linker::KroSymbolBinding::Global : linker::KroSymbolBinding::Local;
        object.symbols.push_back(std::move(out));
    }
    // A relocation naming a symbol this object does not define becomes an external
    // symbol, marked the way the other writers mark one: section all ones.
    for (const Program::Relocation& relocation : program.relocations) {
        std::uint32_t index = 0;
        bool found = false;
        for (std::size_t i = 0; i < object.symbols.size(); ++i) {
            if (object.symbols[i].name == relocation.symbol) {
                index = static_cast<std::uint32_t>(i);
                found = true;
                break;
            }
        }
        if (!found) {
            linker::KroSymbol external;
            external.name = relocation.symbol;
            external.binding = linker::KroSymbolBinding::Global;
            external.type = linker::KroSymbolType::NoType;
            external.section = std::numeric_limits<std::uint32_t>::max();
            external.value = 0;
            index = static_cast<std::uint32_t>(object.symbols.size());
            object.symbols.push_back(std::move(external));
        }
        linker::KroRelocation out;
        out.section = relocation.section;
        out.offset = relocation.offset;
        out.width = relocation.absolute ? 4u : 2u;
        out.type = relocation.absolute ? linker::KroRelocationType::Abs32
                                     : linker::KroRelocationType::PcRel16;
        out.symbol = index;
        out.addend = relocation.addend;
        object.relocations.push_back(std::move(out));
    }
    return true;
}

}  // namespace krc::asm_kr232
