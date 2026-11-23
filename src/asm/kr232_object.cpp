#include "kr232_object.h"

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
        out.size = section.bytes.size();
        out.data = section.bytes;
        object.sections.push_back(std::move(out));
    }
    for (const Symbol& symbol : program.symbols) {
        linker::KroSymbol out;
        out.name = symbol.name;
        out.section = symbol.section;
        out.value = symbol.offset;
        out.binding =
            symbol.global ? linker::KroSymbolBinding::Global : linker::KroSymbolBinding::Local;
        object.symbols.push_back(std::move(out));
    }
    return true;
}

}  // namespace krc::asm_kr232
