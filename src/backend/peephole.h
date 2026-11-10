#pragma once

#include <string>
#include <string_view>

namespace krc::backend {

// Removes identity and zero-strength operations from backend assembly. The pass
// is deliberately textual: it only rewrites exact three-operand/immediate forms
// that the KR32 backend emits.
[[nodiscard]] std::string peephole(std::string_view assembly);

}  // namespace krc::backend
