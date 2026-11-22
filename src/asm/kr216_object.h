#pragma once

#include <string>

#include "asm/kr216_encoder.h"
#include "krc/target/target.h"
#include "linker/kro_object.h"

namespace krc::asmkr216 {

// Converts an assembled KR216 program into a KRO object. The assembler resolves
// all labels itself, so the object carries a single `.text` section and one
// symbol per label; `.global` selects the binding and an undeclared global with
// no matching label is an error rather than a silent local symbol.
[[nodiscard]] bool make_object(const AssembledProgram& assembly,
                               const target::TargetProfile& profile,
                               linker::KroObject& object, std::string& error);

}  // namespace krc::asmkr216
