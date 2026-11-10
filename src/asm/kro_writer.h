#pragma once

#include <string>

#include "asm/kr32_assembler.h"
#include "krc/target/target.h"
#include "linker/kro_object.h"

namespace krc::asmkr32 {

// Converts the in-tree KR32 assembly result into a relocatable KRO object.
[[nodiscard]] bool make_object(const AssemblyResult& assembly,
                               const target::TargetProfile& profile,
                               linker::KroObject& object, std::string& error);

}  // namespace krc::asmkr32
