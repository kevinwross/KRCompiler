#pragma once

#include <string>

#include "asm/kr16_assembler.h"
#include "krc/target/target.h"
#include "linker/kro_object.h"

namespace krc::asmkr16 {

// Converts the assembled KR16 text/symbol subset into a KRO object. The
// KR16 assembler currently resolves its own labels, so the object carries no
// relocations yet.
[[nodiscard]] bool make_object(const AssemblyResult& assembly,
                               const target::TargetProfile& profile,
                               linker::KroObject& object, std::string& error);

}  // namespace krc::asmkr16
