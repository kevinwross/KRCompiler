#pragma once

#include <string>

#include "asm/kr216_encoder.h"
#include "krc/target/target.h"
#include "linker/kro_object.h"

namespace krc::asmkr216 {

// Converts an assembled KR216 program into a KRO object. `.text` carries the
// instructions and each `.data`/`.rodata`/`.bss`/`.init_array`/`.fini_array`
// directive adds a section; labels become symbols in their own section.
// `.global` selects the binding and an undeclared global with no matching label
// is an error rather than a silent local symbol.
[[nodiscard]] bool make_object(const AssembledProgram& assembly,
                               const target::TargetProfile& profile,
                               linker::KroObject& object, std::string& error);

}  // namespace krc::asmkr216
